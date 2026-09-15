/*
 * FAAD - Freeware Advanced Audio Decoder
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

#ifndef FAAD_H
#define FAAD_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define FAAD_VERSION_MAJOR 2
#define FAAD_VERSION_MINOR 0
#define FAAD_VERSION_PATCH 0
#define FAAD_VERSION_HEX \
    ((FAAD_VERSION_MAJOR << 16) | (FAAD_VERSION_MINOR << 8) | FAAD_VERSION_PATCH)

/* Export/visibility marker. */
#ifndef FAADAPI
# if defined(_WIN32)
#  define FAADAPI __declspec(dllexport)
# elif defined(__GNUC__) && (__GNUC__ >= 4)
#  define FAADAPI __attribute__((visibility("default")))
# else
#  define FAADAPI
# endif
#endif

/* Opaque decoder handle */
typedef struct faad_decoder faad_decoder;

typedef enum faad_status {
    FAAD_OK                   = 0,
    FAAD_ERR_INVALID_ARGUMENT = -1,  /* NULL pointer or bad arguments */
    FAAD_ERR_UNSUPPORTED      = -2,  /* Unsupported object type or stream format */
    FAAD_ERR_NO_MEMORY        = -3,  /* Allocation failed */
    FAAD_ERR_OUTPUT_TOO_SMALL = -4,  /* Output buffer too small */
    FAAD_ERR_NEED_MORE_DATA   = -5,  /* Input buffer insufficient for full frame */
    FAAD_ERR_DECODE_FAILED    = -6,  /* Bitstream corruption or decoding error */
    FAAD_STATUS_MAX           = 0x7fffffff
} faad_status;

enum faad_object_type {
    FAAD_OBJ_NULL      = 0,
    FAAD_OBJ_LOW       = 2,          /* AAC-LC */
    FAAD_OBJ_HE_AAC_V1 = 5,          /* HE-AAC v1 (AAC-LC + SBR) */
    FAAD_OBJ_MAX       = 0x7fffffff
};

enum faad_stream_format {
    FAAD_STREAM_RAW  = 0,            /* Raw AAC frames (requires out-of-band ASC) */
    FAAD_STREAM_ADTS = 1,            /* Self-framing ADTS bitstream */
    FAAD_STREAM_MAX  = 0x7fffffff
};

enum faad_output_format {
    FAAD_OUTPUT_16BIT = 1,           /* Interleaved int16 PCM */
    FAAD_OUTPUT_FLOAT = 4,           /* Interleaved 32-bit float PCM */
    FAAD_OUTPUT_MAX   = 0x7fffffff
};

typedef struct faad_params {
    uint32_t                struct_size;   /* set by faad_params_init() */
    enum faad_stream_format stream_format; /* RAW or ADTS stream format */
    enum faad_output_format output_format; /* 16-bit int or float output */
    bool                    downmix_stereo;/* Downmix multi-channel to stereo */
} faad_params;

typedef struct faad_decoder_info {
    uint32_t                struct_size;   /* set by caller */
    uint32_t                sample_rate;   /* Effective output sample rate in Hz */
    uint32_t                num_channels;  /* Effective channel count (1..8) */
    uint32_t                frame_samples; /* Samples per channel per frame (1024 or 2048) */
    enum faad_object_type   object_type;   /* Resolved object type (LC or HE-AAC v1) */
    uint32_t                max_output_bytes; /* Upper bound on PCM bytes per frame */
} faad_decoder_info;

FAADAPI faad_status faad_params_init(faad_params *p, uint32_t caller_size);

FAADAPI faad_status faad_decoder_open(const faad_params *p,
                                       const uint8_t *asc_buf, uint32_t asc_len,
                                       faad_decoder **out);

FAADAPI faad_status faad_decoder_close(faad_decoder **dec);

FAADAPI faad_status faad_decoder_get_info(faad_decoder *dec, faad_decoder_info *out);

/*
 * Decode one frame of AAC.
 * `in`: input AAC bitstream payload buffer.
 * `in_bytes`: size of `in` payload.
 * `bytes_consumed`: output parameter receiving bytes consumed from `in`.
 * `out`: output PCM buffer.
 * `out_cap`: capacity of `out` buffer in bytes.
 * `bytes_written`: output parameter receiving written PCM bytes.
 */
FAADAPI faad_status faad_decoder_decode(faad_decoder *dec,
                                        const uint8_t *in, uint32_t in_bytes,
                                        uint32_t *bytes_consumed,
                                        void *out, uint32_t out_cap,
                                        uint32_t *bytes_written);

FAADAPI const char *faad_strerror(faad_status status);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FAAD_H */
