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
#ifndef FAAR_DFT_FIR_H
#define FAAR_DFT_FIR_H

#include "fft.h"
#include "libfaar_internal.h"
#ifndef FAAR_WIDE_HIGH
#include "fft_float2.h"
#endif

#include <stddef.h>

/* Block overlap-save FIR stage; filter coefficients are supplied by caller. */
typedef struct faar_dft_fir {
    int shared_coeffs; /* Borrowed immutable coefficients; source outlives stage. */
    int shared_fft; /* Borrowed workspace; executions with source are serialized. */
    int compensated; /* High in float builds: wider arithmetic in sensitive stages. */
    int halfband; /* Sparse time-domain 2:1 FIR; otherwise paired FFT. */
    size_t fft_size;
    size_t tap_count;
    size_t expansion;
    size_t block_size;
    size_t output_block_size;
    size_t input_block_size;
    size_t decimation;
    faar_sample_t *filter_spectrum;
    faar_sample_t *work;
    faar_sample_t *overlap;
    faar_sample_t *input_block;
    faar_sample_t *output_block;
    int stacked;
    size_t stream_in; /* samples per lane the stage takes before it runs */
    size_t stream_out;
    size_t lanes;
    faar_fft_plan in, out;
#ifndef FAAR_WIDE_HIGH
    faar_float2_plan precise_in, precise_out;
    float *spec_low;
    faar_float2 *precise_half_coeffs, *precise_half_work;
#endif
#ifdef FAAR_WIDE_HIGH
    faar_fftd_plan in_wide, out_wide;
    double *spec_wide;
    double *wide_half_coeffs, *wide_half_work;
#endif
    faar_sample_t *spec;
    size_t input_fill;
    size_t output_count;
    size_t output_offset;
    int finishing;
    int finished;
} faar_dft_fir;

/* Input samples per half-band block. */
#define FAAR_HALFBAND_BLOCK 256

/* fft_size must be a supported power of two and at least tap_count. */
int faar_dft_fir_init(faar_dft_fir *stage, const double *taps,
                      size_t tap_count, size_t fft_size);
int faar_dft_fir_init_expanded(faar_dft_fir *stage, const double *taps,
                      size_t tap_count, size_t fft_size, size_t expansion);
int faar_dft_fir_init_rate(faar_dft_fir *stage, const double *taps,
                      size_t tap_count, size_t fft_size, size_t expansion,
                      size_t decimation);
/* Paired stage: `lanes` (1 or 2) channels share one complex FFT as
 * z = a + jb, so the two real outputs come out as real and imaginary parts.
 * With stacked, a lone channel instead pairs each block with the next, so the
 * stage runs once per two blocks (stream_in input samples). Rates and block
 * geometry are as for init_rate; fft_size must be a power of two the complex
 * FFT supports. Lane planes are laid out one after another: input lane l
 * starts at input + l * input_count, output lane l at output + l *
 * output_capacity (process: stream_in and stream_out). */
int faar_dft_fir_init_paired(faar_dft_fir *stage, const double *taps,
                      size_t tap_count, size_t fft_size, size_t expansion,
                      size_t decimation, size_t lanes, int stacked);
/* Initialize private streaming state using source's filter and FFT workspace.
 * Executions must be serialized; source must outlive this stage. This is for channel
 * stages owned by one prefilter, not for sharing across public handles. */
int faar_dft_fir_init_shared(faar_dft_fir *stage,
                      const faar_dft_fir *source, size_t lanes, int stacked);
/* Share immutable filter data while allocating private transform workspace.
 * The source must outlive the stage; per-stage execution needs no serialization. */
int faar_dft_fir_init_filter_shared(faar_dft_fir *stage,
                      const faar_dft_fir *source, size_t lanes, int stacked);
/* Sparse time-domain 2:1 stage: N = 4m+1 taps with the even non-centre
 * taps zero. */
int faar_dft_fir_init_halfband(faar_dft_fir *stage, const double *taps,
                      size_t tap_count);
/* Float-expansion variant for High. Internal arithmetic remains float. */
int faar_dft_fir_init_compensated(faar_dft_fir *stage, const double *taps,
                      size_t tap_count, size_t fft_size, size_t expansion,
                      size_t decimation, size_t lanes, int stacked, int halfband);
void faar_dft_fir_clear(faar_dft_fir *stage);
/* Drop mutable execution storage but retain filter and twiddle metadata for
 * constructing another private-workspace clone. Safe to call more than once. */
void faar_dft_fir_release_workspace(faar_dft_fir *stage);
/* Touch private work buffers before the first input. */
void faar_dft_fir_prepare(faar_dft_fir *stage);

/* Consumes and produces exactly stage->block_size mono samples per call. */
int faar_dft_fir_process(faar_dft_fir *stage, const faar_sample_t *input,
                         faar_sample_t *output);

/* Stream arbitrary chunks. consumed/produced are always set. Output capacity
 * may be smaller than a transform block; remaining output is retained. */
int faar_dft_fir_stream(faar_dft_fir *stage, const faar_sample_t *input,
                        size_t input_count, size_t *consumed,
                        faar_sample_t *output, size_t output_capacity,
                        size_t *produced);

/* Finish a stream, emitting the remaining real input samples. Call until 0. */
int faar_dft_fir_flush(faar_dft_fir *stage, faar_sample_t *output,
                       size_t output_capacity, size_t *produced);

/* Return to the zero-history initial state. */
void faar_dft_fir_reset(faar_dft_fir *stage);

#endif
