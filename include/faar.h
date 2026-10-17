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
 * @brief Public API for libfaar - Polyphase Farrow-Interpolated Kaiser-Sinc Audio Resampler
 *
 * Design summary:
 *   - Parameters are supplied up front to faar_create() or configured via faar_set_dither().
 *   - Fixed-width integer types and width-pinned enums keep the ABI identical
 *     across 64-bit and 32-bit targets.
 *   - Export symbol definitions follow FAACAPI conventions for Windows DLL and ELF hidden visibility.
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
#define FAAR_VERSION_HEX \
    ((FAAR_VERSION_MAJOR << 16) | (FAAR_VERSION_MINOR << 8) | FAAR_VERSION_PATCH)

/* Windows exports from DLL build only (FAAR_BUILDING). No dllimport. */
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
    FAAR_QUALITY_FAST = 0, /**< Fast mode: 127-tap filter, 28 dB Kaiser beta (~100 dB stopband rejection) */
    FAAR_QUALITY_GOOD = 1, /**< Good mode: 255-tap filter, 32 dB Kaiser beta (~120 dB stopband rejection) */
    FAAR_QUALITY_HIGH = 2, /**< High mode: 511-tap filter, 38 dB Kaiser beta (>140 dB stopband rejection) */
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

/**
 * @brief Opaque resampler handle type
 */
typedef struct faar_resampler_struct faar_resampler_struct;
typedef faar_resampler_struct *faar_handle_t;

/**
 * @brief Create a new polyphase Farrow resampler instance.
 *
 * @param in_rate Input sampling rate in Hz (e.g. 48000, 96000, 192000)
 * @param out_rate Target output sampling rate in Hz (e.g. 44100, 24000, 16000)
 * @param channels Number of audio channels (1 to 8 channels supported)
 * @param quality Resampling quality preset
 * @return Opaque handle to resampler instance, or NULL on allocation failure/invalid parameters.
 */
FAARAPI faar_handle_t faar_create(uint32_t in_rate, uint32_t out_rate, uint16_t channels, faar_quality_t quality);

/**
 * @brief Set dithering and noise shaping mode on a resampler instance.
 *
 * @param handle Resampler instance handle
 * @param dither Dither mode (OFF, TPDF, SHAPED)
 * @param target_bits Target integer bit depth (16 or 24)
 * @return 0 on success, -1 on invalid parameter/handle.
 */
FAARAPI int faar_set_dither(faar_handle_t handle, faar_dither_t dither, uint8_t target_bits);

/**
 * @brief Get the filter group delay in input samples.
 *
 * @param handle Resampler instance handle
 * @return Group delay in frames (samples per channel)
 */
FAARAPI size_t faar_get_delay(faar_handle_t handle);

/**
 * @brief Process PCM float audio samples through the resampler.
 *
 * Inputs and outputs are interleaved float audio samples (-32768.0 to +32768.0 scale).
 *
 * @param handle Resampler instance handle
 * @param in_samples Pointer to input interleaved float sample array (can be NULL if in_count is 0)
 * @param in_count Number of input audio frames (samples per channel)
 * @param out_samples Pointer to output interleaved float buffer
 * @param max_out_count Maximum capacity of output buffer in audio frames (samples per channel)
 * @return Number of output audio frames written to out_samples
 */
FAARAPI size_t faar_process(faar_handle_t handle, const float *in_samples, size_t in_count,
                           float *out_samples, size_t max_out_count);

/**
 * @brief Flush remaining history samples in the filter pipeline at stream end.
 *
 * @param handle Resampler instance handle
 * @param out_samples Pointer to output interleaved float buffer
 * @param max_out_count Maximum capacity of output buffer in audio frames
 * @return Number of flushed audio frames written to out_samples
 */
FAARAPI size_t faar_flush(faar_handle_t handle, float *out_samples, size_t max_out_count);

/**
 * @brief Destroy a resampler instance and release all allocated resources.
 *
 * @param handle Resampler instance handle to destroy
 */
FAARAPI void faar_destroy(faar_handle_t handle);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FAAR_H */
