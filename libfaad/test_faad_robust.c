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

/* Encodes a synthetic signal with libfaac, damages the stream with a fixed
 * PRNG (dropped frames, flipped bits, truncated tail), then feeds it to the
 * decoder the way a frontend does. The decoder must consume every byte,
 * advance on every call, emit at most one frame per call, and never exceed
 * the output the intact stream would produce. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "faac.h"
#include "faad.h"
#include "endian.h"

#define RATE     44100
#ifndef FAAD_TEST_SECONDS
#define FAAD_TEST_SECONDS 4
#endif
#define SECONDS  FAAD_TEST_SECONDS
#define FRAME    1024

static uint32_t rng_state = 0x2545F491u;
static uint32_t rng(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5; return rng_state; }

static int fail(const char *msg) { fprintf(stderr, "test_faad_robust: %s\n", msg); return 1; }

/* Encode to ADTS; returns the stream length or 0. */
static uint32_t encode(enum faac_object_type obj, uint32_t channels, uint8_t *out, uint32_t cap)
{
    faac_params p;
    if (faac_params_init(&p, sizeof(p)) != FAAC_OK) return 0;
    p.sample_rate = RATE;
    p.num_channels = channels;
    p.use_lfe = channels == 6;
    p.object_type = obj;
    p.bit_rate = 48000;
    p.output_format = FAAC_STREAM_ADTS;
    p.input_format = FAAC_INPUT_16BIT;
    faac_encoder *enc = NULL;
    if (faac_encoder_open(&p, &enc) != FAAC_OK) return 0;

    uint32_t total = 0;
    int16_t pcm[FRAME * 8];
    double phase = 0.0;
    for (int f = 0; f < RATE * SECONDS / FRAME; f++) {
        for (int i = 0; i < FRAME; i++) {
            /* sweep with a burst every half second, so all block types occur */
            phase += 2.0 * M_PI * (200.0 + 6000.0 * ((f * FRAME + i) % RATE) / RATE) / RATE;
            double v = 8000.0 * sin(phase);
            if (((f * FRAME + i) % (RATE / 2)) < 64) v += 12000.0 * ((rng() & 1) ? 1 : -1);
            for (uint32_t c = 0; c < channels; c++) pcm[channels * i + c] = (int16_t)(v / (c + 1));
        }
        uint32_t n = 0;
        if (faac_encoder_encode(enc, pcm, FRAME * channels, out + total, cap - total, &n) != FAAC_OK) break;
        total += n;
    }
    for (;;) {
        uint32_t n = 0;
        if (faac_encoder_encode(enc, NULL, 0, out + total, cap - total, &n) != FAAC_OK || n == 0) break;
        total += n;
    }
    faac_encoder_close(&enc);
    return total;
}

/* Compare real audio and subsequent history after rejected, safely retryable
 * calls. Also compare a flushed decoder with a fresh decoder at a seek point. */
