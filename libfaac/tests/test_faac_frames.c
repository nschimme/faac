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

/* ADTS output contract, read back without the decoder:
 *  - frame_length chains the stream exactly (every frame starts on a sync word and the
 *    lengths sum to the bytes written);
 *  - no raw data block exceeds 6144 bits per channel, the ISO/IEC 14496-3 limit, in any
 *    rate-control mode;
 *  - window_sequence only moves along legal transitions (long -> start -> short -> stop),
 *    and a full-scale click in a tonal background does switch to short blocks. */
#include <stdio.h>
#include "faac.h"
#include "test_faac_pcm.h"

#define RATE 44100
enum { LONG = 0, START = 1, SHORT = 2, STOP = 3 };

typedef struct { uint8_t *data; size_t len; } stream;

/* encode() refuses an output buffer below one maximal ADTS frame, so keep that much spare. */
#define ADTS_SPARE 8192

static stream encode(uint32_t channels, enum faac_rate_control rc, uint32_t bit_rate, uint32_t quality,
                     const int16_t *pcm, uint32_t frames)
{
    faac_params p;
    faac_encoder *enc = NULL;
    faac_encoder_info info = { .struct_size = sizeof(info) };
    stream s = { NULL, 0 };
    FT_REQUIRE(faac_params_init(&p, sizeof(p)) == FAAC_OK);
    p.sample_rate = RATE;
    p.channels = channels;
    p.object_type = FAAC_OBJ_LC;
    p.rate_control = rc;
    p.bit_rate = bit_rate;
    p.quality = quality;
    p.stream_format = FAAC_STREAM_ADTS;
    p.input_format = FAAC_INPUT_16BIT;
    FT_REQUIRE(faac_encoder_open(&p, &enc) == FAAC_OK);
    FT_REQUIRE(faac_encoder_get_info(enc, &info) == FAAC_OK);
    size_t cap = (size_t)info.max_output_bytes * (frames / info.frame_samples + 8) + ADTS_SPARE;
    s.data = (uint8_t *)malloc(cap);
    FT_REQUIRE(s.data);
    for (uint32_t done = 0; done < frames; done += info.frame_samples) {
        uint32_t n = frames - done < info.frame_samples ? frames - done : info.frame_samples, got = 0;
        FT_REQUIRE(faac_encoder_encode(enc, pcm + (size_t)done * channels, n * channels,
                                       s.data + s.len, (uint32_t)(cap - s.len), &got) == FAAC_OK);
        s.len += got;
    }
    for (uint32_t got = 1; got;) {
        FT_REQUIRE(faac_encoder_encode(enc, NULL, 0, s.data + s.len, (uint32_t)(cap - s.len), &got) == FAAC_OK);
        s.len += got;
    }
    FT_REQUIRE(faac_encoder_close(&enc) == FAAC_OK);
    return s;
}

/* Walks the frames; fills window_sequence of each (mono SCE) when seq is non-NULL. */
static unsigned walk(const stream *s, uint32_t channels, int *seq, unsigned max_frames)
{
    size_t pos = 0;
    unsigned n = 0;
    while (pos < s->len) {
        const uint8_t *h = s->data + pos;
        FT_REQUIRE(s->len - pos >= 7);
        FT_REQUIRE(h[0] == 0xFF && (h[1] & 0xF0) == 0xF0);
        size_t header = (h[1] & 1) ? 7 : 9;
        size_t length = ((size_t)(h[3] & 3) << 11) | ((size_t)h[4] << 3) | (h[5] >> 5);
        FT_REQUIRE(length > header && length <= s->len - pos);
        FT_CHECK((length - header) * 8 <= 6144u * channels);
        if (seq) {
            FT_REQUIRE(n < max_frames && length >= header + 3);
            FT_REQUIRE((h[header] >> 5) == 0); /* SCE; ics_info follows global_gain */
            seq[n] = (h[header + 2] >> 6) & 3;
        }
        pos += length;
        n++;
    }
    return n;
}

