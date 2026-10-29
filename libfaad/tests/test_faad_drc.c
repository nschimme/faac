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

/* Exercise dynamic-range fill elements through the encoder and decoder APIs. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "faac.h"
#include "faad3.h"
#include "faad_internal.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct { uint8_t *data; size_t len, cap; } bytes;
typedef struct { uint32_t flags, short_window, ref_present, ref_level; size_t offset, len; } frame_info;
typedef struct { bytes pcm; frame_info frames[128]; size_t count; } decoded;
typedef struct { uint8_t data[8192]; size_t bits; } bitstream;
static unsigned fixture_channels;

static void reserve(bytes *b, size_t add)
{
    if (b->len + add > b->cap) {
        size_t cap = b->cap ? b->cap : 4096;
        while (cap < b->len + add) cap *= 2;
        b->data = (uint8_t *)realloc(b->data, cap);
        assert(b->data);
        b->cap = cap;
    }
}

static void put_bits(bitstream *b, uint32_t value, unsigned width)
{
    assert(width <= 32 && (width == 32 || value < (UINT32_C(1) << width)));
    assert(b->bits + width <= sizeof(b->data) * 8);
    for (unsigned i = width; i; i--, b->bits++)
        if ((value >> (i - 1)) & 1) b->data[b->bits / 8] |= (uint8_t)(1u << (7 - b->bits % 8));
}

static void put_from(bitstream *out, const uint8_t *in, size_t from, size_t to)
{
    for (size_t i = from; i < to; i++)
        put_bits(out, (in[i / 8] >> (7 - i % 8)) & 1, 1);
}

static void make_payload(bitstream *p, int sign, unsigned control, int ref,
                         const uint8_t *tops, const uint8_t *controls, unsigned bands,
                         int exclude)
{
    put_bits(p, 0xb, 4);                 /* dynamic_range_info extension */
    put_bits(p, 0, 1);                   /* reserved */
    put_bits(p, exclude != 0, 1);
    if (exclude) put_bits(p, 0x40, 8);   /* FL included, FR excluded */
    put_bits(p, bands != 0, 1);
    if (bands) {
        put_bits(p, bands - 1, 4);
        put_bits(p, 0, 4);               /* band-top reserved */
        for (unsigned i = 0; i < bands; i++) put_bits(p, tops[i], 8);
    }
    put_bits(p, ref >= 0, 1);
    if (ref >= 0) { put_bits(p, (unsigned)ref, 7); put_bits(p, 0, 1); }
    if (bands) {
        for (unsigned i = 0; i < bands; i++) {
            put_bits(p, sign != 0, 1);
            put_bits(p, controls ? controls[i] : control, 7);
        }
    } else {
        put_bits(p, sign != 0, 1);
        put_bits(p, control, 7);
    }
    while (p->bits % 8) put_bits(p, 0, 1);
}

static void put_fill(bitstream *out, const bitstream *payload, unsigned count)
{
    put_bits(out, 6, 3);                 /* ID_FIL */
    put_bits(out, count >= 15 ? 15 : count, 4);
    if (count >= 15) put_bits(out, count - 14, 8);
    put_from(out, payload->data, 0, payload->bits);
}

static uint32_t adts_size(const uint8_t *p)
{
    return ((uint32_t)(p[3] & 3) << 11) | ((uint32_t)p[4] << 3) | (p[5] >> 5);
}