static int test_retry_and_flush(const uint8_t *stream, uint32_t channels,
                               enum faad_output_format format,
                               enum faad_downmix_mode downmix, bool raw)
{
    faad_config cfg;
    faad_config_init(&cfg, sizeof(cfg));
    cfg.stream_format = raw ? FAAD_STREAM_RAW : FAAD_STREAM_ADTS;
    cfg.output_format = format;
    cfg.downmix_mode = downmix;
    uint32_t sr_index = (stream[2] >> 2) & 15;
    uint16_t asc_bits = (uint16_t)((2u << 11) | (sr_index << 7) | (channels << 3));
    uint8_t asc[] = { (uint8_t)(asc_bits >> 8), (uint8_t)asc_bits };
    faad_decoder *retry, *control;
    if (faad_decoder_open(&cfg, raw ? asc : NULL, raw ? sizeof(asc) : 0, &retry) != FAAD_OK
        || faad_decoder_open(&cfg, raw ? asc : NULL, raw ? sizeof(asc) : 0, &control) != FAAD_OK)
        return fail("retry decoder open");
    faad_stream_info si = { .struct_size = sizeof(si) };
    faad_decoder_get_info(retry, &si);
    uint32_t cap = si.max_output_bytes;
    void *a = malloc(cap), *b = malloc(cap);
    if (!a || !b) return fail("retry PCM allocation");
    uint32_t pos = 0;
    for (int f = 0; f < 8; f++) {
        uint32_t packet = ((stream[pos + 3] & 3) << 11) | (stream[pos + 4] << 3) | (stream[pos + 5] >> 5);
        uint32_t header = raw ? 7 : 0;
        const uint8_t *in = stream + pos + header;
        uint32_t len = packet - header, used = 123, written = 123, used_b, written_b;
        uint32_t fi = UINT32_MAX, fb;
        if (faad_decode_frame(retry, in, len, &used, a, cap - 1, &written, &fi) != FAAD_ERR_OUTPUT_TOO_SMALL
            || used || written || fi) return fail("capacity retry contract");
        if (!raw && (faad_decode_frame(retry, in, len - 1, &used, a, cap, &written, &fi) != FAAD_ERR_NEED_MORE_DATA
            || used || written || fi)) return fail("input retry contract");
        if (faad_decode_frame(retry, in, len, &used, a, cap, &written, &fi) != FAAD_OK
            || faad_decode_frame(control, in, len, &used_b, b, cap, &written_b, &fb) != FAAD_OK)
            return fail("retry decode");
        if (used != used_b || written != written_b || memcmp(a, b, written)
            || fi != fb) return fail("retry changed PCM or metadata/history");
        if ((fi & FAAD_FRAME_CONCEALED) || (fi & FAAD_FRAME_DEGRADED)) {
            fprintf(stderr, "  frame %d, channels %u, format %d, raw %d, concealed %d, degraded %d\n",
                f, channels, (int)format, raw, !!(fi & FAAD_FRAME_CONCEALED), !!(fi & FAAD_FRAME_DEGRADED));
            return fail("intact frame recovery flags");
        }
        faad_stream_info current = { .struct_size = sizeof(current) };
        faad_decoder_get_info(retry, &current);
        if (!current.format_known || current.max_output_bytes != cap
            || current.decoder_delay != ((fi & FAAD_FRAME_SBR) ? 962u : 0u)) return fail("runtime format/delay");
        pos += packet;
    }
    if (faad_decoder_flush(retry) != FAAD_OK) return fail("flush");
    faad_decoder_close(&control);
    if (faad_decoder_open(&cfg, raw ? asc : NULL, raw ? sizeof(asc) : 0, &control) != FAAD_OK)
        return fail("flush control open");
    uint32_t packet = ((stream[3] & 3) << 11) | (stream[4] << 3) | (stream[5] >> 5);
    uint32_t header = raw ? 7 : 0, used, written, used_b, written_b;
    uint32_t fi = UINT32_MAX, fb;
    if (faad_decode_frame(retry, stream + header, packet - header, &used, a, cap, &written, &fi) != FAAD_OK
        || faad_decode_frame(control, stream + header, packet - header, &used_b, b, cap, &written_b, &fb) != FAAD_OK
        || written != written_b || memcmp(a, b, written) || (fi & FAAD_FRAME_FORMAT_CHANGED)
        || !(fb & FAAD_FRAME_FORMAT_CHANGED)
        || (fi & ~FAAD_FRAME_FORMAT_CHANGED) != (fb & ~FAAD_FRAME_FORMAT_CHANGED))
        return fail("flush retained audio history");
    /* Concealment immediately after flush must not replay the previous track. */
    faad_decoder_flush(retry);
    const uint8_t end[] = { 0xe0 };
    if (raw) {
        if (faad_decode_frame(retry, end, sizeof(end), &used, a, cap, &written, &fi) != FAAD_OK || !(fi & FAAD_FRAME_CONCEALED))
            return fail("post-flush concealment");
        if (format == FAAD_OUTPUT_FLOAT) {
            for (uint32_t i = 0; i < written / sizeof(float); i++)
                if (((float *)a)[i] != 0.0f) return fail("post-flush stale audio");
        } else {
            for (uint32_t i = 0; i < written; i++)
                if (((uint8_t *)a)[i]) return fail("post-flush stale audio");
        }
    }
    faad_decoder_close(&retry); faad_decoder_close(&control);
    free(a); free(b);
    return 0;
}

