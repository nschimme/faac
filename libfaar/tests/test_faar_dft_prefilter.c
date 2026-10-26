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
#include "dft_coeffs.h"
#include "dft_prefilter.h"
#include "libfaar_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static int drain_direct(faar_dft_prefilter *stage, float *output,
                        size_t frames, size_t channels, size_t capacity,
                        size_t *out_at) {
    while (faar_dft_prefilter_pending(stage)) {
        size_t pending = faar_dft_prefilter_pending(stage);
        size_t count = pending < capacity ? pending : capacity;
        if (count > frames - *out_at)
            return fail("direct prefilter drain exceeds output buffer");
        float *planes[8];
        for (size_t c = 0; c < channels; ++c)
            planes[c] = output + c * frames + *out_at;
        size_t produced = faar_dft_prefilter_drain_planes(stage, planes, count);
        if (produced != count
                || faar_dft_prefilter_pending(stage) != pending - produced)
            return fail("direct prefilter drain count mismatch");
        *out_at += produced;
    }
    return 0;
}

static int run_boundary(size_t tap_count, size_t fft_size,
                        size_t frames, size_t input_chunk,
                        size_t output_capacity,
                        size_t channels, int direct_planes) {
    const size_t CHANNELS = channels;
    double *taps = (double *)malloc(tap_count * sizeof(*taps));
    float *input = (float *)malloc(frames * CHANNELS * sizeof(*input));
    float *output = (float *)malloc(frames * CHANNELS * sizeof(*output));
    float *chunk = (float *)malloc(output_capacity * CHANNELS * sizeof(*chunk));
    if (!taps || !input || !output || !chunk) {
        free(taps); free(input); free(output); free(chunk);
        return fail("test buffers allocation failed");
    }
    for (size_t i = 0; i < frames; ++i)
        for (size_t c = 0; c < CHANNELS; ++c)
            input[i * CHANNELS + c] = (float)(0.2 * sin(0.071 * i + c * 0.4)
                                                   + c * 0.1);
    if (faar_dft_design_lowpass(taps, tap_count, 0.18, 5.0)) {
        free(taps); free(input); free(output); free(chunk);
        return fail("test filter design failed");
    }
    faar_dft_prefilter *stage = faar_dft_prefilter_create_rate(CHANNELS, taps,
            tap_count, fft_size, 1, 1);
    if (!stage) {
        faar_dft_prefilter_destroy(stage);
        free(taps); free(input); free(output); free(chunk);
        return fail("prefilter create failed");
    }
    size_t in_at = 0, out_at = 0;
    while (in_at < frames) {
        size_t count = frames - in_at;
        if (count > input_chunk) count = input_chunk;
        size_t produced = 0;
        if (faar_dft_prefilter_process(stage, input + in_at * CHANNELS,
                count, direct_planes ? NULL : chunk,
                direct_planes ? 0 : output_capacity, &produced)) {
            faar_dft_prefilter_destroy(stage);
            free(taps); free(input); free(output); free(chunk);
            return fail("prefilter process failed");
        }
        if (direct_planes) {
            if (produced || drain_direct(stage, output, frames, CHANNELS,
                                         output_capacity, &out_at)) {
                faar_dft_prefilter_destroy(stage);
                free(taps); free(input); free(output); free(chunk);
                return fail("direct prefilter process drain failed");
            }
        } else {
            for (size_t c = 0; c < CHANNELS; ++c)
                memcpy(output + c * frames + out_at,
                       chunk + c * produced, produced * sizeof(*output));
            out_at += produced;
        }
        in_at += count;
    }
    for (;;) {
        size_t produced = 0;
        if (faar_dft_prefilter_flush(stage, direct_planes ? NULL : chunk,
                direct_planes ? 0 : output_capacity, &produced)) {
            faar_dft_prefilter_destroy(stage);
            free(taps); free(input); free(output); free(chunk);
            return fail("prefilter flush failed");
        }
        if (direct_planes) {
            if (produced || drain_direct(stage, output, frames, CHANNELS,
                                         output_capacity, &out_at)) {
                faar_dft_prefilter_destroy(stage);
                free(taps); free(input); free(output); free(chunk);
                return fail("direct prefilter flush drain failed");
            }
        } else {
            for (size_t c = 0; c < CHANNELS; ++c)
                memcpy(output + c * frames + out_at,
                       chunk + c * produced, produced * sizeof(*output));
            out_at += produced;
        }
        if (direct_planes && !faar_dft_prefilter_pending(stage)) break;
        if (!produced) break;
    }
    if (out_at != frames) {
        faar_dft_prefilter_destroy(stage);
        free(taps); free(input); free(output); free(chunk);
        return fail("prefilter output count differs from input count");
    }
    for (size_t n = 0; n < frames; ++n) {
        for (size_t c = 0; c < CHANNELS; ++c) {
            double expected = 0.0;
            for (size_t k = 0; k < tap_count; ++k) {
                long index = (long)n + (long)(tap_count - 1) / 2 - (long)k;
                double sample;
                if (index < 0)
                    sample = input[c];
                else if ((size_t)index >= frames)
                    sample = input[(frames - 1) * CHANNELS + c];
                else sample = input[(size_t)index * CHANNELS + c];
                expected += taps[k] * sample;
            }
            if (fabs(output[c * frames + n] - expected) > (sizeof(faar_sample_t) == sizeof(float) ? 2e-6 : 2e-7)) {
                fprintf(stderr, "frame %zu channel %zu\n", n, c);
                faar_dft_prefilter_destroy(stage);
                free(taps); free(input); free(output); free(chunk);
                return fail("centered prefilter output differs from direct convolution");
            }
        }
    }
    size_t produced = 1;
    if (faar_dft_prefilter_flush(stage, direct_planes ? NULL : chunk,
            direct_planes ? 0 : output_capacity, &produced) || produced
            || faar_dft_prefilter_pending(stage)) {
        faar_dft_prefilter_destroy(stage);
        free(taps); free(input); free(output); free(chunk);
        return fail("repeated prefilter flush was not empty");
    }
    faar_dft_prefilter_reset(stage);
    faar_dft_prefilter_destroy(stage);
    free(taps); free(input); free(output); free(chunk);
    return 0;
}

int main(void) {
    /* With float pairs, one channel stacks blocks, two share a transform and
     * three make a pair plus a lone lane. */
        for (size_t channels = 1; channels <= 3; ++channels)
            for (int direct = 0; direct <= 1; ++direct)
                if (run_boundary(31, 128, 257, 17, 7, channels, direct)
                        || run_boundary(601, 4096, 4000, 257, 97, channels, direct))
                    return 1;
    puts("DFT prefilter: centered alignment, short/601-tap plans, packed/direct multi-channel isolation, hold boundaries, chunking and drain passed");
    return 0;
}
