/*
 * FAAR - Freeware Advanced Audio Resampler
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

/*
 * libfaar resampler API.
 *
 * Converts interleaved 32-bit float PCM between sample rates on the CPU. The
 * caller owns every buffer and handle; the library never reads or writes files.
 *
 * Allocation uses overridable AllocMemory / FreeMemory macros in source builds.
 *
 * Design summary:
 *   - Configuration is supplied once, up front, to faar_resampler_open(), so a
 *     resampler never exists half-configured. The required lifecycle is
 *     faar_config_init() -> faar_resampler_open() -> faar_resampler_process()
 *     -> faar_resampler_finalize() -> faar_resampler_close().
 *   - Two optional refinements need no extra calls: faar_config.max_input_frames
 *     reserves all streaming storage in open() for allocation-free processing,
 *     and a faar_design (faar_design_open()) shares one filter design among
 *     many identical streams.
 *   - Every fallible call returns a faar_status; faar_strerror() maps a status
 *     to a human-readable string.
 *   - Fixed-width integer types and width-pinned enums preserve the ABI within
 *     each target platform, including builds using -fshort-enums.
 *   - faar_config and the info structs support size-based extension; callers
 *     MUST set struct_size (faar_config_init() does) so the library can
 *     reconcile versions. The library accepts any size at or above the
 *     FAAR_*_BASELINE of that struct, reads only the fields that size covers
 *     and, for output structs, writes at most struct_size bytes. New fields are
 *     appended at or after the previous sizeof(struct); compiler padding is
 *     never reused. Absent input fields take their zero value, so zero keeps
 *     the previous behavior. faar_config and
 *     faar_resampler_info are 4-byte aligned: append only members of 4 bytes or
 *     less. Structs holding pointers take members at multiples of their size, in
 *     blocks of 8 bytes.
 *     Callers must check library support (the struct_size written back by the
 *     library) before requesting new behavior. Baseline sizes, enum values,
 *     signatures and offsets of released fields stay fixed within a major version.
 *   - FAAR_VERSION_MAJOR tracks the libfaar SONAME, not the project release.
 *     Structs hold fixed-width integers only; size_t appears in signatures and
 *     follows the platform ABI.
 *   - Independent handles may be used concurrently; one handle is owned by one
 *     thread at a time.
 *
 * Status contract:
 *   - A NULL pointer, NULL out parameter or otherwise invalid argument or
 *     configuration returns FAAR_ERR_INVALID_ARGUMENT.
 *   - FAAR_ERR_UNSUPPORTED is reserved for a configuration a build refuses on
 *     purpose; this release does not return it.
 *   - An allocation that failed, or a filter design the planner could not
 *     build, returns FAAR_ERR_NO_MEMORY.
 *   - A failure while resampling returns FAAR_ERR_PROCESSING; discard and close
 *     that handle.
 *   - On failure open functions set *out to NULL before anything else, so the
 *     pointer is always safe to pass to faar_resampler_close().
 */

#ifndef FAAR_H
#define FAAR_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stddef.h>
#include <stdint.h>

#define FAAR_VERSION_MAJOR 3
#define FAAR_VERSION_MINOR 0
#define FAAR_VERSION_PATCH 0
#define FAAR_STR_(x) #x
#define FAAR_STR(x) FAAR_STR_(x)
#define FAAR_VERSION_STRING \
    FAAR_STR(FAAR_VERSION_MAJOR) "." FAAR_STR(FAAR_VERSION_MINOR) "." FAAR_STR(FAAR_VERSION_PATCH)
#define FAAR_VERSION_HEX \
    ((FAAR_VERSION_MAJOR << 16) | (FAAR_VERSION_MINOR << 8) | FAAR_VERSION_PATCH)

/* Windows exports from DLL build only (FAAR_BUILDING). No dllimport: the API
 * has no data, so DLL and static consumers link the same way with no define. */
#ifndef FAARAPI
# if defined(_WIN32)
#  ifdef FAAR_BUILDING
#   define FAARAPI __declspec(dllexport)
#  else
#   define FAARAPI
#  endif
# elif defined(__GNUC__) && (__GNUC__ >= 4)
#  define FAARAPI __attribute__((visibility("default")))
# else
#  define FAARAPI
# endif
#endif

/* Resampler quality presets. */
typedef enum faar_quality {
    FAAR_QUALITY_FAST = 0,  /* lower-cost filter; narrower passband                        */
    FAAR_QUALITY_GOOD = 1,  /* default profile: wide passband and strong rejection         */
    FAAR_QUALITY_HIGH = 2,  /* stronger rejection, higher cost; float builds use compensated arithmetic */
    FAAR_QUALITY_MAX  = 0x7fffffff
} faar_quality;