/* Numeric equivalence across native PCM encodings also checks byte order on
 * big-endian runners. Float is the reference before integer quantization. */
static int test_pcm_formats(const uint8_t *stream)
{
    faad_decoder *dec[4];
    uint32_t cap[4];
    const enum faad_output_format formats[] = {
        FAAD_OUTPUT_16BIT, FAAD_OUTPUT_24BIT, FAAD_OUTPUT_32BIT, FAAD_OUTPUT_FLOAT };
    void *pcm[4];
    for (int f = 0; f < 4; f++) {
        faad_config cfg;
        faad_config_init(&cfg, sizeof(cfg));
        cfg.output_format = formats[f];
        if (faad_decoder_open(&cfg, NULL, 0, &dec[f]) != FAAD_OK) return fail("PCM format open");
        faad_stream_info info = { .struct_size = sizeof(info) };
        faad_decoder_get_info(dec[f], &info);
        cap[f] = info.max_output_bytes;
        pcm[f] = malloc(cap[f]);
        if (!pcm[f]) return fail("PCM format allocation");
    }
    uint32_t pos = 0;
    bool nonzero = false;
    for (int frame = 0; frame < 8; frame++) {
        uint32_t packet = ((stream[pos + 3] & 3) << 11) | (stream[pos + 4] << 3) | (stream[pos + 5] >> 5);
        uint32_t flags[4];
        faad_stream_info info[4];
        for (int f = 0; f < 4; f++) {
            info[f].struct_size = sizeof(info[f]);
            uint32_t used, written;
            if (faad_decode_frame(dec[f], stream + pos, packet, &used, pcm[f], cap[f], &written, &flags[f]) != FAAD_OK)
                return fail("PCM format decode");
            faad_decoder_get_info(dec[f], &info[f]);
        }
        uint32_t count = info[3].frame_samples * info[3].channels;
        for (uint32_t i = 0; i < count; i++) {
            double reference = ((float *)pcm[3])[i];
            if (fabs(reference) > 0.01) nonzero = true;
            if (reference < -1) reference = -1;
            if (reference > 1) reference = 1;
            double s16 = ((int16_t *)pcm[0])[i] / 32768.0;
            double s24 = read_pcm24((uint8_t *)pcm[1] + 3 * i, WORDS_BIGENDIAN) / 8388608.0;
            double s32 = ((int32_t *)pcm[2])[i] / 2147483648.0;
            if (fabs(reference - s16) > 1.0 / 32768 || fabs(reference - s24) > 1.0 / 8388608
                || fabs(reference - s32) > 1.0 / 16777216) return fail("native PCM numeric/byte-order mismatch");
        }
        pos += packet;
    }
    for (int f = 0; f < 4; f++) {
        faad_decoder_close(&dec[f]);
        free(pcm[f]);
    }
    return nonzero ? 0 : fail("PCM format comparison had no audio");
}

/* Walk the ADTS frames, dropping some and flipping bits in others. */
static uint32_t corrupt(const uint8_t *in, uint32_t len, uint8_t *out)
{
    uint32_t i = 0, o = 0;
    while (i + 7 <= len) {
        if (!(in[i] == 0xFF && (in[i + 1] & 0xF6) == 0xF0)) { i++; continue; }
        uint32_t flen = ((in[i + 3] & 3) << 11) | (in[i + 4] << 3) | (in[i + 5] >> 5);
        if (flen < 7 || i + flen > len) break;
        uint32_t r = rng() % 100;
        if (r < 8) { i += flen; continue; }                 /* dropped frame */
        memcpy(out + o, in + i, flen);
        if (r < 40) {                                      /* payload bit flips */
            int flips = 1 + (int)(rng() % 6);
            for (int k = 0; k < flips; k++) out[o + 7 + rng() % (flen - 7)] ^= (uint8_t)(1u << (rng() % 8));
        } else if (r < 46) {                               /* header damage */
            out[o + 2 + rng() % 5] ^= (uint8_t)(1u << (rng() % 8));
        } else if (r < 50) {                               /* truncated frame */
            o += flen / 2; i += flen; continue;
        }
        o += flen;
        i += flen;
    }
    return o;
}