static bytes encode_fixture(unsigned channels, unsigned kind)
{
    faac_params p;
    faac_encoder *enc = NULL;
    faac_encoder_info info = { .struct_size = sizeof(info) };
    assert(faac_params_init(&p, sizeof(p)) == FAAC_OK);
    p.sample_rate = 44100;
    p.num_channels = channels;
    p.object_type = FAAC_OBJ_LOW;
    p.bit_rate = 96000;
    p.output_format = FAAC_STREAM_ADTS;
    p.input_format = FAAC_INPUT_16BIT;
    assert(faac_encoder_open(&p, &enc) == FAAC_OK);
    assert(faac_encoder_get_info(enc, &info) == FAAC_OK);
    bytes stream = {0};
    uint32_t frames = 15435 / info.frame_samples + (15435 % info.frame_samples != 0);
    int16_t pcm[1024 * 2];
    for (uint32_t f = 0; f < frames; f++) {
        for (uint32_t i = 0; i < info.frame_samples; i++) {
            double t = (double)(f * info.frame_samples + i) / 44100.0;
            double sample;
            if (kind == 1) {
                sample = 1500 * (sin(2 * M_PI * 440 * t) + sin(2 * M_PI * 4000 * t) + sin(2 * M_PI * 10000 * t));
            } else if (kind == 2) {
                sample = 30000 * sin(2 * M_PI * 500 * t);
            } else if (kind == 3) {
                sample = (i % 4096 < 64) ? 6000 * sin((double)i * 1.7) : 0;
            } else {
                sample = 7000 * sin(2 * M_PI * 440 * t) + 900 * sin(2 * M_PI * 1234 * t);
            }
            for (unsigned c = 0; c < channels; c++) pcm[i * channels + c] = (int16_t)sample;
        }
        uint32_t bytes_out = 0;
        reserve(&stream, info.max_output_bytes);
        assert(faac_encoder_encode(enc, pcm, info.frame_samples * channels,
            stream.data + stream.len, (uint32_t)(stream.cap - stream.len), &bytes_out) == FAAC_OK);
        stream.len += bytes_out;
    }
    for (;;) {
        uint32_t bytes_out = 0;
        reserve(&stream, info.max_output_bytes);
        assert(faac_encoder_encode(enc, NULL, 0, stream.data + stream.len,
            (uint32_t)(stream.cap - stream.len), &bytes_out) == FAAC_OK);
        stream.len += bytes_out;
        if (!bytes_out) break;
    }
    assert(faac_encoder_close(&enc) == FAAC_OK);
    assert(stream.len > 0);
    return stream;
}

static bytes inject(const bytes *stream, const bitstream *payload, int at_start, unsigned count)
{
    bytes result = {0};
    if (!count) count = (unsigned)(payload->bits / 8);
    for (size_t pos = 0; pos < stream->len;) {
        const uint8_t *header = stream->data + pos;
        uint32_t frame_len = adts_size(header);
        assert(frame_len >= 7 && pos + frame_len <= stream->len);
        const uint8_t *raw = header + 7;
        size_t raw_bits = (frame_len - 7) * 8;
        size_t trailing = 0;
        while (trailing < raw_bits && ((raw[(raw_bits - trailing - 1) / 8]
            >> (7 - (raw_bits - trailing - 1) % 8)) & 1) == 0) trailing++;
        assert(trailing + 3 <= raw_bits);
        size_t end = raw_bits - trailing - 3;
        assert(((raw[end / 8] >> (7 - end % 8)) & 1) == 1);
        assert(((raw[(end + 1) / 8] >> (7 - (end + 1) % 8)) & 1) == 1);
        assert(((raw[(end + 2) / 8] >> (7 - (end + 2) % 8)) & 1) == 1);

        bitstream body = {{0}, 0};
        if (at_start) {
            put_fill(&body, payload, count);
            put_from(&body, raw, 0, end + 3);
        } else {
            put_from(&body, raw, 0, end);
            put_fill(&body, payload, count);
            put_from(&body, raw, end, end + 3);
            put_from(&body, raw, end + 3, raw_bits);
        }
        while (body.bits % 8) put_bits(&body, 0, 1);
        uint32_t out_len = 7 + (uint32_t)(body.bits / 8);
        assert(out_len <= 0x1fff);
        reserve(&result, out_len);
        memcpy(result.data + result.len, header, 7);
        result.data[result.len + 3] = (uint8_t)((header[3] & 0xfc) | (out_len >> 11));
        result.data[result.len + 4] = (uint8_t)(out_len >> 3);
        result.data[result.len + 5] = (uint8_t)((header[5] & 0x1f) | ((out_len & 7) << 5));
        memcpy(result.data + result.len + 7, body.data, body.bits / 8);
        result.len += out_len;
        pos += frame_len;
    }
    return result;
}

