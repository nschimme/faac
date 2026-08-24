/*
 * FAAC - Freeware Advanced Audio Coder
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <emscripten.h>

#include <faac.h>
#include "mp4write.h"

typedef struct {
    faac_encoder *hEncoder;
    uint32_t max_output_bytes;
    uint32_t frame_samples;
    uint8_t *bitbuf;
    float *interleaved_buf;
    uint32_t interleaved_cap;
    uint32_t samplerate;
    uint32_t channels;
    uint64_t total_samples;
    faac_status last_status;
} faac_wasm_t;

static char g_last_error[256] = "";

static void wasm_set_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_last_error, sizeof(g_last_error), fmt, ap);
    va_end(ap);
}

EMSCRIPTEN_KEEPALIVE
const char *faac_wasm_get_last_error(void) {
    return g_last_error;
}

static bool wasm_configure_params(faac_params *params, int32_t samplerate, int32_t channels,
                                   int32_t bitrate, int32_t quality, int32_t object_type,
                                   int32_t use_tns, int32_t pns_level, int32_t joint_mode,
                                   int32_t cutoff) {
    faac_status st = faac_params_init(params, sizeof(*params));
    if (st != FAAC_OK) {
        wasm_set_error("faac_params_init failed: %s", faac_strerror(st));
        return false;
    }

    params->sample_rate = (uint32_t)samplerate;
    params->num_channels = (uint32_t)channels;
    params->mpeg_version = FAAC_MPEG4;

    if (object_type != -1) params->object_type = (enum faac_object_type)object_type;
    if (use_tns != -1) params->use_tns = (use_tns != 0);
    if (pns_level >= 0) params->pns_level = (int8_t)pns_level;
    if (joint_mode != -1) params->joint_mode = (enum faac_joint_mode)joint_mode;

    params->bandwidth = (cutoff > 0) ? (uint32_t)cutoff : 0;

    if (quality > 0) {
        params->quant_quality = (uint16_t)quality;
        params->bit_rate = 0;
    } else if (bitrate > 0) {
        params->bit_rate = (uint32_t)((bitrate * 1000) / channels);
        params->quant_quality = 0;
    }

    params->output_format = FAAC_STREAM_RAW;
    params->input_format = FAAC_INPUT_FLOAT;
    return true;
}

EMSCRIPTEN_KEEPALIVE
faac_wasm_t *faac_wasm_init(int32_t samplerate, int32_t channels, int32_t bitrate, int32_t quality,
                            int32_t object_type, int32_t use_tns, int32_t pns_level, int32_t joint_mode,
                            int32_t cutoff, double total_samples_double,
                            const char *title, const char *artist, const char *album) {
    g_last_error[0] = '\0';
    faac_wasm_t *ctx = malloc(sizeof(faac_wasm_t));
    if (!ctx) {
        wasm_set_error("Out of memory allocating context");
        return NULL;
    }
    memset(ctx, 0, sizeof(faac_wasm_t));

    faac_params params;
    if (!wasm_configure_params(&params, samplerate, channels, bitrate, quality,
                                object_type, use_tns, pns_level, joint_mode, cutoff)) {
        free(ctx);
        return NULL;
    }

    faac_status st = faac_encoder_open(&params, &ctx->hEncoder);
    if (st != FAAC_OK) {
        wasm_set_error("faac_encoder_open failed: %s", faac_strerror(st));
        free(ctx);
        return NULL;
    }

    faac_encoder_info info = { .struct_size = sizeof(info) };
    faac_encoder_get_info(ctx->hEncoder, &info);

    ctx->max_output_bytes = info.max_output_bytes;
    ctx->frame_samples = info.frame_samples;
    ctx->bitbuf = malloc(info.max_output_bytes);
    if (!ctx->bitbuf) {
        wasm_set_error("Out of memory allocating bitstream buffer");
        faac_encoder_close(&ctx->hEncoder);
        free(ctx);
        return NULL;
    }

    ctx->samplerate = (uint32_t)samplerate;
    ctx->channels = (uint32_t)channels;
    ctx->total_samples = (uint64_t)total_samples_double;

    if (mp4_open("output.bin", true) != 0) {
        wasm_set_error("mp4_open failed to create output file");
        faac_encoder_close(&ctx->hEncoder);
        free(ctx->bitbuf);
        free(ctx);
        return NULL;
    }

    mp4_set_format((uint32_t)samplerate, (uint32_t)channels, 16);
    const uint8_t *asc = NULL;
    uint32_t asc_len = 0;
    if (faac_encoder_asc(ctx->hEncoder, &asc, &asc_len) == FAAC_OK && asc_len > 0) {
        mp4_set_decoder_config(asc, asc_len);
    }

    if (title && title[0] != '\0') mp4_set_tag(MP4TAG_TITLE, title);
    if (artist && artist[0] != '\0') mp4_set_tag(MP4TAG_ARTIST, artist);
    if (album && album[0] != '\0') mp4_set_tag(MP4TAG_ALBUM, album);

    return ctx;
}

EMSCRIPTEN_KEEPALIVE
int32_t faac_wasm_encode(faac_wasm_t *ctx, float *planar_ptrs[], int32_t num_frames) {
    if (!ctx || !planar_ptrs || num_frames <= 0) {
        if (!ctx) wasm_set_error("Invalid WASM encoder context");
        return -1;
    }

    uint32_t total_samples = (uint32_t)num_frames * ctx->channels;
    if (total_samples > ctx->interleaved_cap) {
        float *tmp = realloc(ctx->interleaved_buf, total_samples * sizeof(float));
        if (!tmp) {
            wasm_set_error("Out of memory allocating planar interleave buffer");
            return -1;
        }
        ctx->interleaved_buf = tmp;
        ctx->interleaved_cap = total_samples;
    }

    /* Interleave planar channel vectors directly in C for SIMD/auto-vectorization */
    uint32_t chs = ctx->channels;
    for (int32_t i = 0; i < num_frames; i++) {
        for (uint32_t c = 0; c < chs; c++) {
            ctx->interleaved_buf[i * chs + c] = planar_ptrs[c][i];
        }
    }

    uint32_t bytes_written = 0;
    faac_status st = faac_encoder_encode(ctx->hEncoder, ctx->interleaved_buf, total_samples,
                                         ctx->bitbuf, ctx->max_output_bytes, &bytes_written);
    ctx->last_status = st;

    if (st != FAAC_OK) {
        wasm_set_error("faac_encoder_encode failed: %s", faac_strerror(st));
        return -1;
    }

    if (bytes_written > 0) {
        uint32_t frame_samples = (uint32_t)num_frames;
        if (frame_samples > ctx->frame_samples) frame_samples = ctx->frame_samples;
        if (mp4_write_frame(ctx->bitbuf, bytes_written, frame_samples) != 0) {
            wasm_set_error("mp4_write_frame failed");
            return -1;
        }
    }

    return (int32_t)bytes_written;
}

