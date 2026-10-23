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
#include "dft_fir.h"
#include "dft_coeffs.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846264338327950288
#endif

static int fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static long double bessel_i0_long_double(long double x) {
    long double half = x * 0.5L, sum = 1.0L, term = 1.0L;
    for (unsigned k = 1; k < 512; ++k) {
        term *= (half / k) * (half / k);
        sum += term;
        if (term <= sum * 1.0e-19L) break;
    }
    return sum;
}

static int test_designed_filter(void) {
    enum { N = 601 };
    double taps[N], normalized_sum = 0.0;
    const double cutoff = 0.229, beta = 16.0;
    if (faar_dft_design_lowpass(taps, N, cutoff, beta))
        return fail("Kaiser coefficient design failed");
    for (size_t i = 0; i < N; ++i) {
        normalized_sum += taps[i];
        if (fabs(taps[i] - taps[N - 1 - i]) > 1e-16)
            return fail("Kaiser coefficients are not symmetric");
    }
    if (fabs(normalized_sum - 1.0) > 2e-13)
        return fail("Kaiser coefficients are not unity-DC normalized");

    /* Independent long-double probes of the analytic windowed-sinc formula. */
    static const size_t indices[] = {0, 17, 93, 251, 300};
    const long double center = (N - 1) * 0.5L;
    const long double denom = bessel_i0_long_double(beta);
    long double raw_sum = 0.0L;
    for (size_t i = 0; i < N; ++i) {
        long double x = (long double)i - center;
        long double ideal = x == 0.0L ? 2.0L * cutoff
            : sinl(2.0L * acosl(-1.0L) * cutoff * x)
              / (acosl(-1.0L) * x);
        long double pos = x / center;
        raw_sum += ideal * bessel_i0_long_double(beta * sqrtl(1.0L-pos*pos)) / denom;
    }
    for (size_t q = 0; q < sizeof(indices)/sizeof(indices[0]); ++q) {
        const size_t i = indices[q];
        long double x = (long double)i - center;
        long double ideal = x == 0.0L ? 2.0L * cutoff
            : sinl(2.0L * acosl(-1.0L) * cutoff * x)
              / (acosl(-1.0L) * x);
        long double pos = x / center;
        long double win = bessel_i0_long_double(beta * sqrtl(1.0L-pos*pos)) / denom;
        if (fabsl((long double)taps[i] - ideal * win / raw_sum) > 2e-15L)
            return fail("Kaiser coefficient differs from long-double calculation");
    }

    static const double freqs[] = {0.20, 0.2083333333333333, 0.22, 0.24};
    double gains[sizeof(freqs)/sizeof(freqs[0])];
    for (size_t p = 0; p < sizeof(freqs)/sizeof(freqs[0]); ++p) {
        double re = 0.0, im = 0.0;
        for (size_t i = 0; i < N; ++i) {
            double angle = -2.0 * M_PI * freqs[p] * ((double)i-center);
            re += taps[i] * cos(angle);
            im += taps[i] * sin(angle);
        }
        gains[p] = 20.0 * log10(hypot(re, im));
    }
    if (fabs(gains[0]) > 0.001 || fabs(gains[1]) > 0.001
            || fabs(gains[2]) > 0.001 || gains[3] > -140.0) {
        fprintf(stderr, "response dB: %.6f %.6f %.6f %.6f\n",
                gains[0], gains[1], gains[2], gains[3]);
        return fail("Kaiser prefilter passband/stopband response failed");
    }
    return 0;
}

