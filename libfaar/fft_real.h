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
#ifndef FAAR_FFT_REAL_H
#define FAAR_FFT_REAL_H

/* Common twiddle tables are immutable after once initialization.
 * Plans have mutable scratch: executions sharing one must be serialized.
 * Sizes are powers of two, 256 through 65536; init returns 0 on success and
 * needs a fresh or cleared plan, and clear accepts a zero-initialized one.
 * Data is N scalars packed as DC, Nyquist, then (real, positive-sine
 * imaginary) bins. The inverse is normalized. */
typedef struct faar_rfftf_plan {
    int size, logm;
    int work_buffers;
    int twiddle_owned; /* Set unless the plan borrows a filter owner's table. */
    float *twiddle, *split, *work, *scratch;
} faar_rfftf_plan;

int faar_rfftf_init(faar_rfftf_plan *plan, int size);
void faar_rfftf_clear(faar_rfftf_plan *plan);
/* Touch private workspace before the first transform; no stream data yet.
 * Set both_buffers when one plan serves both forward and inverse filtering. */
void faar_rfftf_prepare(faar_rfftf_plan *plan, int both_buffers);
void faar_rfftf_execute(const faar_rfftf_plan *plan, float *data, int inverse);

/* Complex transform of n points, 16 through 4194304, on the same plan type.
 * Data is planar: n real parts then n imaginary parts, e^{-j} convention and
 * unnormalized. The plan owns two such buffers (cbuffer 0 and 1). cforward
 * transforms the buffer `in`, which must be one of them, and returns the
 * buffer holding the result: either one, so the caller never aliases it.
 * The inverse is conj(cforward(conj(x))) / n. */
int faar_rfftf_cinit(faar_rfftf_plan *plan, int n);
int faar_rfftf_cinit_shared(faar_rfftf_plan *plan, int n,
                              const faar_rfftf_plan *owner);
/* Private paired-FIR variant for the smaller side of a padded plan. Only
 * cbuffer 0 is valid for padded plans initialized this way; the existing
 * cinit_shared function retains its two-buffer contract. */
int faar_rfftf_cinit_shared_single(faar_rfftf_plan *plan, int n,
                                     const faar_rfftf_plan *owner);
/* Shared filter twiddles with private work; workspace_owner is an optional
 * larger plan in the same stream. Both owners must outlive this plan. */
int faar_rfftf_cinit_filter_shared(faar_rfftf_plan *plan, int n,
        const faar_rfftf_plan *filter_owner,
        const faar_rfftf_plan *workspace_owner, int single);
float *faar_rfftf_cbuffer(const faar_rfftf_plan *plan, int which);
float *faar_rfftf_cforward(const faar_rfftf_plan *plan, float *in);

typedef struct faar_rfftd_plan {
    int size, logm;
    int work_buffers;
    int twiddle_owned; /* Set unless the plan borrows a filter owner's table. */
    double *twiddle, *split, *work, *scratch;
} faar_rfftd_plan;

int faar_rfftd_init(faar_rfftd_plan *plan, int size);
void faar_rfftd_clear(faar_rfftd_plan *plan);
void faar_rfftd_prepare(faar_rfftd_plan *plan, int both_buffers);
void faar_rfftd_execute(const faar_rfftd_plan *plan, double *data, int inverse);
int faar_rfftd_cinit(faar_rfftd_plan *plan, int n);
int faar_rfftd_cinit_shared(faar_rfftd_plan *plan, int n,
                              const faar_rfftd_plan *owner);
int faar_rfftd_cinit_shared_single(faar_rfftd_plan *plan, int n,
                                     const faar_rfftd_plan *owner);
/* Shared filter twiddles with private work; workspace_owner is an optional
 * larger plan in the same stream. Both owners must outlive this plan. */
int faar_rfftd_cinit_filter_shared(faar_rfftd_plan *plan, int n,
        const faar_rfftd_plan *filter_owner,
        const faar_rfftd_plan *workspace_owner, int single);
double *faar_rfftd_cbuffer(const faar_rfftd_plan *plan, int which);
double *faar_rfftd_cforward(const faar_rfftd_plan *plan, double *in);

#endif