/* faar_config.flags: dither for a later bit-depth truncation; FAAR itself outputs
 * float. Zero is no dither. Choose exactly one mode and exactly one depth, or none
 * of the four; anything else, including unknown bits, is FAAR_ERR_INVALID_ARGUMENT. */
enum faar_flag {
    FAAR_DITHER_TPDF   = 0x1, /* triangular probability density function dither            */
    FAAR_DITHER_SHAPED = 0x2, /* TPDF dither with high-pass noise shaping error feedback   */
    FAAR_DITHER_16     = 0x4, /* target depth: 16-bit integer output                       */
    FAAR_DITHER_24     = 0x8, /* target depth: 24-bit integer output                       */
    FAAR_FLAG_MAX      = 0x7fffffff
};

/*
 * Result codes. All values are negative except FAAR_OK so a caller can test
 * `status < 0` for failure. The FAAR_STATUS_MAX sentinel pins the enum to a
 * 32-bit underlying type for a stable ABI under -fshort-enums.
 */
typedef enum faar_status {
    FAAR_OK                   = 0,
    FAAR_ERR_INVALID_ARGUMENT = -1, /* NULL pointer, bad arguments or bad configuration  */
    FAAR_ERR_UNSUPPORTED      = -2, /* reserved; not returned by this release            */
    FAAR_ERR_NO_MEMORY        = -3, /* allocation or filter design failed, or reserved queue exhausted */
    FAAR_ERR_PROCESSING       = -4, /* resampling failed; discard and close the handle   */
    FAAR_STATUS_MAX           = 0x7fffffff
} faar_status;

/* faar_library_info.flags */
enum faar_library_flag {
    FAAR_LIB_FLOAT_ONLY = 1u << 0, /* resampler-float=true: the sample path never uses double,
                                    * so High runs on emulated extended precision and costs
                                    * about ten times more than in the default build */
    FAAR_LIB_FLAG_MAX   = 0x7fffffff
};

/*
 * Resampler configuration, supplied once at open.
 *
 * ALWAYS initialize with faar_config_init() before setting fields: it stamps
 * struct_size, which is how the library stays compatible as this struct grows.
 */
typedef struct faar_config {
    uint32_t       struct_size;       /* set by faar_config_init()                         */

    uint32_t       in_rate;           /* input sample rate in Hz (required)                */
    uint32_t       out_rate;          /* output sample rate in Hz (required)               */
    uint32_t       channels;          /* interleaved channels, 1..faar_library_info.max_channels */
    faar_quality   quality;           /* filter preset; default GOOD                       */
    uint32_t       flags;             /* FAAR_DITHER_* bits; default 0 (no dither)         */
    uint32_t       decimate_factor;   /* 0 plans automatically; 2 or more requests an explicit cascade */
    uint32_t       max_input_frames;  /* 0 grows storage on demand. Nonzero bounds the frames per
                                       * process call and reserves all streaming storage in
                                       * open(): process and finalize then never allocate or free
                                       * heap storage. A larger chunk is rejected with
                                       * FAAR_ERR_INVALID_ARGUMENT before consuming input.
                                       * Drain with NULL/0 input and nonzero output capacity
                                       * until no frames are made before feeding more input;
                                       * exhausting the reserved output queue is a processing
                                       * error (close that stream). */
} faar_config;

/*
 * Library-global facts, independent of any resampler. Set out_info->struct_size to
 * sizeof(faar_library_info) before the call; the struct grows only by
 * appending fields. struct_size reports bytes populated. version/copyright are
 * static and library-owned; do not free them.
 */
typedef struct faar_library_info {
    uint32_t       struct_size;       /* set by caller to sizeof(faar_library_info)        */

    const char    *version;           /* library version string   (library-owned)          */
    const char    *copyright;         /* library copyright string (library-owned)          */

    uint32_t       max_channels;      /* highest channel count this build resamples        */
    uint32_t       flags;             /* FAAR_LIB_* bits of this build                     */
} faar_library_info;

/*
 * Decisions the library made for one open resampler; what the caller configured
 * (rates, channels, quality, flags) is not repeated. Set struct_size to
 * sizeof(faar_resampler_info) before faar_resampler_get_info(); it is updated
 * to the bytes populated.
 */