static decoded decode(const bytes *stream, unsigned cut, unsigned boost, unsigned target,
                      int floating, unsigned flush_after)
{
    faad_config cfg;
    faad_decoder *dec = NULL;
    decoded out = {0};
    assert(faad_config_init(&cfg, sizeof(cfg)) == FAAD_OK);
    cfg.drc_cut = (uint8_t)cut;
    cfg.drc_boost = (uint8_t)boost;
    cfg.drc_target_level = (uint8_t)target;
    cfg.output_format = floating ? FAAD_OUTPUT_FLOAT : FAAD_OUTPUT_16BIT;
    assert(faad_decoder_open(&cfg, NULL, 0, &dec) == FAAD_OK);
    faad_stream_info info = { .struct_size = sizeof(info) };
    assert(faad_decoder_get_info(dec, &info) == FAAD_OK);
    uint8_t *pcm = (uint8_t *)malloc(info.max_output_bytes);
    assert(pcm);
    for (size_t pos = 0; pos < stream->len;) {
        uint32_t frame_len = adts_size(stream->data + pos);
        uint32_t used = 0, wrote = 0, flags = 0;
        assert(frame_len >= 7 && pos + frame_len <= stream->len);
        assert(faad_decode_frame(dec, stream->data + pos, frame_len, &used,
            pcm, info.max_output_bytes, &wrote, &flags) == FAAD_OK);
        assert(used == frame_len && wrote > 0 && out.count < sizeof(out.frames) / sizeof(out.frames[0]));
        assert(faad_decoder_get_info(dec, &info) == FAAD_OK);
        frame_info *r = &out.frames[out.count++];
        r->flags = flags;
        r->short_window = dec->win_seq[0] == EIGHT_SHORT_SEQUENCE;
        r->ref_present = (info.flags & FAAD_STREAM_PROG_REF_LEVEL) != 0;
        r->ref_level = info.prog_ref_level;
        r->offset = out.pcm.len;
        r->len = wrote;
        reserve(&out.pcm, wrote);
        memcpy(out.pcm.data + out.pcm.len, pcm, wrote);
        out.pcm.len += wrote;
        pos += used;
        if (flush_after && out.count == flush_after) {
            assert(faad_decoder_flush(dec) == FAAD_OK);
        }
    }
    free(pcm);
    assert(faad_decoder_close(&dec) == FAAD_OK);
    assert(out.count >= 8);
    return out;
}

static void compare_pcm(const decoded *a, const decoded *b)
{
    assert(a->count == b->count && a->pcm.len == b->pcm.len);
    assert(memcmp(a->pcm.data, b->pcm.data, a->pcm.len) == 0);
}

static double energy(const decoded *d, unsigned skip_frames)
{
    const float *p = (const float *)d->pcm.data;
    size_t start = d->frames[skip_frames].offset / sizeof(float);
    size_t end = d->pcm.len / sizeof(float);
    double sum = 0;
    for (size_t i = start; i < end; i++) sum += (double)p[i] * p[i];
    return sum;
}

static void release_decoded(decoded *d)
{
    free(d->pcm.data);
    memset(d, 0, sizeof(*d));
}

static void test_defaults(const bytes *original)
{
    bitstream p = {{0}, 0}; make_payload(&p, 1, 24, -1, NULL, NULL, 0, 0);
    bytes with = inject(original, &p, 1, 0);
    for (unsigned floating = 0; floating <= 1; floating++) {
        decoded base = decode(original, 0, 0, 0, floating, 0);
        decoded got = decode(&with, 0, 0, 0, floating, 0);
        compare_pcm(&base, &got);
        release_decoded(&base); release_decoded(&got);
    }
    free(with.data);
}

static int drc_enabled(void)
{
#ifdef FAAD_DISABLE_DRC
    return 0;
#else
    return 1;
#endif
}

