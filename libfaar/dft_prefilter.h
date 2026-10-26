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
#ifndef FAAR_DFT_PREFILTER_H
#define FAAR_DFT_PREFILTER_H

#include <stddef.h>
#include <stdint.h>
#include "faar.h"

typedef struct faar_dft_prefilter faar_dft_prefilter;

faar_dft_prefilter *faar_dft_prefilter_create(uint16_t channels,
                                               const double *taps,
                                               size_t tap_count,
                                               size_t fft_size);
faar_dft_prefilter *faar_dft_prefilter_create_expanded(uint16_t channels,
                                                const double *taps,
                                                size_t tap_count,
                                                size_t fft_size,
                                                size_t expansion);
/* Channel pairs share one complex FFT (a lone channel stacks its blocks),
 * in the build-selected precision. */
faar_dft_prefilter *faar_dft_prefilter_create_rate(uint16_t channels,
                                               const double *taps,
                                               size_t tap_count,
                                               size_t fft_size,
                                               size_t expansion,
                                               size_t decimation);
/* halfband selects a sparse time-domain 2:1 stage (N = 4m+1, alternate
 * non-centre taps zero) and ignores the FFT geometry. */
faar_dft_prefilter *faar_dft_prefilter_create_stage(uint16_t channels,
                                               const double *taps,
                                               size_t tap_count,
                                               size_t fft_size,
                                               size_t expansion,
                                               size_t decimation,
                                               int halfband, int compensated);
/* Clone immutable filter data from source with private transform and stream
 * workspace. The source must outlive the clone. */
faar_dft_prefilter *faar_dft_prefilter_clone_filter(
        const faar_dft_prefilter *source);
/* Release mutable stream and transform workspace while retaining filter data
 * for future clones. Safe to call more than once; the stage cannot process. */
void faar_dft_prefilter_release_workspace(faar_dft_prefilter *stage);
void faar_dft_prefilter_destroy(faar_dft_prefilter *stage);
/* Reserve before feeding input; returns a conservative output burst bound. */
faar_status faar_dft_prefilter_prepare(faar_dft_prefilter *stage,
        size_t max_input, size_t extra_output, size_t *output_bound);
void faar_dft_prefilter_set_prepared(faar_dft_prefilter *stage, int prepared);
int faar_dft_prefilter_process(faar_dft_prefilter *stage, const float *input,
                               size_t frames, float *output,
                               size_t output_capacity, size_t *produced);
int faar_dft_prefilter_flush(faar_dft_prefilter *stage, float *output,
                             size_t output_capacity, size_t *produced);
/* Internal downstream handoff: feed/finish with zero output capacity, then
 * drain directly into channel histories. Destination planes must each hold
 * output_capacity floats and must not overlap this prefilter's storage. */
size_t faar_dft_prefilter_pending(const faar_dft_prefilter *stage);
size_t faar_dft_prefilter_drain_planes(faar_dft_prefilter *stage,
        float *const output[], size_t output_capacity);
void faar_dft_prefilter_reset(faar_dft_prefilter *stage);

#endif