static int decode_bounded(const uint8_t *stream, uint32_t len, uint32_t max_frames, uint32_t expected_channels,
                          enum faad_output_format format, enum faad_downmix_mode downmix, bool raw)
{
    faad_config cfg;
    faad_config_init(&cfg, sizeof(cfg));
    cfg.stream_format = raw ? FAAD_STREAM_RAW : FAAD_STREAM_ADTS;
    uint8_t asc[2];
    asc[0] = (uint8_t)(0x10 | ((stream[2] >> 2) & 15) >> 1);
    asc[1] = (uint8_t)((((stream[2] >> 2) & 1) << 7) | (expected_channels << 3));
    cfg.output_format = format;
    cfg.downmix_mode = downmix;
    faad_decoder *dec = NULL;
    if (faad_decoder_open(&cfg, raw ? asc : NULL, raw ? sizeof(asc) : 0, &dec) != FAAD_OK) return fail("decoder create");

    faad_stream_info info = { .struct_size = sizeof(info) };
    if (faad_decoder_get_info(dec, &info) != FAAD_OK) return fail("stream info");
    uint32_t lifetime_bound = info.max_output_bytes;
    static float pcm[8 * 2048];
    uint32_t pos = 0, calls = 0, frames = 0;
    uint64_t out_bytes = 0;
    while (pos < len) {
        uint32_t used = 0, written = 0;
        uint32_t fi;
        uint32_t packet = len - pos, header = 0;
        if (raw) {
            packet = ((stream[pos + 3] & 3) << 11) | (stream[pos + 4] << 3) | (stream[pos + 5] >> 5);
            header = 7;
        }
        faad_status st = faad_decode_frame(dec, stream + pos + header, packet - header,
            &used, pcm, lifetime_bound, &written, &fi);
        used += header;
        calls++;
        if (st == FAAD_ERR_NEED_MORE_DATA) break;
        if (used == 0) { faad_decoder_close(&dec); return fail("decoder did not advance"); }
        if (written > lifetime_bound) { faad_decoder_close(&dec); return fail("output exceeds lifetime bound"); }
        if (written && expected_channels == 6 && downmix == FAAD_DOWNMIX_NONE) {
            faad_decoder_get_info(dec, &info);
            if (info.channel_mask != 0x3f || info.channels != 6) return fail("5.1 channel mask");
        }
        if (written > sizeof(pcm)) { faad_decoder_close(&dec); return fail("output exceeds buffer"); }
        if (written > 0) frames++;
        out_bytes += written;
        pos += used;
        /* a call per byte of input is the loosest bound a resync could need */
        if (calls > len + 16) { faad_decoder_close(&dec); return fail("too many calls: stalled"); }
    }
    faad_decoder_close(&dec);
    if (frames > max_frames) return fail("more output frames than the intact stream");
    if (out_bytes > (uint64_t)max_frames * 2048 * 8 * 2) return fail("output larger than the intact stream");
    return 0;
}


/* Malformed HE-AAC access units that once hung the decoder: a decode that
 * returns at all is the pass condition (the test runner's timeout is the
 * failure). Bytes come from fuzzing; layout is ASC, then the raw packets. */
typedef struct { const uint8_t *asc; uint32_t asc_len, chunk; const uint8_t *data; uint32_t len; } hang_case;