static void test_gain(const bytes *original)
{
    decoded base = decode(original, 0, 0, 0, 1, 0);
    for (unsigned sign = 0; sign <= 1; sign++) {
        bitstream payload = {{0}, 0};
        make_payload(&payload, (int)sign, 24, -1, NULL, NULL, 0, 0);
        bytes input = inject(original, &payload, 1, 0);
        for (unsigned amount = 64; amount <= 127; amount += 63) {
            decoded changed = decode(&input, sign ? amount : 0, sign ? 0 : amount, 0, 1, 0);
            double expected = drc_enabled() ? (sign ? -1.0 : 1.0) * amount / 127.0 * 20.0 * log10(2.0) : 0.0;
            double actual = 10.0 * log10(energy(&changed, 3) / energy(&base, 3));
            assert(fabs(actual - expected) < 0.05);
            for (size_t f = 0; f < changed.count; f++) {
                assert(!!(changed.frames[f].flags & FAAD_FRAME_DRC) == !!drc_enabled());
                assert(!!(changed.frames[f].flags & FAAD_FRAME_DRC_INFO) == !!drc_enabled());
            }
            if (!drc_enabled()) compare_pcm(&base, &changed);
            release_decoded(&changed);
        }
        free(input.data);
    }
    release_decoded(&base);
}

static void test_integer_limiter(void)
{
    if (!drc_enabled()) return;

    bytes original = encode_fixture(fixture_channels, 2);
    bitstream payload = {{0}, 0};
    make_payload(&payload, 0, 24, -1, NULL, NULL, 0, 0);
    bytes input = inject(&original, &payload, 1, 0);
    decoded floating = decode(&input, 0, 127, 0, 1, 0);
    decoded integer = decode(&input, 0, 127, 0, 0, 0);

    const float *float_pcm = (const float *)floating.pcm.data;
    size_t float_count = floating.pcm.len / sizeof(*float_pcm);
    float float_peak = 0;
    for (size_t i = 0; i < float_count; i++) {
        float value = fabsf(float_pcm[i]);
        if (value > float_peak) float_peak = value;
    }
    assert(float_peak > 1.0f);

    const int16_t *int_pcm = (const int16_t *)integer.pcm.data;
    size_t int_count = integer.pcm.len / sizeof(*int_pcm);
    int32_t int_peak = 0;
    int16_t int_min = INT16_MAX, int_max = INT16_MIN;
    for (size_t i = 0; i < int_count; i++) {
        int32_t value = int_pcm[i];
        int32_t magnitude = value < 0 ? -value : value;
        if (magnitude > int_peak) int_peak = magnitude;
        if (int_pcm[i] < int_min) int_min = int_pcm[i];
        if (int_pcm[i] > int_max) int_max = int_pcm[i];
    }
    assert(int_min > INT16_MIN && int_max < INT16_MAX);
    assert(int_peak > 0.89 * 32768.0);

    release_decoded(&floating); release_decoded(&integer);
    free(input.data); free(original.data);
}

static void test_routing_and_order(const bytes *original)
{
    decoded base = decode(original, 0, 0, 0, 1, 0);
    decoded both = decode(original, 127, 127, 0, 1, 0);
    compare_pcm(&base, &both);
    release_decoded(&both);
    for (unsigned sign = 0; sign <= 1; sign++) {
        bitstream payload = {{0}, 0};
        make_payload(&payload, (int)sign, 24, -1, NULL, NULL, 0, 0);
        bytes input = inject(original, &payload, 1, 0);
        decoded wrong_route = decode(&input, sign ? 0 : 127, sign ? 127 : 0, 0, 1, 0);
        compare_pcm(&base, &wrong_route);
        for (size_t f = 0; f < wrong_route.count; f++) {
            assert(!(wrong_route.frames[f].flags & FAAD_FRAME_DRC));
            assert(!!(wrong_route.frames[f].flags & FAAD_FRAME_DRC_INFO) == !!drc_enabled());
        }
        release_decoded(&wrong_route);
        decoded before = decode(&input, 127, 0, 0, 1, 0);
        bitstream after_payload = {{0}, 0};
        make_payload(&after_payload, (int)sign, 24, -1, NULL, NULL, 0, 0);
        bytes after_input = inject(original, &after_payload, 0, 0);
        decoded after = decode(&after_input, 127, 0, 0, 1, 0);
        compare_pcm(&before, &after);
        release_decoded(&before); release_decoded(&after);
        free(after_input.data); free(input.data);
    }
    release_decoded(&base);
}