static int test_32_to_48_high_filter(void) {
    enum { N = 4097 };
    const long double cutoff = 0.24889387661163931L;
    const long double beta = 19.078143572239966L;
    double *taps = (double *)malloc(N * sizeof(*taps));
    if (!taps) return fail("32-to-48 High coefficient allocation failed");
    if (faar_dft_design_lowpass(taps, N, (double)cutoff, (double)beta)) {
        free(taps);
        return fail("32-to-48 High coefficient design failed");
    }

    const long double center = (N - 1) * 0.5L;
    const long double pi = acosl(-1.0L);
    const long double window_denominator = bessel_i0_long_double(beta);
    long double raw_sum = 0.0L;
    for (size_t i = 0; i < N; ++i) {
        long double x = (long double)i - center;
        long double ideal = x == 0.0L ? 2.0L * cutoff
            : sinl(2.0L * pi * cutoff * x) / (pi * x);
        long double position = x / center;
        raw_sum += ideal * bessel_i0_long_double(
                beta * sqrtl(1.0L - position * position))
            / window_denominator;
    }
    static const size_t indices[] = {0, 257, 1024, 2048, 3072, 4096};
    for (size_t q = 0; q < sizeof(indices) / sizeof(indices[0]); ++q) {
        size_t i = indices[q];
        long double x = (long double)i - center;
        long double ideal = x == 0.0L ? 2.0L * cutoff
            : sinl(2.0L * pi * cutoff * x) / (pi * x);
        long double position = x / center;
        long double expected = ideal * bessel_i0_long_double(
                beta * sqrtl(1.0L - position * position))
            / window_denominator / raw_sum;
        if (fabsl((long double)taps[i] - expected) > 2e-15L) {
            free(taps);
            return fail("32-to-48 High coefficient differs from long-double oracle");
        }
    }
    free(taps);
    puts("DFT FIR: 4097-tap 32-to-48 High coefficients match long-double oracle");
    return 0;
}

/* Stream `lanes` channels (or one stacked channel) through the paired stage
 * against explicit zero insertion and double convolution; they agree to the
 * paired precision's roundoff. */
static int check_paired(size_t expansion, size_t decimation, size_t lanes,
                        int stacked) {
    enum { TAPS = 13, FFT = 128, FRAMES = 700, CHUNK = 37 };
    double taps[TAPS];
    faar_sample_t in[2][FRAMES], planes[2 * CHUNK];
    faar_sample_t out[2][FRAMES * 2 + FFT];
    double ref[2][FRAMES * 2 + FFT];
    size_t count[2] = {0, 0};
    faar_dft_fir paired;
    const size_t cap = FFT * 2;
    faar_sample_t *buf = malloc(2 * cap * sizeof(*buf));
    size_t used = 0, produced = 0;
    for (size_t j = 0; j < TAPS; ++j) {
        double x = (double)j - (TAPS - 1) / 2.0;
        taps[j] = (x == 0.0 ? 0.3 : sin(0.3 * M_PI * x) / (M_PI * x))
            * (0.54 - 0.46 * cos(2.0 * M_PI * j / (TAPS - 1)));
        taps[j] *= (double)expansion;
    }
    for (size_t l = 0; l < 2; ++l)
        for (size_t i = 0; i < FRAMES; ++i)
            in[l][i] = (0.3 + 0.2 * l) * sin(0.05 * (1 + l) * i) + 0.1 * cos(0.31 * i);
    if (!buf || faar_dft_fir_init_paired(&paired, taps, TAPS, FFT, expansion,
                                         decimation, lanes, stacked))
        return fail("paired FIR init failed");
    for (size_t at = 0; at < FRAMES; at += CHUNK) {
        size_t n = FRAMES - at < CHUNK ? FRAMES - at : CHUNK, eaten = 0;
        for (size_t l = 0; l < lanes; ++l)
            for (size_t i = 0; i < n; ++i) planes[l * n + i] = in[l][at + i];
        if (faar_dft_fir_stream(&paired, planes, n, &eaten, buf, cap, &produced)
                || eaten != n) return fail("paired FIR stream failed");
        for (size_t l = 0; l < lanes; ++l)
            for (size_t i = 0; i < produced; ++i) out[l][count[l]++] = buf[l * cap + i];
    }
    do {
        if (faar_dft_fir_flush(&paired, buf, cap, &produced))
            return fail("paired FIR flush failed");
        for (size_t l = 0; l < lanes; ++l)
            for (size_t i = 0; i < produced; ++i) out[l][count[l]++] = buf[l * cap + i];
    } while (produced);
    faar_dft_fir_clear(&paired);
    for (size_t l = 0; l < lanes; ++l) {
        used = (FRAMES * expansion + decimation - 1) / decimation;
        for (size_t i = 0; i < used; ++i) {
            size_t n = i * decimation;
            double sum = 0.0;
            for (size_t k = 0; k < TAPS && k <= n; ++k)
                if ((n - k) % expansion == 0
                        && (n - k) / expansion < FRAMES)
                    sum += taps[k] * in[l][(n - k) / expansion];
            ref[l][i] = sum;
        }
        if (used != count[l]) return fail("paired FIR output count differs");
        for (size_t i = 0; i < used; ++i)
            if (fabs(ref[l][i] - out[l][i]) > (sizeof(faar_sample_t) == sizeof(float) ? 2e-6 : 2e-14))
                return fail("paired FIR differs from direct double convolution");
    }
    free(buf);
    return 0;
}

