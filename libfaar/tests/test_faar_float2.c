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
/* Numerical validation of the compensated float FFT against the independent
 * double FFT, plus inverse round trips and borrowed-twiddle ownership. */
#include "fft_float2.h"
#include "fft.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t random_state = UINT32_C(0x6d2b79f5);
static float random_sample(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return (float)(random_state >> 8) / 8388608.0f - 1.0f;
}

static int check_transform(int n, int tone) {
    faar_float2_plan plan = {0};
    faar_fftd_plan reference = {0};
    float *input = NULL;
    int bad = 1;
    if (faar_float2_init(&plan, (size_t)n) || faar_fftd_cinit(&reference, n)) goto done;
    input = malloc(2 * (size_t)n * sizeof(*input));
    if (!input) goto done;
    double *expected = faar_fftd_cbuffer(&reference, 0);
    for (int j = 0; j < n; ++j) {
        double phase = 6.283185307179586476925286766559 * (n / 8) * j / n;
        input[j] = tone ? (float)(.75 * cos(phase)) : random_sample();
        input[n + j] = tone ? (float)(.5 * sin(phase)) : random_sample();
        plan.work[j] = ff2(input[j]);
        plan.work[n + j] = ff2(input[n + j]);
        expected[j] = input[j];
        expected[n + j] = input[n + j];
    }
    faar_float2_forward(&plan);
    expected = faar_fftd_cforward(&reference, expected);
    double error = 0, energy = 0;
    for (int j = 0; j < 2 * n; ++j) {
        double actual = (double)plan.work[j].hi + plan.work[j].lo;
        double difference = actual - expected[j];
        error += difference * difference;
        energy += expected[j] * expected[j];
    }
    if (!isfinite(error) || error > energy * 1e-24) {
        fprintf(stderr, "float expansion FFT error n=%d tone=%d relative_rms=%g\n",
                n, tone, sqrt(error / energy));
        goto done;
    }
    for (int j = 0; j < n; ++j) plan.work[n + j] = ff2_neg(plan.work[n + j]);
    faar_float2_forward(&plan);
    for (int j = 0; j < 2 * n; ++j) {
        double actual = ((double)plan.work[j].hi + plan.work[j].lo) / n;
        if (j >= n) actual = -actual;
        if (!isfinite(actual) || fabs(actual - input[j]) > 1e-12) {
            fprintf(stderr, "float expansion FFT roundtrip n=%d index=%d\n", n, j);
            goto done;
        }
    }
    bad = 0;
done:
    free(input);
    faar_float2_clear(&plan);
    faar_fftd_clear(&reference);
    return bad;
}

static int check_shared(void) {
    faar_float2_plan owner = {0}, borrower = {0}, separate = {0};
    int bad = 1;
    if (faar_float2_init(&owner, 1024)
            || faar_float2_init_shared(&borrower, 256, &owner)
            || faar_float2_init(&separate, 256)) goto done;
    if (owner.twiddle != borrower.twiddle || borrower.twiddle_owned
            || owner.work == borrower.work) goto done;
    for (size_t i = 0; i < 512; ++i)
        borrower.work[i] = separate.work[i] = ff2(random_sample());
    faar_float2_forward(&borrower);
    faar_float2_forward(&separate);
    for (size_t i = 0; i < 512; ++i) {
        double actual = (double)borrower.work[i].hi + borrower.work[i].lo;
        double expected = (double)separate.work[i].hi + separate.work[i].lo;
        if (fabs(actual - expected) > 1e-12) goto done;
    }
    faar_float2_clear(&borrower);
    faar_float2_prepare(&owner);
    for (size_t i = 0; i < 2048; ++i)
        if (owner.work[i].hi != 0 || owner.work[i].lo != 0) goto done;
    bad = 0;
done:
    faar_float2_clear(&borrower);
    faar_float2_clear(&separate);
    faar_float2_clear(&owner);
    if (bad) fprintf(stderr, "float expansion shared FFT plans failed\n");
    return bad;
}

int main(void) {
    const int sizes[] = {16, 32, 64, 256, 1024, 8192, 65536};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
        if (check_transform(sizes[i], 0) || check_transform(sizes[i], 1)) return 1;
    if (check_shared()) return 1;
    const uint32_t integers[] = {0, 1, 47999, 192000, UINT32_MAX};
    for (size_t i = 0; i < sizeof(integers) / sizeof(integers[0]); ++i) {
        faar_float2 a = ff2_uint32(integers[i]);
        if ((double)a.hi + a.lo != integers[i]) return 1;
        faar_float2 q = ff2_div(a, ff2_uint32(UINT32_MAX));
        if (fabs((double)q.hi + q.lo - (double)integers[i] / UINT32_MAX) > 1e-12) return 1;
    }
    puts("compensated float FFT and arithmetic PASS");
    return 0;
}