static void test_frame_flags(const bytes *original)
{
    bitstream zero = {{0}, 0}, active = {{0}, 0};
    make_payload(&zero, 1, 0, -1, NULL, NULL, 0, 0);
    make_payload(&active, 1, 24, -1, NULL, NULL, 0, 0);
    bytes zero_input = inject(original, &zero, 1, 0);
    bytes zero_result = {0};
    decoded zero_decoded = decode(&zero_input, 127, 0, 0, 1, 0);
    for (size_t f = 0; f < zero_decoded.count; f++) {
        assert(!(zero_decoded.frames[f].flags & FAAD_FRAME_DRC));
        assert(!!(zero_decoded.frames[f].flags & FAAD_FRAME_DRC_INFO) == !!drc_enabled());
    }
    release_decoded(&zero_decoded); free(zero_input.data);

    uint32_t first_len = adts_size(original->data);
    bytes first = { original->data, first_len, first_len };
    bytes changed_first = inject(&first, &active, 1, 0);
    reserve(&zero_result, changed_first.len + original->len - first_len);
    memcpy(zero_result.data, changed_first.data, changed_first.len);
    memcpy(zero_result.data + changed_first.len, original->data + first_len, original->len - first_len);
    zero_result.len = changed_first.len + original->len - first_len;
    decoded base = decode(original, 127, 0, 0, 1, 0);
    decoded got = decode(&zero_result, 127, 0, 0, 1, 0);
    assert(base.count == got.count);
    assert(!!(got.frames[0].flags & FAAD_FRAME_DRC) == !!drc_enabled());
    assert(!!(got.frames[0].flags & FAAD_FRAME_DRC_INFO) == !!drc_enabled());
    for (size_t f = 1; f < got.count; f++) {
        assert(!(got.frames[f].flags & FAAD_FRAME_DRC));
        assert(!(got.frames[f].flags & FAAD_FRAME_DRC_INFO));
    }
    size_t offset = base.frames[3].offset;
    assert(memcmp(base.pcm.data + offset, got.pcm.data + offset, base.pcm.len - offset) == 0);
    release_decoded(&base); release_decoded(&got);
    free(changed_first.data); free(zero_result.data);
}

static void test_channel_exclusion(const bytes *original)
{
    faac_library_info enc = { .struct_size = sizeof(enc) };
    assert(faac_get_library_info(&enc) == FAAC_OK);
    if (enc.max_channels < 2) return;
    bitstream payload = {{0}, 0};
    make_payload(&payload, 1, 24, -1, NULL, NULL, 0, 1);
    bytes input = inject(original, &payload, 1, 0);
    decoded base = decode(original, 0, 0, 0, 1, 0);
    decoded changed = decode(&input, 127, 0, 0, 1, 0);
    assert(base.pcm.len == changed.pcm.len);
    const float *a = (const float *)base.pcm.data;
    const float *b = (const float *)changed.pcm.data;
    size_t first = base.frames[3].offset / sizeof(float);
    double left_a = 0, left_b = 0;
    for (size_t i = first; i + 1 < base.pcm.len / sizeof(float); i += 2) {
        left_a += (double)a[i] * a[i];
        left_b += (double)b[i] * b[i];
        assert(a[i + 1] == b[i + 1]);
    }
    double expected = drc_enabled() ? -20.0 * log10(2.0) : 0.0;
    double actual = 10.0 * log10(left_b / left_a);
    assert(fabs(actual - expected) < 0.05);
    release_decoded(&base); release_decoded(&changed); free(input.data);
}

static double projected_energy(const decoded *d, unsigned channel, double frequency)
{
    const float *pcm = (const float *)d->pcm.data;
    size_t first = d->frames[3].offset / sizeof(float);
    size_t samples = d->pcm.len / sizeof(float) / fixture_channels;
    double real = 0, imag = 0;
    for (size_t i = first / fixture_channels; i < samples; i++) {
        double x = pcm[i * fixture_channels + channel];
        double phase = 2.0 * M_PI * frequency * (i - first / fixture_channels) / 44100.0;
        real += x * cos(phase);
        imag += x * sin(phase);
    }
    return real * real + imag * imag;
}