static int test_paired(void) {
    static const size_t rates[][2] = {{1, 1}, {2, 1}, {1, 2}, {1, 4}, {2, 2}, {2, 4}};
        for (size_t r = 0; r < sizeof(rates) / sizeof(rates[0]); ++r)
            for (size_t mode = 0; mode < 3; ++mode)
                if (check_paired(rates[r][0], rates[r][1], mode == 1 ? 2 : 1,
                                 mode == 2)) return 1;
    puts("DFT FIR: paired lanes match direct double convolution for every rate");
    return 0;
}

int main(void) {
    if (test_designed_filter()) return 1;
    if (test_32_to_48_high_filter()) return 1;
    if (test_paired()) return 1;
    enum { TAPS = 31, FFT_SIZE = 128, BLOCK = FFT_SIZE - TAPS + 1,
           BLOCKS = 9 };
    double taps[TAPS];
    faar_sample_t input[BLOCK * BLOCKS], output[BLOCK * BLOCKS];
    faar_dft_fir stage;

    for (size_t j = 0; j < TAPS; ++j) {
        double x = (double)j - (TAPS - 1) / 2.0;
        double sinc = x == 0.0 ? 0.31 : sin(0.31 * M_PI * x) / (M_PI * x);
        double window = 0.54 - 0.46 * cos(2.0 * M_PI * j / (TAPS - 1));
        taps[j] = sinc * window;
    }
    for (size_t i = 0; i < BLOCK * BLOCKS; ++i)
        input[i] = 0.3 * sin(0.071 * i) + 0.2 * cos(0.193 * i);

    if (faar_dft_fir_init(&stage, taps, TAPS, FFT_SIZE))
        return fail("DFT FIR init failed");
    if (stage.block_size != BLOCK) {
        faar_dft_fir_clear(&stage);
        return fail("DFT FIR block geometry mismatch");
    }
    for (size_t b = 0; b < BLOCKS; ++b) {
        if (faar_dft_fir_process(&stage, input + b * BLOCK,
                                 output + b * BLOCK)) {
            faar_dft_fir_clear(&stage);
            return fail("DFT FIR block processing failed");
        }
        if (b == 0) {
            for (size_t k = 0; k < TAPS - 1; ++k) {
                if (stage.overlap[k] != input[BLOCK - (TAPS - 1) + k]) {
                    faar_dft_fir_clear(&stage);
                    return fail("overlap state does not retain the previous block tail");
                }
            }
        }
    }
    faar_dft_fir_clear(&stage);

    for (size_t n = 0; n < BLOCK * BLOCKS; ++n) {
        double expected = 0.0;
        for (size_t k = 0; k < TAPS && k <= n; ++k)
            expected += taps[k] * input[n - k];
        if (fabs(output[n] - expected) > (sizeof(faar_sample_t) == sizeof(float) ? 2e-6 : 2e-12)) {
            fprintf(stderr, "sample %zu: got %.17g expected %.17g\n",
                    n, output[n], expected);
            return fail("overlap-save differs from direct FIR convolution");
        }
    }
    puts("DFT FIR: nine block overlap-save outputs match direct convolution");

    /* Exercise the actual chunk/backpressure wrapper on a partial final block. */
    enum { STREAM_N = 503, CHUNK = 17, CAPACITY = 11 };
    faar_sample_t stream_input[STREAM_N], stream_output[STREAM_N];
    for (size_t i = 0; i < STREAM_N; ++i)
        stream_input[i] = 0.25 * sin(0.037 * i) + 0.1 * cos(0.113 * i);
    if (faar_dft_fir_init(&stage, taps, TAPS, FFT_SIZE))
        return fail("streaming FIR init failed");
    size_t input_offset = 0, output_offset = 0;
    while (input_offset < STREAM_N) {
        size_t request = STREAM_N - input_offset;
        if (request > CHUNK) request = CHUNK;
        size_t consumed = 0, produced = 0;
        if (faar_dft_fir_stream(&stage, stream_input + input_offset, request,
                                &consumed, stream_output + output_offset,
                                CAPACITY, &produced)) {
            faar_dft_fir_clear(&stage);
            return fail("streaming FIR process failed");
        }
        input_offset += consumed;
        output_offset += produced;
        if (!consumed && !produced) {
            faar_dft_fir_clear(&stage);
            return fail("streaming FIR made no progress");
        }
    }
    for (;;) {
        size_t produced = 0;
        if (faar_dft_fir_flush(&stage, stream_output + output_offset,
                               CAPACITY, &produced)) {
            faar_dft_fir_clear(&stage);
            return fail("streaming FIR flush failed");
        }
        output_offset += produced;
        if (!produced) break;
    }
    if (output_offset != STREAM_N) {
        faar_dft_fir_clear(&stage);
        return fail("streaming FIR output count mismatch");
    }
    for (size_t n = 0; n < STREAM_N; ++n) {
        double expected = 0.0;
        for (size_t k = 0; k < TAPS && k <= n; ++k)
            expected += taps[k] * stream_input[n-k];
        if (fabs(stream_output[n] - expected) > (sizeof(faar_sample_t) == sizeof(float) ? 2e-6 : 2e-12)) {
            faar_dft_fir_clear(&stage);
            return fail("streaming FIR differs from direct causal convolution");
        }
    }
    faar_dft_fir_reset(&stage);
    if (stage.input_fill || stage.output_count || stage.finishing || stage.finished) {
        faar_dft_fir_clear(&stage);
        return fail("streaming FIR reset did not clear stream state");
    }
    input_offset = output_offset = 0;
    size_t consumed = 0, produced = 0;
    if (faar_dft_fir_stream(&stage, stream_input, STREAM_N, &consumed,
                            stream_output + output_offset, STREAM_N, &produced)
            || consumed != STREAM_N) {
        faar_dft_fir_clear(&stage);
        return fail("reset FIR stream did not consume full input");
    }
    output_offset += produced;
    do {
        produced = 0;
        if (faar_dft_fir_flush(&stage, stream_output + output_offset,
                               STREAM_N - output_offset, &produced)) {
            faar_dft_fir_clear(&stage);
            return fail("reset FIR flush failed");
        }
        output_offset += produced;
    } while (produced);
    if (output_offset != STREAM_N) {
        faar_dft_fir_clear(&stage);
        return fail("reset FIR stream output count mismatch");
    }
    for (size_t n = 0; n < STREAM_N; ++n) {
        double expected = 0.0;
        for (size_t k = 0; k < TAPS && k <= n; ++k)
            expected += taps[k] * stream_input[n-k];
        if (fabs(stream_output[n] - expected) > (sizeof(faar_sample_t) == sizeof(float) ? 2e-6 : 2e-12)) {
            faar_dft_fir_clear(&stage);
            return fail("reset FIR output depends on prior stream");
        }
    }
    faar_dft_fir_clear(&stage);

    /* A factor-two interpolating transform is compared against literal
       zero insertion followed by the direct time-domain convolution. */
    enum { UPSAMPLE_N = 503 };
    faar_sample_t upsample_input[UPSAMPLE_N], upsample_output[2 * UPSAMPLE_N];
    faar_sample_t expanded[2 * UPSAMPLE_N];
    for (size_t i = 0; i < UPSAMPLE_N; ++i) {
        upsample_input[i] = 0.2 * sin(0.041 * i) + 0.1 * cos(0.17 * i);
        expanded[2 * i] = upsample_input[i];
        expanded[2 * i + 1] = 0.0;
    }
    if (faar_dft_fir_init_expanded(&stage, taps, TAPS, FFT_SIZE, 2))
        return fail("factor-two DFT FIR init failed");
    size_t up_in = 0, up_out = 0;
    while (up_in < UPSAMPLE_N) {
        size_t request = UPSAMPLE_N - up_in;
        if (request > CHUNK) request = CHUNK;
        size_t consumed = 0, produced = 0;
        if (faar_dft_fir_stream(&stage, upsample_input + up_in, request,
                    &consumed, upsample_output + up_out, CAPACITY, &produced)) {
            faar_dft_fir_clear(&stage);
            return fail("factor-two FIR process failed");
        }
        up_in += consumed;
        up_out += produced;
        if (!consumed && !produced) {
            faar_dft_fir_clear(&stage);
            return fail("factor-two FIR made no progress");
        }
    }
    for (;;) {
        size_t produced = 0;
        if (faar_dft_fir_flush(&stage, upsample_output + up_out,
                               2 * UPSAMPLE_N - up_out, &produced)) {
            faar_dft_fir_clear(&stage);
            return fail("factor-two FIR flush failed");
        }
        up_out += produced;
        if (!produced) break;
    }
    if (up_out != 2 * UPSAMPLE_N) {
        faar_dft_fir_clear(&stage);
        return fail("factor-two FIR output count mismatch");
    }
    faar_dft_fir_clear(&stage);
    for (size_t n = 0; n < 2 * UPSAMPLE_N; ++n) {
        double expected = 0.0;
        for (size_t k = 0; k < TAPS && k <= n; ++k)
            expected += taps[k] * expanded[n-k];
        if (fabs(upsample_output[n] - expected) > (sizeof(faar_sample_t) == sizeof(float) ? 2e-6 : 2e-12)) {
            fprintf(stderr, "expanded sample %zu: got %.17g expected %.17g\n",
                    n, upsample_output[n], expected);
            return fail("frequency-domain factor-two expansion differs from direct convolution");
        }
    }

    /* The half-rate path folds the product spectrum and uses an N/2 inverse
       transform. Compare it to every second sample from the full N inverse. */
    faar_sample_t half_rate_output[BLOCKS * BLOCK / 2];
    faar_dft_fir full_rate, half_rate;
    if (faar_dft_fir_init(&full_rate, taps, TAPS, FFT_SIZE)
            || faar_dft_fir_init_rate(&half_rate, taps, TAPS, FFT_SIZE,
                                      1, 2))
        return fail("half-rate FIR init failed");
    if (half_rate.output_block_size != BLOCK / 2) {
        faar_dft_fir_clear(&full_rate);
        faar_dft_fir_clear(&half_rate);
        return fail("half-rate FIR output block geometry mismatch");
    }
    for (size_t b = 0; b < BLOCKS; ++b) {
        if (faar_dft_fir_process(&full_rate, input + b * BLOCK,
                                 output + b * BLOCK)
                || faar_dft_fir_process(&half_rate, input + b * BLOCK,
                                        half_rate_output + b * BLOCK / 2)) {
            faar_dft_fir_clear(&full_rate);
            faar_dft_fir_clear(&half_rate);
            return fail("half-rate FIR block processing failed");
        }
        for (size_t i = 0; i < BLOCK / 2; ++i) {
            if (fabs(half_rate_output[b * BLOCK / 2 + i]
                        - output[b * BLOCK + 2 * i]) > (sizeof(faar_sample_t) == sizeof(float) ? 2e-6 : 2e-12)) {
                fprintf(stderr, "half-rate block %zu sample %zu got %.17g even %.17g odd %.17g\n",
                        b, i, half_rate_output[b * BLOCK / 2 + i],
                        output[b * BLOCK + 2 * i], output[b * BLOCK + 2 * i + 1]);
                faar_dft_fir_clear(&full_rate);
                faar_dft_fir_clear(&half_rate);
                return fail("folded inverse differs from full inverse decimation");
            }
        }
    }
    faar_dft_fir_clear(&full_rate);
    faar_dft_fir_clear(&half_rate);

    enum { HALF_STREAM_N = 503 };
    faar_sample_t half_stream_input[HALF_STREAM_N], half_stream_output[(HALF_STREAM_N+1)/2];
    for (size_t i = 0; i < HALF_STREAM_N; ++i)
        half_stream_input[i] = 0.25 * sin(0.037 * i) + 0.1 * cos(0.113 * i);
    if (faar_dft_fir_init_rate(&stage, taps, TAPS, FFT_SIZE, 1, 2))
        return fail("half-rate streaming FIR init failed");
    size_t half_in = 0, half_out = 0;
    while (half_in < HALF_STREAM_N) {
        size_t request = HALF_STREAM_N - half_in;
        if (request > CHUNK) request = CHUNK;
        size_t consumed = 0, produced = 0;
        if (faar_dft_fir_stream(&stage, half_stream_input + half_in, request,
                    &consumed, half_stream_output + half_out,
                    CAPACITY, &produced)) {
            faar_dft_fir_clear(&stage);
            return fail("half-rate streaming FIR process failed");
        }
        half_in += consumed;
        half_out += produced;
        if (!consumed && !produced) {
            faar_dft_fir_clear(&stage);
            return fail("half-rate streaming FIR made no progress");
        }
    }
    for (;;) {
        size_t produced = 0;
        if (faar_dft_fir_flush(&stage, half_stream_output + half_out,
                    (HALF_STREAM_N + 1) / 2 - half_out, &produced)) {
            faar_dft_fir_clear(&stage);
            return fail("half-rate streaming FIR flush failed");
        }
        half_out += produced;
        if (!produced) break;
    }
    if (half_out != (HALF_STREAM_N + 1) / 2) {
        faar_dft_fir_clear(&stage);
        return fail("half-rate streaming FIR output count mismatch");
    }
    for (size_t m = 0; m < half_out; ++m) {
        size_t n = 2 * m;
        double expected = 0.0;
        for (size_t k = 0; k < TAPS && k <= n; ++k)
            expected += taps[k] * half_stream_input[n-k];
        if (fabs(half_stream_output[m] - expected) > (sizeof(faar_sample_t) == sizeof(float) ? 2e-6 : 2e-12)) {
            faar_dft_fir_clear(&stage);
            return fail("half-rate streaming differs from direct decimated convolution");
        }
    }
    faar_dft_fir_clear(&stage);

    /* Factor-four folding is used by the 192->48 kHz High plan. Check the
       packed N/4 inverse against a full inverse and streamed convolution. */
    enum { TAPS4 = 13, FFT4 = 128, BLOCK4 = FFT4 - TAPS4 + 1,
           BLOCKS4 = 4, STREAM4 = 503 };
    double taps4[TAPS4];
    faar_sample_t input4[BLOCK4 * BLOCKS4];
    faar_sample_t full4_output[BLOCK4], quarter4_output[BLOCK4 / 4];
    for (size_t j = 0; j < TAPS4; ++j) {
        double x = (double)j - (TAPS4 - 1) / 2.0;
        double sinc = x == 0.0 ? 0.24 : sin(0.24 * M_PI * x) / (M_PI * x);
        double window = 0.54 - 0.46 * cos(2.0 * M_PI * j / (TAPS4 - 1));
        taps4[j] = sinc * window;
    }
    for (size_t i = 0; i < BLOCK4 * BLOCKS4; ++i)
        input4[i] = 0.2 * sin(0.043 * i) - 0.1 * cos(0.127 * i);
    faar_dft_fir full4, quarter4;
    if (faar_dft_fir_init(&full4, taps4, TAPS4, FFT4)
            || faar_dft_fir_init_rate(&quarter4, taps4, TAPS4, FFT4,
                                      1, 4))
        return fail("quarter-rate FIR init failed");
    for (size_t b = 0; b < BLOCKS4; ++b) {
        if (faar_dft_fir_process(&full4, input4 + b * BLOCK4,
                                 full4_output)
                || faar_dft_fir_process(&quarter4, input4 + b * BLOCK4,
                                        quarter4_output)) {
            faar_dft_fir_clear(&full4);
            faar_dft_fir_clear(&quarter4);
            return fail("quarter-rate FIR block processing failed");
        }
        for (size_t i = 0; i < BLOCK4 / 4; ++i) {
            if (fabs(quarter4_output[i] - full4_output[4 * i]) > (sizeof(faar_sample_t) == sizeof(float) ? 2e-6 : 2e-12)) {
                faar_dft_fir_clear(&full4);
                faar_dft_fir_clear(&quarter4);
                return fail("folded N/4 inverse differs from full inverse");
            }
        }
    }
    faar_dft_fir_clear(&full4);
    faar_dft_fir_clear(&quarter4);

    faar_sample_t stream4_input[STREAM4], stream4_output[(STREAM4 + 3) / 4];
    for (size_t i = 0; i < STREAM4; ++i)
        stream4_input[i] = 0.19 * sin(0.031 * i) + 0.08 * cos(0.097 * i);
    if (faar_dft_fir_init_rate(&stage, taps4, TAPS4, FFT4, 1, 4))
        return fail("quarter-rate streaming FIR init failed");
    size_t in4 = 0, out4 = 0;
    while (in4 < STREAM4) {
        size_t request = STREAM4 - in4;
        if (request > CHUNK) request = CHUNK;
        size_t consumed = 0, produced = 0;
        if (faar_dft_fir_stream(&stage, stream4_input + in4, request,
                    &consumed, stream4_output + out4, CAPACITY, &produced)) {
            faar_dft_fir_clear(&stage);
            return fail("quarter-rate streaming FIR process failed");
        }
        in4 += consumed;
        out4 += produced;
        if (!consumed && !produced) {
            faar_dft_fir_clear(&stage);
            return fail("quarter-rate streaming FIR made no progress");
        }
    }
    for (;;) {
        size_t produced = 0;
        if (faar_dft_fir_flush(&stage, stream4_output + out4,
                    (STREAM4 + 3) / 4 - out4, &produced)) {
            faar_dft_fir_clear(&stage);
            return fail("quarter-rate streaming FIR flush failed");
        }
        out4 += produced;
        if (!produced) break;
    }
    if (out4 != (STREAM4 + 3) / 4) {
        faar_dft_fir_clear(&stage);
        return fail("quarter-rate streaming output count mismatch");
    }
    for (size_t m = 0; m < out4; ++m) {
        size_t n = 4 * m;
        double expected = 0.0;
        for (size_t k = 0; k < TAPS4 && k <= n; ++k)
            expected += taps4[k] * stream4_input[n - k];
        if (fabs(stream4_output[m] - expected) > (sizeof(faar_sample_t) == sizeof(float) ? 2e-6 : 2e-12)) {
            faar_dft_fir_clear(&stage);
            return fail("quarter-rate stream differs from direct convolution");
        }
    }
    faar_dft_fir_clear(&stage);
    puts("DFT FIR: chunk/capacity independence, flush and reset passed");
    puts("DFT FIR: factor-two spectral expansion matches explicit zero insertion");
    puts("DFT FIR: folded half-rate inverse matches full inverse and direct convolution");
    puts("DFT FIR: folded quarter-rate inverse matches full and streamed convolution");
    puts("DFT FIR: 601-tap Kaiser coefficients and response passed");
    return 0;
}
