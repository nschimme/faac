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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#undef NDEBUG
#include <assert.h>
#include <faar.h>
#include "faar_test_api.h"

#ifdef FAAR_FLOAT
#define TEST_QUALITY FAAR_QUALITY_GOOD
#else
#define TEST_QUALITY FAAR_QUALITY_HIGH
#endif

int main(void) {
    const uint16_t channels = MAX_CHANNELS < 2 ? MAX_CHANNELS : 2;
    printf("Testing libfaar API...\n");
#ifdef FAAR_FLOAT
    static const uint32_t rates[][2] = {
        {96000, 44100}, {44100, 48000}, {48000, 48000}
    };
    for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) {
        assert(faar_test_create(rates[i][0], rates[i][1], channels,
                           FAAR_QUALITY_HIGH) == NULL);
        assert(faar_test_create_ext(rates[i][0], rates[i][1], channels,
                               FAAR_QUALITY_HIGH, 2) == NULL);
    }
#endif

    uint32_t in_rate = 96000;
    uint32_t out_rate = 44100;

    faar_test_handle * r = faar_test_create(in_rate, out_rate, channels, FAAR_QUALITY_GOOD);
    assert(r != NULL);

    size_t delay = faar_test_delay(r);
    printf("Resampler delay: %zu samples\n", delay);

    size_t in_frames = 1024;
    float *in_buf = (float *)calloc(in_frames * channels, sizeof(float));
    /* Fill with 1 kHz sine wave */
    for (size_t i = 0; i < in_frames; i++) {
        float val = (float)sin(2.0 * 3.141592653589793 * 1000.0 * i / in_rate);
        for (uint16_t c = 0; c < channels; ++c)
            in_buf[i * channels + c] = val;
    }

    size_t out_cap = 2048;
    float *out_buf = (float *)calloc(out_cap * channels, sizeof(float));

    size_t produced = faar_test_process(r, in_buf, in_frames, out_buf, out_cap);
    printf("Processed %zu input frames -> %zu output frames\n", in_frames, produced);
    size_t total = produced;
    size_t flushed;
    do {
        flushed = faar_test_flush(r, out_buf + total * channels, out_cap - total);
        total += flushed;
    } while (flushed && total < out_cap);
    printf("Flushed %zu additional frames\n", total - produced);
    assert(total == (in_frames * (size_t)out_rate + in_rate - 1) / in_rate);

    faar_test_close(r);

    /* Test Dithering Modes (TPDF & SHAPED for 16-bit and 24-bit) */
    printf("Testing libfaar dithering modes...\n");
    faar_dither_t test_dithers[] = {FAAR_DITHER_OFF, FAAR_DITHER_TPDF, FAAR_DITHER_SHAPED};
    uint8_t test_bits[] = {16, 24};

    for (size_t d = 0; d < 3; d++) {
        for (size_t b = 0; b < 2; b++) {
            faar_test_handle * r_dither = faar_test_create(in_rate, out_rate, channels, TEST_QUALITY);
            assert(r_dither != NULL);
            if (test_dithers[d] != FAAR_DITHER_OFF) {
                int status = faar_test_set_dither(r_dither, test_dithers[d], test_bits[b]);
                assert(status == 0);
            }

            size_t d_produced = faar_test_process(r_dither, in_buf, in_frames, out_buf, out_cap);
            size_t d_flushed = faar_test_flush(r_dither, out_buf + d_produced * channels, out_cap - d_produced);
            assert(d_produced + d_flushed > 0);

            faar_test_close(r_dither);
        }
    }

    free(in_buf);
    free(out_buf);

    printf("libfaar test passed successfully!\n");
    return 0;
}