/* PS extension loop without input left */
static const uint8_t ps_ext_asc[] = { 0xeb, 0x08, 0x00, 0x1e };
static const uint8_t ps_ext_data[] = {
    0x64, 0xe0, 0x2d, 0x4d, 0x6f, 0x43, 0x48, 0x5c, 0x18, 0x98, 0x95, 0x10,
    0x44, 0x9c, 0xe9, 0x76, 0xe8, 0xbc, 0xee, 0x30, 0x26, 0xef, 0xe5, 0x68,
    0x20, 0x3f, 0x16, 0xe1, 0x1b, 0xa8, 0x63, 0x95, 0x08, 0xd4, 0x4b, 0xb7,
    0xe9, 0xd6, 0x3f, 0xcc, 0x43, 0xc3, 0xc9, 0x90, 0x65, 0x63, 0x0b, 0x25,
    0xf1, 0x30, 0x2c, 0xd0, 0x19, 0x9e, 0x1d, 0x89, 0x0b, 0xe7, 0x2d, 0xaf,
    0xa3, 0xfd, 0x50, 0x23, 0x4d, 0xd4, 0x38, 0xde, 0xa4, 0xcd, 0x09, 0x9b,
    0xe6, 0xa3, 0x9b, 0x95, 0x59, 0x99, 0xbd, 0x09, 0x21, 0x97, 0xbf, 0xad,
    0x91, 0xad, 0xfd, 0x33, 0xda, 0x6b, 0x5c, 0xcf, 0xe7, 0x62, 0x77,
};

/* SBR patch search that never reaches the last band */
static const uint8_t patches_asc[] = { 0xeb, 0x5f, 0x92, 0x08 };
static const uint8_t patches_data[] = {
    0x77, 0x0a, 0xa9, 0x49, 0xa1, 0x46, 0x77, 0xf9, 0xbc, 0xad, 0x88, 0x85,
    0x64, 0xca, 0xf9, 0x29, 0x60, 0x0e, 0x7f, 0x59, 0x64, 0x9b, 0xe5, 0xb4,
    0x8f, 0x4a, 0xa3, 0xba, 0x6e, 0x0e, 0x07, 0x71, 0x0e, 0xbb, 0x04, 0xa9,
    0xb1, 0xc9, 0x2d, 0x20, 0x52, 0x2b, 0x39, 0x18, 0x29, 0xb6, 0x82, 0x4b,
    0x18, 0xc4, 0xab, 0x18, 0x73, 0xdc, 0xad, 0xb8, 0xd7, 0xd4, 0xbe, 0x6e,
    0x68, 0x05, 0x72, 0xed, 0xae, 0xdd, 0x99, 0x9b, 0xb5, 0xee, 0x2d, 0x5a,
    0xe3, 0x02, 0x1f, 0xeb, 0x89, 0x11, 0xc6, 0x5c, 0x68, 0x9b, 0xb9, 0x40,
    0x92, 0x25, 0xa9, 0x69, 0x52, 0x55, 0x70, 0x64, 0x2a, 0x2f, 0xa6, 0x09,
    0x40, 0x8d, 0x52, 0x11, 0xbf, 0x2c, 0xd9, 0xfc, 0xc5, 0x26, 0x66, 0x1b,
    0xc0, 0x1c, 0x69, 0x07, 0xd7, 0x5d, 0x35, 0xf6, 0x65, 0x64, 0x14, 0x1a,
    0xb4, 0xa4, 0xf8, 0x50, 0x85, 0x6b, 0x9e, 0xb9, 0xc8, 0xd2, 0xee, 0x61,
    0x73, 0x28, 0xc7, 0x52, 0xd4, 0x1e, 0x19, 0x9f, 0xde, 0xd0, 0x9d, 0x02,
    0x48, 0x73, 0x5c, 0x46, 0xc6, 0xcc, 0xe7, 0x38, 0x91, 0xfb, 0xb7, 0x64,
    0x75, 0x60, 0x75, 0xa6, 0x94, 0x65, 0x73, 0xc9, 0x15, 0x82, 0x24, 0x1c,
    0x88, 0x77, 0x63, 0xe0, 0x91, 0xfc, 0xe5, 0xae, 0x81, 0xdd, 0x3b, 0x40,
    0x6a, 0x2c, 0xa7, 0x07, 0xb9, 0x0f, 0xb6, 0xf1, 0x87, 0xe7, 0xea,
};