typedef struct faar_resampler_info {
    uint32_t       struct_size;       /* set by caller to sizeof(faar_resampler_info)      */

    uint32_t       decimate_factor;   /* total decimation of the cascade stages ahead of the
                                       * final stage, 1 when the plan has none              */
    uint32_t       delay_frames;      /* filter lookahead in input frames, all stages      */
} faar_resampler_info;

/* Offset just past the named field: the smallest struct size the library accepts. */
#define FAAR_STRUCT_END(type, field) \
    (offsetof(type, field) + sizeof(((type *)0)->field))
#define FAAR_CONFIG_BASELINE FAAR_STRUCT_END(faar_config, max_input_frames)
#define FAAR_LIBRARY_INFO_BASELINE FAAR_STRUCT_END(faar_library_info, flags)
#define FAAR_RESAMPLER_INFO_BASELINE FAAR_STRUCT_END(faar_resampler_info, delay_frames)

/* Opaque resampler handle. */
typedef struct faar_resampler_struct faar_resampler;
/* Immutable filter design, reusable by independent streams. */
typedef struct faar_design_struct faar_design;

/* Zero-initialize *config and fill in library defaults and struct_size. Pass
 * sizeof(*config) as caller_size; never writes past min(caller_size,
 * sizeof(faar_config)). Returns FAAR_ERR_INVALID_ARGUMENT if config is NULL or
 * caller_size is smaller than faar_config's baseline layout. Set the rates and
 * channel count afterwards. */
FAARAPI faar_status faar_config_init(faar_config *config, uint32_t caller_size);

/* Query build-time facts. Set out_info->struct_size before calling; it is updated
 * to the bytes populated. */
FAARAPI faar_status faar_get_library_info(faar_library_info *out_info);

/* Human-readable, static description of a status code. Never returns NULL. */
FAARAPI const char *faar_strerror(faar_status status);

/* Validate the configuration and create a resampler. Reads only the config
 * bytes struct_size covers. *out is set to NULL first and receives the handle
 * on success. */
FAARAPI faar_status faar_resampler_open(const faar_config *config, faar_resampler **out);

/* Optional: share one filter design among many identical streams. A design
 * retains coefficients and immutable setup tables, but no streaming workspace,
 * and fixes the whole configuration, including max_input_frames. Streams opened
 * from it own their mutable storage and retain the design, so the caller may
 * close its design reference before closing streams. A design may be used
 * concurrently to open streams; do not close the same caller reference while
 * another thread is using it. On toolchains without atomics, serialize design
 * and associated stream open/close operations. Process/finalize on independent
 * streams remain concurrent. Ordinary open() needs no design. */
FAARAPI faar_status faar_design_open(const faar_config *config, faar_design **out);
/* Releases the caller's reference and sets *design to NULL; a NULL *design is a no-op. */
FAARAPI faar_status faar_design_close(faar_design **design);
FAARAPI faar_status faar_resampler_open_from_design(faar_design *design,
                                                    faar_resampler **out);

/* Frees *resampler and sets it to NULL. A NULL *resampler is a no-op that
 * returns FAAR_OK; a NULL pointer is invalid. */
FAARAPI faar_status faar_resampler_close(faar_resampler **resampler);

/* Query the resolved settings and filter delay. Set out_info->struct_size to
 * sizeof(faar_resampler_info); smaller baseline layouts are rejected, larger
 * layouts are accepted and only known bytes are written. */
FAARAPI faar_status faar_resampler_get_info(const faar_resampler *resampler,
                                             faar_resampler_info *out_info);

/* Consume in_count input frames (interleaved floats on the FAAC integer scale,
 * +-32768.0) and write up to max_out_count output frames. Counts are frames
 * (samples per channel), not samples. *out_count receives the frames written
 * and is zeroed on error. With in_samples == NULL and in_count == 0 it only
 * drains queued output; call that with nonzero capacity until it makes zero
 * frames before supplying more input when the output buffer is smaller than the
 * result. Input after faar_resampler_finalize() is rejected with
 * FAAR_ERR_INVALID_ARGUMENT. */
FAARAPI faar_status faar_resampler_process(faar_resampler *resampler,
        const float *in_samples, size_t in_count, float *out_samples,
        size_t max_out_count, size_t *out_count);

/* End of stream: write the remaining filter tail. Call until *out_count is zero.
 * The total over a stream is ceil(input_frames * out_rate / in_rate). Unlike
 * libfaad's faad_decoder_flush() this does not discard history. */
FAARAPI faar_status faar_resampler_finalize(faar_resampler *resampler,
        float *out_samples, size_t max_out_count, size_t *out_count);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FAAR_H */