static void test_multiband(void)
{
    bytes original = encode_fixture(fixture_channels, 1);
    bitstream payload = {{0}, 0};
    const uint8_t tops[] = {31, 95}, controls[] = {24, 12};
    make_payload(&payload, 1, 24, -1, tops, controls, 2, 0);
    bytes input = inject(&original, &payload, 1, 0);
    decoded base = decode(&original, 0, 0, 0, 1, 0);
    decoded changed = decode(&input, 127, 0, 0, 1, 0);
    const double frequencies[] = {440, 4000, 10000};
    const double gains[] = {0.5, 0.7071067811865476, 1.0};
    for (unsigned i = 0; i < 3; i++) {
        double expected = drc_enabled() ? 20.0 * log10(gains[i]) : 0.0;
        double actual = 10.0 * log10(projected_energy(&changed, 0, frequencies[i]) /
                                      projected_energy(&base, 0, frequencies[i]));
        assert(fabs(actual - expected) < 0.1);
    }
    release_decoded(&base); release_decoded(&changed);
    free(input.data); free(original.data);
}

static void test_short_window(void)
{
    bytes original = encode_fixture(fixture_channels, 3);
    decoded base = decode(&original, 0, 0, 0, 1, 0);
    int found_short = 0;
    for (size_t f = 0; f < base.count; f++) found_short |= base.frames[f].short_window != 0;
    assert(found_short);
    bitstream payload = {{0}, 0};
    make_payload(&payload, 1, 24, -1, NULL, NULL, 0, 0);
    bytes input = inject(&original, &payload, 1, 0);
    decoded changed = decode(&input, 127, 0, 0, 1, 0);
    double expected = drc_enabled() ? -20.0 * log10(2.0) : 0.0;
    double actual = 10.0 * log10(energy(&changed, 3) / energy(&base, 3));
    assert(fabs(actual - expected) < 0.05);
    release_decoded(&base); release_decoded(&changed);
    free(input.data); free(original.data);
}

static void test_normalisation(const bytes *original)
{
    bitstream payload = {{0}, 0};
    make_payload(&payload, 1, 0, 124, NULL, NULL, 0, 0);
    bytes with = inject(original, &payload, 1, 0);
    decoded base = decode(original, 0, 0, 92, 1, 0);
    decoded changed = decode(&with, 0, 0, 92, 1, 0);
    double expected = drc_enabled() ? 20.0 * log10(2.0) * 32.0 / 24.0 : 0.0;
    double actual = 10.0 * log10(energy(&changed, 3) / energy(&base, 3));
    assert(fabs(actual - expected) < 0.05);
    for (size_t f = 0; f < changed.count; f++) {
        assert(!!changed.frames[f].ref_present == !!drc_enabled());
        if (drc_enabled()) assert(changed.frames[f].ref_level == 124);
        assert(!!(changed.frames[f].flags & FAAD_FRAME_DRC) == !!drc_enabled());
    }
    if (!drc_enabled()) compare_pcm(&base, &changed);

    uint32_t first_len = adts_size(original->data);
    bytes first = { original->data, first_len, first_len };
    bytes changed_first = inject(&first, &payload, 1, 0);
    bytes persistent = {0};
    reserve(&persistent, changed_first.len + original->len - first_len);
    memcpy(persistent.data, changed_first.data, changed_first.len);
    memcpy(persistent.data + changed_first.len, original->data + first_len, original->len - first_len);
    persistent.len = changed_first.len + original->len - first_len;
    decoded flushed = decode(&persistent, 0, 0, 92, 1, 5);
    for (size_t f = 5; f < flushed.count; f++) assert(!flushed.frames[f].ref_present);

    bytes changing = {0};
    size_t pos = 0, frame = 0;
    size_t frame_count = 0;
    for (size_t p = 0; p < original->len;) { p += adts_size(original->data + p); frame_count++; }
    while (pos < original->len) {
        uint32_t size = adts_size(original->data + pos);
        bytes packet = { (uint8_t *)(original->data + pos), size, size };
        bitstream per_frame = {{0}, 0};
        make_payload(&per_frame, 1, 0, frame < frame_count / 2 ? 124 : 92, NULL, NULL, 0, 0);
        bytes altered = inject(&packet, &per_frame, 1, 0);
        reserve(&changing, altered.len);
        memcpy(changing.data + changing.len, altered.data, altered.len);
        changing.len += altered.len;
        free(altered.data);
        pos += size; frame++;
    }
    decoded ramped = decode(&changing, 0, 0, 92, 1, 0);
    decoded reference = decode(original, 0, 0, 92, 1, 0);
    assert(ramped.count == reference.count);
    double previous = 1.0;
    for (size_t f = 0; f < ramped.count; f++) {
        const float *actual_pcm = (const float *)(ramped.pcm.data + ramped.frames[f].offset);
        const float *base_pcm = (const float *)(reference.pcm.data + reference.frames[f].offset);
        size_t count = ramped.frames[f].len / sizeof(float);
        double target_gain = drc_enabled() && f < frame_count / 2 ? exp2(32.0 / 24.0) : 1.0;
        size_t samples = count / fixture_channels;
        for (size_t i = 0; i < count; i++) {
            double gain = previous + (target_gain - previous) * ((i / fixture_channels + 1.0) / samples);
            assert(fabs(actual_pcm[i] - base_pcm[i] * gain) < 2e-7);
        }
        previous = target_gain;
    }
    release_decoded(&base); release_decoded(&changed); release_decoded(&flushed);
    release_decoded(&ramped); release_decoded(&reference);
    free(changed_first.data); free(persistent.data); free(with.data); free(changing.data);
}