static const hang_case hang_cases[] = {
    { ps_ext_asc, sizeof(ps_ext_asc), 640, ps_ext_data, sizeof(ps_ext_data) },
    { patches_asc, sizeof(patches_asc), 8, patches_data, sizeof(patches_data) },
};

static void decode_hang_case(const hang_case *hc)
{
    faad_config cfg;
    faad_config_init(&cfg, sizeof(cfg));
    cfg.stream_format = FAAD_STREAM_RAW;
    faad_decoder *dec = NULL;
    if (faad_decoder_open(&cfg, hc->asc, hc->asc_len, &dec) != FAAD_OK) return; /* rejecting the config is fine */
    faad_stream_info info = { .struct_size = sizeof(info) };
    faad_decoder_get_info(dec, &info);
    static float pcm[8 * 2048];
    uint32_t cap = info.max_output_bytes < sizeof(pcm) ? info.max_output_bytes : (uint32_t)sizeof(pcm);
    for (uint32_t pos = 0, calls = 0; pos < hc->len && calls < 64; calls++) {
        uint32_t take = hc->len - pos < hc->chunk ? hc->len - pos : hc->chunk, used = 0, written = 0;
        uint32_t fi;
        faad_status st = faad_decode_frame(dec, hc->data + pos, take, &used, pcm, cap, &written, &fi);
        if (st == FAAD_ERR_NEED_MORE_DATA || st == FAAD_ERR_OUTPUT_TOO_SMALL) break;
        pos += used ? used : take;
    }
    faad_decoder_close(&dec);
}

int main(void)
{
    static uint8_t intact[1 << 20], damaged[1 << 20];
    const enum faac_object_type objs[2] = { FAAC_OBJ_LOW, FAAC_OBJ_HE_AAC_V1 };
    faad_library_info lib = { .struct_size = sizeof(lib) };
    faad_get_library_info(&lib);
    for (int t = 0; t < 4; t++) {
        uint32_t channels = t == 0 ? 1 : t == 3 ? 6 : 2;
        enum faac_object_type obj = objs[t == 2 ? 1 : 0];
        if (channels > lib.max_channels || (t == 2 && !lib.sbr_supported)) continue;
        uint32_t len = encode(obj, channels, intact, sizeof(intact));
        if (len == 0) return fail("encode");
        if (test_pcm_formats(intact)) return 1;
        uint32_t frames_intact = 0;
        for (uint32_t i = 0; i + 7 <= len; ) {
            uint32_t flen = ((intact[i + 3] & 3) << 11) | (intact[i + 4] << 3) | (intact[i + 5] >> 5);
            if (flen < 7) break;
            frames_intact++;
            i += flen;
        }
        /* the intact stream itself must decode within its own frame count */
        static const enum faad_output_format formats[] = {
            FAAD_OUTPUT_16BIT, FAAD_OUTPUT_24BIT, FAAD_OUTPUT_32BIT, FAAD_OUTPUT_FLOAT };
        for (int f = 0; f < 4; f++)
            for (int d = 0; d < 3; d++)
                for (int raw = 0; raw < 2; raw++)
                if (decode_bounded(intact, len, frames_intact, channels,
                    formats[f], (enum faad_downmix_mode)d, raw != 0)
                    || test_retry_and_flush(intact, channels, formats[f], (enum faad_downmix_mode)d, raw != 0)) return 1;
        for (int seed = 1; seed <= 8; seed++) {
            rng_state = 0x9E3779B9u * (uint32_t)seed;
            uint32_t dlen = corrupt(intact, len, damaged);
            if (decode_bounded(damaged, dlen, frames_intact, 0, FAAD_OUTPUT_16BIT, FAAD_DOWNMIX_NONE, false)) {
                fprintf(stderr, "  object %d, corruption seed %d\n", (int)obj, seed);
                return 1;
            }
        }
    }
    for (size_t i = 0; i < sizeof(hang_cases) / sizeof(hang_cases[0]); i++) decode_hang_case(&hang_cases[i]);
    printf("test_faad_robust: ok\n");
    return 0;
}
