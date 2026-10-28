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
/* Regression coverage for serialized FIR stages sharing coefficients and FFTs. */
#include "../dft_fir.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAPS 65
#define FFT 512
#define BLOCKS 4
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int fail(const char *message) {
    fprintf(stderr, "test_faar_dft_shared: %s\n", message);
    return 1;
}

static void make_taps(double taps[TAPS]) {
    double sum = 0.0;
    for (size_t i = 0; i < TAPS; ++i) {
        double x = (double)i - (TAPS - 1) * 0.5;
        double sinc = x == 0.0 ? 0.42 : sin(0.42 * M_PI * x) / (M_PI * x);
        double window = 0.54 - 0.46 * cos(2.0 * M_PI * i / (TAPS - 1));
        taps[i] = sinc * window;
        sum += taps[i];
    }
    for (size_t i = 0; i < TAPS; ++i) taps[i] /= sum;
}

static faar_sample_t value(size_t block, size_t lane, size_t i, unsigned salt) {
    unsigned x = (unsigned)(block * 1664525u + lane * 1013904223u + i * 747796405u + salt);
    x ^= x >> 16; x *= 2246822519u; x ^= x >> 13;
    return (faar_sample_t)((int)(x & 0xffffu) - 32768) / 32768.0;
}

static void fill_input(faar_sample_t *input, size_t lanes, size_t count,
                       size_t block, unsigned salt) {
    for (size_t lane = 0; lane < lanes; ++lane)
        for (size_t i = 0; i < count; ++i)
            input[lane * count + i] = value(block, lane, i, salt);
}

static int process_exact(faar_dft_fir *stage, const faar_sample_t *input,
                         faar_sample_t *output) {
    return faar_dft_fir_process(stage, input, output) == 0;
}

static int init_case(faar_dft_fir *stage, const double *taps, size_t fft,
        size_t expansion, size_t decimation, size_t lanes, int stacked,
        int compensated) {
    return compensated
        ? faar_dft_fir_init_compensated(stage, taps, TAPS, fft, expansion,
                                       decimation, lanes, stacked, 0)
        : faar_dft_fir_init_paired(stage, taps, TAPS, fft, expansion,
                                  decimation, lanes, stacked);
}

static int run_paired_case(size_t expansion, size_t decimation,
                           size_t source_lanes, size_t borrower_lanes,
                           int stacked, size_t fft, int compensated) {
    double taps[TAPS];
    faar_dft_fir owner = {0}, owner_ref = {0}, borrower = {0}, borrower_ref = {0};
    faar_sample_t *si = NULL, *sr = NULL, *sref = NULL;
    faar_sample_t *bi = NULL, *br = NULL, *bref = NULL;
    make_taps(taps);
    if (init_case(&owner, taps, fft, expansion,
                                 decimation, source_lanes, stacked, compensated) ||
        init_case(&owner_ref, taps, fft, expansion,
                                 decimation, source_lanes, stacked, compensated) ||
        init_case(&borrower_ref, taps, fft, expansion,
                                 decimation, borrower_lanes, stacked, compensated) ||
        faar_dft_fir_init_shared(&borrower, &owner, borrower_lanes, stacked))
        goto error;
    if (owner.spec != borrower.spec || !borrower.shared_coeffs || !borrower.shared_fft ||
        owner.overlap == borrower.overlap ||
        owner.input_block == borrower.input_block ||
        owner.output_block == borrower.output_block ||
        owner.in.work != borrower.in.work ||
        owner.in.scratch != borrower.in.scratch ||
        (owner.out.size && (owner.out.work != borrower.out.work ||
                            owner.out.scratch != borrower.out.scratch)) ||
        owner.in.twiddle != borrower.in.twiddle ||
        owner.out.twiddle != borrower.out.twiddle) goto error;

#ifdef FAAR_WIDE_HIGH
    if (compensated && (owner.in_wide.work != borrower.in_wide.work ||
                        owner.out_wide.work != borrower.out_wide.work)) goto error;
#endif
#ifndef FAAR_WIDE_HIGH
    if (compensated && (owner.precise_in.work != borrower.precise_in.work ||
                        owner.precise_out.work != borrower.precise_out.work)) goto error;
#endif
    const size_t source_in = owner.stream_in, source_out = owner.stream_out;
    const size_t borrow_in = borrower.stream_in, borrow_out = borrower.stream_out;
    si = malloc(source_lanes * source_in * sizeof(*si));
    sr = malloc(source_lanes * source_out * sizeof(*sr));
    sref = malloc(source_lanes * source_out * sizeof(*sref));
    bi = malloc(borrower_lanes * borrow_in * sizeof(*bi));
    br = malloc(borrower_lanes * borrow_out * sizeof(*br));
    bref = malloc(borrower_lanes * borrow_out * sizeof(*bref));
    if (!si || !sr || !sref || !bi || !br || !bref) {
        goto error;
    }
    for (size_t pass = 0; pass < 2; ++pass) {
        if (pass) {
            faar_dft_fir_reset(&owner); faar_dft_fir_reset(&owner_ref);
            faar_dft_fir_reset(&borrower); faar_dft_fir_reset(&borrower_ref);
        }
        for (size_t b = 0; b < BLOCKS; ++b) {
            fill_input(si, source_lanes, source_in, b, 37);
            fill_input(bi, borrower_lanes, borrow_in, b, 9127);
            if (!process_exact(&owner, si, sr) ||
                !process_exact(&owner_ref, si, sref) ||
                !process_exact(&borrower, bi, br) ||
                !process_exact(&borrower_ref, bi, bref) ||
                memcmp(sr, sref, source_lanes * source_out * sizeof(*sr)) ||
                memcmp(br, bref, borrower_lanes * borrow_out * sizeof(*br))) {
                goto error;
            }
        }
    }
    /* The owner remains usable after its borrower is cleared. */
    faar_dft_fir_clear(&borrower);
    fill_input(si, source_lanes, source_in, BLOCKS + 1, 37);
    if (!process_exact(&owner, si, sr) ||
        !process_exact(&owner_ref, si, sref) ||
        memcmp(sr, sref, source_lanes * source_out * sizeof(*sr))) goto error;
    free(si); free(sr); free(sref);
    free(bi); free(br); free(bref);
    faar_dft_fir_clear(&owner); faar_dft_fir_clear(&owner_ref);
    faar_dft_fir_clear(&borrower_ref);
    return 1;
error:
    free(si); free(sr); free(sref); free(bi); free(br); free(bref);
    faar_dft_fir_clear(&borrower); faar_dft_fir_clear(&borrower_ref);
    faar_dft_fir_clear(&owner); faar_dft_fir_clear(&owner_ref);
    return 0;
}