static void wasm_flush_and_finalize(faac_wasm_t *ctx) {
    faac_status st;
    uint32_t bytes_written = 0;
    do {
        st = faac_encoder_encode(ctx->hEncoder, NULL, 0, ctx->bitbuf, ctx->max_output_bytes, &bytes_written);
        if (st == FAAC_OK && bytes_written > 0) {
            mp4_write_frame(ctx->bitbuf, bytes_written, ctx->frame_samples);
        }
    } while (st == FAAC_OK && bytes_written > 0);

    faac_encoder_info info = { .struct_size = sizeof(info) };
    if (faac_encoder_get_info(ctx->hEncoder, &info) == FAAC_OK) {
        uint32_t priming = info.encoder_delay;
        uint64_t total_output_samples = mp4_sample_count();
        uint64_t padding = 0;
        if (total_output_samples > (uint64_t)priming + ctx->total_samples)
            padding = total_output_samples - (uint64_t)priming - ctx->total_samples;
        mp4_set_gapless(priming, (uint32_t)padding, ctx->total_samples);
    }

    faac_library_info libinfo = { .struct_size = sizeof(libinfo) };
    faac_get_library_info(&libinfo);
    if (libinfo.version) {
        char ver_buf[128];
        snprintf(ver_buf, sizeof(ver_buf), "FAAC %s", libinfo.version);
        mp4_set_encoder(ver_buf);
    }

    mp4_finish();
    mp4_close();
}

EMSCRIPTEN_KEEPALIVE
void faac_wasm_close(faac_wasm_t *ctx) {
    if (!ctx) return;

    wasm_flush_and_finalize(ctx);

    faac_encoder_close(&ctx->hEncoder);
    if (ctx->interleaved_buf) free(ctx->interleaved_buf);
    free(ctx->bitbuf);
    free(ctx);
}
