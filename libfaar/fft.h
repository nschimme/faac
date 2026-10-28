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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */
#ifndef FAAR_FFT_H
#define FAAR_FFT_H

/* Common twiddle tables are immutable after once initialization.
 * Plans have mutable scratch: executions sharing one must be serialized.
 * Init returns 0 on success and needs a fresh or cleared plan, and clear
 * accepts a zero-initialized one. */
typedef struct faar_fftf_plan {
    int size, logm;
    int work_buffers;
    int twiddle_owned; /* Set unless the plan borrows a filter owner's table. */
    float *twiddle, *work, *scratch;
} faar_fftf_plan;

void faar_fftf_clear(faar_fftf_plan *plan);
/* Touch private workspace before the first transform; no stream data yet.
 * Set both_buffers when one plan serves both forward and inverse filtering. */
void faar_fftf_prepare(faar_fftf_plan *plan, int both_buffers);

/* Transform of n complex points, 16 through 4194304.
 * Data is planar: n real parts then n imaginary parts, e^{-j} convention and
 * unnormalized. The plan owns two such buffers (cbuffer 0 and 1). cforward
 * transforms the buffer `in`, which must be one of them, and returns the
 * buffer holding the result: either one, so the caller never aliases it.
 * The inverse is conj(cforward(conj(x))) / n. */
int faar_fftf_cinit(faar_fftf_plan *plan, int n);
int faar_fftf_cinit_shared(faar_fftf_plan *plan, int n,
                              const faar_fftf_plan *owner);
/* Private paired-FIR variant for the smaller side of a padded plan. Only
 * cbuffer 0 is valid for padded plans initialized this way; the existing
 * cinit_shared function retains its two-buffer contract. */
int faar_fftf_cinit_shared_single(faar_fftf_plan *plan, int n,
                                     const faar_fftf_plan *owner);
/* Shared filter twiddles with private work; workspace_owner is an optional
 * larger plan in the same stream. Both owners must outlive this plan. */
int faar_fftf_cinit_filter_shared(faar_fftf_plan *plan, int n,
        const faar_fftf_plan *filter_owner,
        const faar_fftf_plan *workspace_owner, int single);
float *faar_fftf_cbuffer(const faar_fftf_plan *plan, int which);
float *faar_fftf_cforward(const faar_fftf_plan *plan, float *in);

typedef struct faar_fftd_plan {
    int size, logm;
    int work_buffers;
    int twiddle_owned; /* Set unless the plan borrows a filter owner's table. */
    double *twiddle, *work, *scratch;
} faar_fftd_plan;

void faar_fftd_clear(faar_fftd_plan *plan);
void faar_fftd_prepare(faar_fftd_plan *plan, int both_buffers);
int faar_fftd_cinit(faar_fftd_plan *plan, int n);
int faar_fftd_cinit_shared(faar_fftd_plan *plan, int n,
                              const faar_fftd_plan *owner);
int faar_fftd_cinit_shared_single(faar_fftd_plan *plan, int n,
                                     const faar_fftd_plan *owner);
/* Shared filter twiddles with private work; workspace_owner is an optional
 * larger plan in the same stream. Both owners must outlive this plan. */
int faar_fftd_cinit_filter_shared(faar_fftd_plan *plan, int n,
        const faar_fftd_plan *filter_owner,
        const faar_fftd_plan *workspace_owner, int single);
double *faar_fftd_cbuffer(const faar_fftd_plan *plan, int which);
double *faar_fftd_cforward(const faar_fftd_plan *plan, double *in);

#endif