static void test_malformed(const bytes *original)
{
    decoded base = decode(original, 127, 0, 0, 1, 0);
    const char *payloads[] = { "10110000", "10111000", "" };
    for (unsigned n = 0; n < sizeof(payloads) / sizeof(payloads[0]); n++) {
        bitstream p = {{0}, 0};
        for (const char *s = payloads[n]; *s; s++) put_bits(&p, *s == '1', 1);
        bytes input = inject(original, &p, 1, 0);
        decoded got = decode(&input, 127, 0, 0, 1, 0);
        compare_pcm(&base, &got);
        for (size_t f = 0; f < got.count; f++)
            assert(!(got.frames[f].flags & FAAD_FRAME_DRC));
        release_decoded(&got); free(input.data);
    }
    bitstream duplicate = {{0}, 0};
    const uint8_t tops[] = {40, 40}, controls[] = {24, 24};
    make_payload(&duplicate, 1, 24, -1, tops, controls, 2, 0);
    bytes input = inject(original, &duplicate, 1, 0);
    decoded got = decode(&input, 127, 0, 0, 1, 0);
    compare_pcm(&base, &got);
    for (size_t f = 0; f < got.count; f++) assert(!(got.frames[f].flags & FAAD_FRAME_DRC));
    release_decoded(&got); free(input.data);

    bitstream truncated = {{0}, 0};
    const uint8_t increasing[] = {40, 80};
    make_payload(&truncated, 1, 24, -1, increasing, controls, 2, 0);
    truncated.bits = 8;
    input = inject(original, &truncated, 0, 1);
    got = decode(&input, 127, 0, 0, 1, 0);
    compare_pcm(&base, &got);
    release_decoded(&got); free(input.data);
    release_decoded(&base);
}

static void test_oversized(const bytes *original)
{
    bitstream drc = {{0}, 0}, other = {{0}, 0};
    put_bits(&drc, 0xb8, 8); put_bits(&other, 0xa8, 8);
    bytes a = inject(original, &drc, 0, 20), b = inject(original, &other, 0, 20);
    decoded da = decode(&a, 127, 0, 0, 1, 0), db = decode(&b, 127, 0, 0, 1, 0);
    compare_pcm(&da, &db);
    release_decoded(&da); release_decoded(&db); free(a.data); free(b.data);
}

int main(void)
{
    faac_library_info enc = { .struct_size = sizeof(enc) };
    assert(faac_get_library_info(&enc) == FAAC_OK);
    unsigned channels = enc.max_channels >= 2 ? 2 : 1;
    fixture_channels = channels;
    bytes original = encode_fixture(channels, 0);
    test_defaults(&original);
    test_gain(&original);
    test_integer_limiter();
    test_routing_and_order(&original);
    test_frame_flags(&original);
    test_channel_exclusion(&original);
    test_malformed(&original);
    test_oversized(&original);
    test_multiband();
    test_short_window();
    test_normalisation(&original);
    free(original.data);
    return 0;
}
