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

/*
 * Cross-library harness for the program config element (PCE): encode six or
 * eight channels with libfaac, then decode the result with libfaad and check
 * the signalled channel count and WAVE mask. This is the only path where an
 * AAC stream describes its layout with a PCE instead of channelConfiguration.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "faac.h"
#include "faad3.h"
#include "asc_codec.h"

#define MAX_FRAMES 8
#define MAX_OUT (1u << 20)

typedef struct {
    uint8_t *data;
    size_t size;
    size_t cap;
} byte_buf;

static int push(byte_buf *b, const uint8_t *src, size_t n)
{
    if (b->size + n > MAX_OUT)
        return 0;
    if (b->size + n > b->cap) {
        size_t cap = (b->size + n) * 2 + 4096;
        uint8_t *p = realloc(b->data, cap);
        if (!p)
            return 0;
        b->data = p;
        b->cap = cap;
    }
    memcpy(b->data + b->size, src, n);
    b->size += n;
    return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 4)
        return 0;

    uint8_t mode = data[0];
    uint32_t channels = (mode & 1) ? 8 : 6;
    uint32_t rate = (mode & 2) ? 44100 : 48000;
    int raw = (mode & 4) != 0;
    uint32_t frames = 1 + (data[1] % MAX_FRAMES);
    const uint8_t *pcm_seed = data + 2;
    size_t seed_len = size - 2;

    faac_params p;
    if (faac_params_init(&p, sizeof(p)) != FAAC_OK)
        __builtin_trap();
    p.sample_rate = rate;
    p.channels = channels;
    p.object_type = FAAC_OBJ_LC;
    p.stereo_mode = FAAC_STEREO_AUTO;
    p.stream_format = raw ? FAAC_STREAM_RAW : FAAC_STREAM_ADTS;
    p.input_format = FAAC_INPUT_16BIT;
    p.rate_control = FAAC_RC_VBR;
    p.quality = 100;

    faac_encoder *enc = NULL;
    if (faac_encoder_open(&p, &enc) != FAAC_OK || !enc)
        return 0;

    faac_encoder_info info;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    if (faac_encoder_get_info(enc, &info) != FAAC_OK || !info.frame_samples ||
        info.max_output_bytes > MAX_OUT)
        __builtin_trap();

    /* Six channels use channelConfiguration 6; eight must use a 7.1 PCE. */
    if (channels == 8) {
        AscInfo ai;
        asc_codec_parse(info.asc, info.asc_size, &ai);
        if (ai.channel_config != 0 || !ai.pce.wave71 ||
            ai.pce.channels != 8 || ai.num_channels != 8)
            __builtin_trap();
    } else if (info.asc_size && ((info.asc[1] >> 3) & 0x0f) != 6) {
        __builtin_trap();
    }

    uint16_t *pcm = malloc((size_t)info.frame_samples * channels * sizeof(uint16_t));
    uint8_t *out = malloc(info.max_output_bytes);
    if (!pcm || !out) {
        free(pcm);
        free(out);
        faac_encoder_close(&enc);
        return 0;
    }

    byte_buf stream = { 0 };
    uint32_t raw_sizes[MAX_FRAMES * 8];
    uint32_t raw_count = 0;
    int ok = 1;

    for (uint32_t f = 0; f < frames && ok; f++) {
        uint32_t samples = info.frame_samples * channels;
        for (uint32_t i = 0; i < samples; i++) {
            uint8_t lo = pcm_seed[(i + f) % seed_len];
            uint8_t hi = pcm_seed[(i * 2 + f + 1) % seed_len];
            pcm[i] = (uint16_t)(lo | (hi << 8));
        }
        uint32_t written = 0;
        if (faac_encoder_encode(enc, pcm, samples, out, info.max_output_bytes, &written) != FAAC_OK) {
            ok = 0;
            break;
        }
        if (written) {
            if (raw) {
                if (raw_count < sizeof(raw_sizes) / sizeof(raw_sizes[0]))
                    raw_sizes[raw_count++] = written;
            }
            if (!push(&stream, out, written)) {
                ok = 0;
                break;
            }
        }
    }
    for (int guard = 0; guard < 8 && ok; guard++) {
        uint32_t written = 0;
        if (faac_encoder_encode(enc, NULL, 0, out, info.max_output_bytes, &written) != FAAC_OK) {
            ok = 0;
            break;
        }
        if (!written)
            break;
        if (raw && raw_count < sizeof(raw_sizes) / sizeof(raw_sizes[0]))
            raw_sizes[raw_count++] = written;
        if (!push(&stream, out, written)) {
            ok = 0;
            break;
        }
    }
    free(pcm);
    free(out);
    faac_encoder_close(&enc);

    if (!ok || !stream.size) {
        free(stream.data);
        return 0;
    }

    faad_config cfg;
    if (faad_config_init(&cfg, sizeof(cfg)) != FAAD_OK)
        __builtin_trap();
    cfg.stream_format = raw ? FAAD_STREAM_RAW : FAAD_STREAM_ADTS;
    cfg.output_format = FAAD_OUTPUT_16BIT;

    faad_decoder *dec = NULL;
    if (faad_decoder_open(&cfg, raw ? info.asc : NULL, raw ? info.asc_size : 0, &dec) != FAAD_OK || !dec) {
        free(stream.data);
        return 0;
    }

    faad_stream_info si;
    memset(&si, 0, sizeof(si));
    si.struct_size = sizeof(si);
    if (faad_decoder_get_info(dec, &si) != FAAD_OK)
        __builtin_trap();
    uint8_t *pcm_out = malloc(si.max_output_bytes ? si.max_output_bytes : 4096);
    if (!pcm_out) {
        faad_decoder_close(&dec);
        free(stream.data);
        return 0;
    }

    size_t pos = 0;
    uint32_t decoded = 0, seen = 0;
    while (pos < stream.size && decoded < 256) {
        uint32_t frame_len;
        if (raw) {
            if (seen >= raw_count)
                break;
            frame_len = raw_sizes[seen++];
            if (pos + frame_len > stream.size)
                break;
        } else {
            frame_len = (uint32_t)(stream.size - pos);
        }

        uint32_t consumed = 0, written = 0, flags = 0;
        faad_status st = faad_decode_frame(dec, stream.data + pos, frame_len, &consumed,
                                           pcm_out, si.max_output_bytes, &written, &flags);
        if (st == FAAD_ERR_NEED_MORE_DATA)
            break;
        if (st != FAAD_OK && st != FAAD_ERR_SYNC_LOST && st != FAAD_ERR_DECODE_FAILED) {
            ok = 0;
            break;
        }
        if (consumed == 0)
            break;
        pos += consumed;
        if (st == FAAD_OK)
            decoded++;
        if (flags & FAAD_FRAME_FORMAT_CHANGED) {
            si.struct_size = sizeof(si);
            (void)faad_decoder_get_info(dec, &si);
        }
        if (flags & ~(FAAD_FRAME_FORMAT_CHANGED | FAAD_FRAME_SBR | FAAD_FRAME_PS |
                      FAAD_FRAME_CONCEALED | FAAD_FRAME_DEGRADED | FAAD_FRAME_DRC |
                      FAAD_FRAME_DRC_INFO))
            __builtin_trap();
    }

    /* The encoder's own stream must decode to the layout it signalled. */
    if (ok && decoded && si.channels != channels)
        __builtin_trap();
    if (ok && decoded && channels == 8 && si.channel_mask != 0x63f)
        __builtin_trap();
    if (ok && decoded && channels == 6 && si.channel_mask != 0x3f)
        __builtin_trap();

    free(pcm_out);
    faad_decoder_close(&dec);
    free(stream.data);
    return 0;
}
