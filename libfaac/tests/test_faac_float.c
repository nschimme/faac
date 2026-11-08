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

/* FAAC_INPUT_FLOAT is unity full scale, so a 16-bit sample divided by 32768 must encode
 * exactly like the 16-bit sample itself. NaN, infinity and magnitudes of 256 or more are
 * not audio and must encode as silence.
 * FAAC_INPUT_24BIT and FAAC_INPUT_24IN32 scale by 256 relative to 16-bit PCM,
 * so (sample << 8) must encode bit-identically across 16-bit, 24-bit, 24-in-32, and float. */
#include <stdio.h>
#include <string.h>
#include "faac.h"
#include "endian.h"
#include "test_faac_pcm.h"

#define RATE 44100
#define CHANNELS 1

/* Returns the stream length; *out is heap-allocated. */
static size_t encode(enum faac_input_format format, const void *pcm, size_t sample_size,
                     uint32_t frames, uint8_t **out)
{
    faac_params p;
    faac_encoder *enc = NULL;
    faac_encoder_info info = { .struct_size = sizeof(info) };
    FT_REQUIRE(faac_params_init(&p, sizeof(p)) == FAAC_OK);
    p.sample_rate = RATE;
    p.channels = CHANNELS;
    p.object_type = FAAC_OBJ_LC;
    p.bit_rate = 64000 * CHANNELS;
    p.stream_format = FAAC_STREAM_ADTS;
    p.input_format = format;
    FT_REQUIRE(faac_encoder_open(&p, &enc) == FAAC_OK);
    FT_REQUIRE(faac_encoder_get_info(enc, &info) == FAAC_OK);

    size_t cap = (size_t)info.max_output_bytes * (frames / info.frame_samples + 8) + 8192, len = 0;
    uint8_t *stream = (uint8_t *)malloc(cap);
    FT_REQUIRE(stream);

    for (uint32_t done = 0; done < frames;) {
        uint32_t n = frames - done < 1024 ? frames - done : 1024;
        uint32_t got = 0;
        FT_REQUIRE(faac_encoder_encode(enc, (const uint8_t *)pcm + (size_t)done * CHANNELS * sample_size,
                                       n * CHANNELS, stream + len, (uint32_t)(cap - len), &got) == FAAC_OK);
        len += got;
        done += n;
    }
    for (uint32_t got = 1; got;) {
        FT_REQUIRE(faac_encoder_encode(enc, NULL, 0, stream + len, (uint32_t)(cap - len), &got) == FAAC_OK);
        len += got;
    }
    FT_REQUIRE(faac_encoder_close(&enc) == FAAC_OK);
    *out = stream;
    return len;
}

static void expect_same(const char *what, const uint8_t *a, size_t alen, const uint8_t *b, size_t blen)
{
    if (alen != blen) fprintf(stderr, "%s: ", what);
    FT_CHECK_INT(alen, blen);
    if (alen == blen) {
        if (memcmp(a, b, alen)) fprintf(stderr, "%s: ", what);
        FT_CHECK_MEM(a, b, alen);
    }
}

int main(void)
{
    uint32_t frames = RATE * 3 / 2 + 5;
    size_t n = (size_t)frames * CHANNELS;
    int16_t *pcm = pcm_make(RATE, CHANNELS, frames);
    float *unity = (float *)malloc(n * sizeof(*unity));
    float *silent = (float *)malloc(n * sizeof(*silent));
    int16_t *zeros = (int16_t *)calloc(n, sizeof(*zeros));
    uint8_t *pcm24 = (uint8_t *)malloc(n * 3);
    int32_t *pcm24in32 = (int32_t *)malloc(n * sizeof(*pcm24in32));
    FT_REQUIRE(unity && silent && zeros && pcm24 && pcm24in32);
    for (size_t i = 0; i < n; i++) {
        unity[i] = (float)pcm[i] / 32768.0f;
        int32_t s24 = (int32_t)pcm[i] * 256;
        write_pcm24(pcm24 + i * 3, s24, FAAC_BIG_ENDIAN);
        pcm24in32[i] = s24;
        /* Old-scale samples, NaN and infinity cycle through the buffer. */
        switch (i % 4) {
            case 0: silent[i] = (float)pcm[i] + ((pcm[i] < 0) ? -256.0f : 256.0f); break;
            case 1: silent[i] = NAN; break;
            case 2: silent[i] = (i & 4) ? INFINITY : -INFINITY; break;
            default: silent[i] = 8388608.0f; break;
        }
    }

    uint8_t *as_int = NULL, *as_float = NULL, *as_zero = NULL, *as_silent = NULL;
    uint8_t *as_24 = NULL, *as_24in32 = NULL;
    size_t int_len = encode(FAAC_INPUT_16BIT, pcm, sizeof(*pcm), frames, &as_int);
    size_t float_len = encode(FAAC_INPUT_FLOAT, unity, sizeof(*unity), frames, &as_float);
    size_t zero_len = encode(FAAC_INPUT_16BIT, zeros, sizeof(*zeros), frames, &as_zero);
    size_t silent_len = encode(FAAC_INPUT_FLOAT, silent, sizeof(*silent), frames, &as_silent);
    size_t len_24 = encode(FAAC_INPUT_24BIT, pcm24, 3, frames, &as_24);
    size_t len_24in32 = encode(FAAC_INPUT_24IN32, pcm24in32, sizeof(*pcm24in32), frames, &as_24in32);

    FT_CHECK(int_len > 0);
    expect_same("unity float vs 16-bit", as_float, float_len, as_int, int_len);
    expect_same("24-bit packed vs 16-bit", as_24, len_24, as_int, int_len);
    expect_same("24-in-32 vs 16-bit", as_24in32, len_24in32, as_int, int_len);
    expect_same("non-audio float vs silence", as_silent, silent_len, as_zero, zero_len);
    FT_CHECK(int_len != zero_len || memcmp(as_int, as_zero, int_len)); /* the signal is not silent */

    free(as_int); free(as_float); free(as_zero); free(as_silent);
    free(as_24); free(as_24in32);
    free(pcm24); free(pcm24in32);
    free(pcm); free(unity); free(silent); free(zeros);
    return ft_done();
}