static void test_ceiling(uint32_t max_channels)
{
    /* Noise at top quality or the highest accepted rate drives frames to the limit (mono VBR
     * and mono ABR 264k land on exactly 6144 bits), so the check can actually fail. */
    static const struct { uint32_t channels; enum faac_rate_control rc; uint32_t bit_rate, quality; int noise; } modes[] = {
        { 1, FAAC_RC_ABR, 128000, 0, 0 }, { 2, FAAC_RC_ABR, 128000, 0, 0 },
        { 2, FAAC_RC_CBR, 128000, 0, 0 }, { 2, FAAC_RC_VBR, 0, 5000, 0 },
        { 1, FAAC_RC_VBR, 0, 5000, 1 }, { 2, FAAC_RC_VBR, 0, 5000, 1 },
        { 1, FAAC_RC_ABR, 264000, 0, 1 }, { 2, FAAC_RC_CBR, 256000, 0, 1 },
    };
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        uint32_t frames = RATE * 2;
        if (modes[i].channels > max_channels) continue; /* build has fewer channels */
        int16_t *pcm = modes[i].noise ? pcm_make_noise(modes[i].channels, frames) : pcm_make(RATE, modes[i].channels, frames);
        stream s = encode(modes[i].channels, modes[i].rc, modes[i].bit_rate, modes[i].quality, pcm, frames);
        FT_CHECK(walk(&s, modes[i].channels, NULL, 0) > 0);
        free(s.data);
        free(pcm);
    }
}

static void test_window_transitions(void)
{
    enum { FRAMES = RATE * 3 };
    int16_t *pcm = pcm_make(RATE, 1, FRAMES);
    stream s = encode(1, FAAC_RC_ABR, 64000, 0, pcm, FRAMES);
    int seq[256];
    unsigned n = walk(&s, 1, seq, 256), shorts = 0;
    int prev = -1; /* the first frame has no overlap partner, so any sequence is legal */
    for (unsigned i = 0; i < n; i++) {
        int ok = prev < 0 ? 1
               : (prev == LONG || prev == STOP) ? (seq[i] == LONG || seq[i] == START)
                                                : (seq[i] == SHORT || seq[i] == STOP);
        if (!ok) fprintf(stderr, "frame %u: window_sequence %d after %d: ", i, seq[i], prev);
        FT_CHECK(ok);
        shorts += seq[i] == SHORT;
        prev = seq[i];
    }
    FT_CHECK(shorts > 0);
    free(s.data);
    free(pcm);
}

static void test_lfe_narrow_bandwidth(void)
{
    enum { CHANNELS = 4 };
    faac_params p;
    faac_encoder *enc = NULL;
    faac_encoder_info info = { .struct_size = sizeof(info) };
    FT_REQUIRE(faac_params_init(&p, sizeof(p)) == FAAC_OK);
    p.sample_rate = 64000;
    p.channels = CHANNELS;
    p.object_type = FAAC_OBJ_LC;
    p.use_lfe = 1;
    p.bit_rate = 68000;
    p.bandwidth = 150;
    p.input_format = FAAC_INPUT_16BIT;
    p.stream_format = FAAC_STREAM_ADTS;
    FT_REQUIRE(faac_encoder_open(&p, &enc) == FAAC_OK);
    FT_REQUIRE(faac_encoder_get_info(enc, &info) == FAAC_OK);

    uint32_t frames = info.frame_samples * 3;
    int16_t *pcm = pcm_make_noise(CHANNELS, frames);
    size_t cap = (size_t)info.max_output_bytes * 8 + ADTS_SPARE;
    uint8_t *out = malloc(cap);
    FT_REQUIRE(pcm && out);
    size_t len = 0;
    for (uint32_t done = 0; done < frames; done += info.frame_samples) {
        uint32_t n = frames - done < info.frame_samples ? frames - done : info.frame_samples;
        uint32_t got = 0;
        FT_REQUIRE(faac_encoder_encode(enc, pcm + (size_t)done * CHANNELS, n * CHANNELS,
                                       out + len, (uint32_t)(cap - len), &got) == FAAC_OK);
        len += got;
    }
    for (uint32_t got = 1; got;) {
        FT_REQUIRE(faac_encoder_encode(enc, NULL, 0, out + len, (uint32_t)(cap - len), &got) == FAAC_OK);
        len += got;
    }
    FT_CHECK(len > 0);
    FT_REQUIRE(faac_encoder_close(&enc) == FAAC_OK);
    free(out);
    free(pcm);
}

int main(void)
{
    faac_library_info lib = { .struct_size = sizeof(lib) };
    FT_REQUIRE(faac_get_library_info(&lib) == FAAC_OK);
    test_ceiling(lib.max_channels);
    test_window_transitions();
    if (lib.max_channels >= 4) test_lfe_narrow_bandwidth();
    return ft_done();
}
