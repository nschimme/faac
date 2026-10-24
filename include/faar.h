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

/**
 * @file faar.h
 * @brief Public API for libfaar CPU audio resampling
 *
 * API and ABI notes:
 *   - Initialize faar_config with faar_config_init() and open an opaque handle
 *     with faar_resampler_open(). Fallible calls return faar_status; process
 *     and flush write frame counts through output parameters.
 *   - Versioned public structures begin with struct_size and grow only by
 *     appending fields at or after the previous sizeof(struct), never into
 *     compiler padding. Explicit reserved fields must be zero on input and are
 *     zero on output. Integer parameters use fixed-width types, and enum
 *     sentinels pin enum storage to at least 32 bits, including -fshort-enums.
 *   - FAAR_VERSION_MAJOR tracks the libfaar SONAME, not the project release.
 *   - ABI compatibility is guaranteed within a major SONAME on a given
 *     platform ABI. size_t and pointer types follow that platform's ABI.
 *   - A handle belongs to one thread at a time. Independent handles may be
 *     used concurrently.
 *   - Export definitions follow the FAAC/FAAD Windows DLL and ELF conventions.
 */

#ifndef FAAR_H
#define FAAR_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

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

/**
 * @brief Resampler quality presets
 */
typedef enum faar_quality_t {
    FAAR_QUALITY_FAST = 0, /**< Lower-cost filter; narrower passband */
    FAAR_QUALITY_GOOD = 1, /**< Default profile: wide passband and strong rejection */
    FAAR_QUALITY_HIGH = 2, /**< Stronger stopband rejection; higher CPU and setup memory */
    FAAR_QUALITY_MAX  = 0x7fffffff
} faar_quality_t;

/**
 * @brief Dither modes for bit-depth truncation / quantization
 */
typedef enum faar_dither_t {
    FAAR_DITHER_OFF    = 0, /**< No dithering (direct truncation / rounding) */
    FAAR_DITHER_TPDF   = 1, /**< Triangular Probability Density Function dither */
    FAAR_DITHER_SHAPED = 2, /**< TPDF dither with high-pass noise shaping error feedback */
    FAAR_DITHER_MAX    = 0x7fffffff
} faar_dither_t;

/** Samples assumed outside an independently supplied stream. */
typedef enum faar_boundary_t {
    FAAR_BOUNDARY_ZERO = 0, /**< Silence outside the stream */
    FAAR_BOUNDARY_HOLD = 1, /**< Continue the first/last sample (default) */
    FAAR_BOUNDARY_MAX = 0x7fffffff
} faar_boundary_t;

/* Status codes for the versioned resampler API. */
typedef enum faar_status {
    FAAR_OK = 0,
    FAAR_ERR_INVALID_ARGUMENT = -1,
    FAAR_ERR_UNSUPPORTED = -2,
    FAAR_ERR_NO_MEMORY = -3,
    FAAR_ERR_PROCESSING = -4,
    FAAR_STATUS_MAX = 0x7fffffff
} faar_status;

#define FAAR_QUALITY_MASK_FAST (1u << FAAR_QUALITY_FAST)
#define FAAR_QUALITY_MASK_GOOD (1u << FAAR_QUALITY_GOOD)
#define FAAR_QUALITY_MASK_HIGH (1u << FAAR_QUALITY_HIGH)

/* Versioned configuration for faar_resampler_open(). Initialize with
 * faar_config_init() before setting fields. decimate_factor is zero for
 * automatic planning; values of two or greater request an explicit cascade.
 * New releases append fields only; reserved and reserved1 must be zero or
 * faar_resampler_open() fails with FAAR_ERR_INVALID_ARGUMENT. */
typedef struct faar_config {
    uint32_t struct_size;
    uint32_t in_rate;
    uint32_t out_rate;
    uint16_t channels;
    uint16_t reserved;
    faar_quality_t quality;
    faar_boundary_t boundary;
    faar_dither_t dither;
    uint8_t dither_bits; /* 16 or 24 when dither is not OFF; ignored otherwise */
    uint8_t reserved1[3];
    uint32_t decimate_factor;
} faar_config;

/* Global capabilities for this build. Set struct_size before querying. */
typedef struct faar_library_info {
    uint32_t struct_size;
    uint32_t max_channels;
    uint32_t quality_mask;
    uint32_t reserved0;
    const char *version;
    const char *copyright;
} faar_library_info;

/* Resolved settings for one resampler. Set struct_size before querying. */
typedef struct faar_resampler_info {
    uint32_t struct_size;
    uint32_t in_rate;
    uint32_t out_rate;
    uint32_t channels;
    faar_quality_t quality;
    faar_boundary_t boundary;
    faar_dither_t dither;
    uint8_t dither_bits;
    uint8_t reserved[3];
    size_t delay;
} faar_resampler_info;

#define FAAR_STRUCT_END(type, field) \
    ((uint32_t)(offsetof(type, field) + sizeof(((type *)0)->field)))
#define FAAR_CONFIG_BASELINE FAAR_STRUCT_END(faar_config, decimate_factor)
#define FAAR_LIBRARY_INFO_BASELINE FAAR_STRUCT_END(faar_library_info, copyright)
#define FAAR_RESAMPLER_INFO_BASELINE FAAR_STRUCT_END(faar_resampler_info, delay)

/**
 * @brief Opaque resampler handle type
 */
typedef struct faar_resampler_struct faar_resampler;

FAARAPI faar_status faar_config_init(faar_config *config, uint32_t caller_size);
FAARAPI faar_status faar_get_library_info(faar_library_info *out);
FAARAPI const char *faar_strerror(faar_status status);
FAARAPI faar_status faar_resampler_open(const faar_config *config, faar_resampler **out);
/* Reserve streaming storage before any input or flush. Process calls may
 * supply at most max_input_frames; drain with NULL/0 and nonzero output
 * capacity until no frames are made before feeding more input.
 * Prepared process/flush never allocate or free heap storage.
 * Exceeding reserved queue capacity is a processing error;
 * close that stream. Preparation can be repeated before input, and a failed
 * preparation leaves the stream usable. FFT block costs are unchanged. */
FAARAPI faar_status faar_resampler_prepare(faar_resampler *resampler,
                                          size_t max_input_frames);
/* Frees *resampler and sets it to NULL. A NULL *resampler is a no-op. */
FAARAPI faar_status faar_resampler_close(faar_resampler **resampler);
FAARAPI faar_status faar_resampler_get_info(const faar_resampler *resampler,
                                             faar_resampler_info *out);
FAARAPI faar_status faar_resampler_process(faar_resampler *resampler,
        const float *in_samples, size_t in_count, float *out_samples,
        size_t max_out_count, size_t *out_count);
FAARAPI faar_status faar_resampler_flush(faar_resampler *resampler,
        float *out_samples, size_t max_out_count, size_t *out_count);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FAAR_H */
