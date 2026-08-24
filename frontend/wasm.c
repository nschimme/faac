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
#include <emscripten.h>

#include <faac.h>
#include "mp4write.h"

typedef struct {
    faac_encoder *hEncoder;
    uint32_t max_output_bytes;
    uint32_t frame_samples;
    uint8_t *bitbuf;
    uint32_t samplerate;
    uint32_t channels;
} faac_wasm_t;

EMSCRIPTEN_KEEPALIVE
faac_wasm_t *faac_wasm_init(int32_t samplerate, int32_t channels, int32_t bitrate, int32_t quality,
                            int32_t object_type, int32_t use_tns, int32_t pns_level, int32_t joint_mode,
                            int32_t cutoff) {
    faac_wasm_t *ctx = malloc(sizeof(faac_wasm_t));
    if (!ctx) return NULL;
    memset(ctx, 0, sizeof(faac_wasm_t));

    faac_params params;
    if (faac_params_init(&params, sizeof(params)) != FAAC_OK) {
        free(ctx);
        return NULL;
    }

    params.sample_rate = (uint32_t)samplerate;
    params.num_channels = (uint32_t)channels;
    params.mpeg_version = FAAC_MPEG4;

    if (object_type != -1) params.object_type = (enum faac_object_type)object_type;
    if (use_tns != -1) params.use_tns = (use_tns != 0);
    if (pns_level >= 0) params.pns_level = (int8_t)pns_level;
    if (joint_mode != -1) params.joint_mode = (enum faac_joint_mode)joint_mode;

    params.bandwidth = (cutoff > 0) ? (uint32_t)cutoff : 0;

    if (quality > 0) {
        params.quant_quality = (uint16_t)quality;
        params.bit_rate = 0;
    } else if (bitrate > 0) {
        params.bit_rate = (uint32_t)((bitrate * 1000) / channels);
        params.quant_quality = 0;
    }

    params.output_format = FAAC_STREAM_RAW;
    params.input_format = FAAC_INPUT_FLOAT;

    if (faac_encoder_open(&params, &ctx->hEncoder) != FAAC_OK) {
        free(ctx);
        return NULL;
    }

    faac_encoder_info info = { .struct_size = sizeof(info) };
    faac_encoder_get_info(ctx->hEncoder, &info);

    ctx->max_output_bytes = info.max_output_bytes;
    ctx->frame_samples = info.frame_samples;
    ctx->bitbuf = malloc(info.max_output_bytes);
    if (!ctx->bitbuf) {
        faac_encoder_close(&ctx->hEncoder);
        free(ctx);
        return NULL;
    }

    ctx->samplerate = (uint32_t)samplerate;
    ctx->channels = (uint32_t)channels;

    if (mp4_open("output.bin", true) != 0) {
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

    return ctx;
}

EMSCRIPTEN_KEEPALIVE
int32_t faac_wasm_encode(faac_wasm_t *ctx, float *pcm_data, int32_t samples_read) {
    if (!ctx) return -1;

    uint32_t bytes_written = 0;
    faac_status st = faac_encoder_encode(ctx->hEncoder, pcm_data, (uint32_t)samples_read,
                                         ctx->bitbuf, ctx->max_output_bytes, &bytes_written);

    if (st == FAAC_OK && bytes_written > 0) {
        uint32_t frame_samples = (uint32_t)samples_read / ctx->channels;
        if (frame_samples > ctx->frame_samples) frame_samples = ctx->frame_samples;
        mp4_write_frame(ctx->bitbuf, bytes_written, frame_samples);
    }

    return (st == FAAC_OK) ? (int32_t)bytes_written : -1;
}

EMSCRIPTEN_KEEPALIVE
void faac_wasm_close(faac_wasm_t *ctx) {
    if (!ctx) return;

    // Flush encoder
    faac_status st;
    uint32_t bytes_written = 0;
    do {
        st = faac_encoder_encode(ctx->hEncoder, NULL, 0, ctx->bitbuf, ctx->max_output_bytes, &bytes_written);
        if (st == FAAC_OK && bytes_written > 0) {
            mp4_write_frame(ctx->bitbuf, bytes_written, ctx->frame_samples);
        }
    } while (st == FAAC_OK && bytes_written > 0);

    faac_library_info libinfo = { .struct_size = sizeof(libinfo) };
    faac_get_library_info(&libinfo);
    if (libinfo.version) {
        char ver_buf[128];
        snprintf(ver_buf, sizeof(ver_buf), "FAAC %s", libinfo.version);
        mp4_set_encoder(ver_buf);
    }
    mp4_finish();
    mp4_close();

    faac_encoder_close(&ctx->hEncoder);
    free(ctx->bitbuf);
    free(ctx);
}