static int run_halfband(void) {
    double taps[TAPS] = {0};
    for (size_t j = 0; j < TAPS / 4; ++j) taps[2 * j + 1] = 0.02 * (double)(j + 1);
    taps[TAPS / 2] = 0.5;
    faar_dft_fir owner = {0}, owner_ref = {0}, borrower = {0}, borrower_ref = {0};
    if (faar_dft_fir_init_halfband(&owner, taps, TAPS) ||
        faar_dft_fir_init_halfband(&owner_ref, taps, TAPS) ||
        faar_dft_fir_init_halfband(&borrower_ref, taps, TAPS) ||
        faar_dft_fir_init_shared(&borrower, &owner, 1, 0)) goto error;
    if (owner.filter_spectrum != borrower.filter_spectrum ||
        !borrower.shared_coeffs || owner.work == borrower.work ||
        owner.overlap == borrower.overlap ||
        owner.input_block == borrower.input_block ||
        owner.output_block == borrower.output_block) goto error;
    faar_sample_t in[FAAR_HALFBAND_BLOCK], a[FAAR_HALFBAND_BLOCK], b[FAAR_HALFBAND_BLOCK];
    for (size_t block = 0; block < BLOCKS; ++block) {
        for (size_t i = 0; i < FAAR_HALFBAND_BLOCK; ++i)
            in[i] = value(block, 0, i, 81);
        if (!process_exact(&owner, in, a) || !process_exact(&owner_ref, in, b) ||
            memcmp(a, b, owner.stream_out * sizeof(*a))) goto error;
        for (size_t i = 0; i < FAAR_HALFBAND_BLOCK; ++i)
            in[i] = value(block, 0, i, 7201);
        if (!process_exact(&borrower, in, a) || !process_exact(&borrower_ref, in, b) ||
            memcmp(a, b, owner.stream_out * sizeof(*a))) goto error;
    }
    faar_dft_fir_clear(&borrower);
    faar_dft_fir_clear(&owner); faar_dft_fir_clear(&owner_ref);
    faar_dft_fir_clear(&borrower_ref);
    return 1;
error:
    faar_dft_fir_clear(&borrower); faar_dft_fir_clear(&owner);
    faar_dft_fir_clear(&owner_ref); faar_dft_fir_clear(&borrower_ref);
    return 0;
}

static int invalid_cases(void) {
    double taps[TAPS];
    faar_dft_fir source = {0}, dest = {0};
    make_taps(taps);
    if (faar_dft_fir_init_paired(&source, taps, TAPS, FFT, 1, 1, 1, 0)) return 0;
    if (faar_dft_fir_init_shared(NULL, &source, 1, 0) == 0 ||
        faar_dft_fir_init_shared(&dest, NULL, 1, 0) == 0 ||
        faar_dft_fir_init_shared(&source, &source, 1, 0) == 0 ||
        faar_dft_fir_init_shared(&dest, &source, 0, 0) == 0 ||
        faar_dft_fir_init_shared(&dest, &source, 2, 1) == 0 ||
        faar_dft_fir_init_shared(&dest, &source, 2, 2) == 0) {
        faar_dft_fir_clear(&dest); faar_dft_fir_clear(&source);
        return 0;
    }
    if (source.spec == NULL || !source.in.work) {
        faar_dft_fir_clear(&source); return 0;
    }
    faar_dft_fir_clear(&dest); faar_dft_fir_clear(&source);
    return 1;
}

int main(void) {
    if (!run_paired_case(1, 1, 1, 1, 0, FFT, 0)) return fail("1x/1x sharing failed");
    if (!run_paired_case(2, 4, 2, 1, 0, FFT, 0)) return fail("two-lane to mono sharing failed");
    if (!run_paired_case(2, 2, 1, 1, 1, FFT, 0)) return fail("stacked sharing failed");
    for (int wide = 0; wide <= 1; ++wide) {
        if (!run_paired_case(1, 1, 2, 1, 0, 8192, wide) ||
            !run_paired_case(2, 4, 2, 2, 0, 16384, wide) ||
            !run_paired_case(2, 1, 2, 1, 0, 8192, wide))
            return fail("padded or High workspace sharing failed");
    }
    if (!run_halfband()) return fail("halfband sharing failed");
    if (!invalid_cases()) return fail("invalid arguments damaged or accepted a plan");
    return 0;
}
