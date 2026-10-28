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
/*
 * Overlap-save FIR stage used by FAAR. The block geometry follows standard
 * linear convolution.
 */

#include "dft_fir.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int init_stage(faar_dft_fir *stage, const double *taps,
                      size_t tap_count, size_t fft_size, size_t expansion,
                      size_t decimation, size_t lanes, int stacked, int compensated);


int faar_dft_fir_init(faar_dft_fir *stage, const double *taps,
                      size_t tap_count, size_t fft_size) {
    return faar_dft_fir_init_rate(stage, taps, tap_count, fft_size, 1, 1);
}

int faar_dft_fir_init_expanded(faar_dft_fir *stage, const double *taps,
                      size_t tap_count, size_t fft_size, size_t expansion) {
    return faar_dft_fir_init_rate(stage, taps, tap_count, fft_size,
                                  expansion, 1);
}

int faar_dft_fir_init_rate(faar_dft_fir *stage, const double *taps,
                      size_t tap_count, size_t fft_size, size_t expansion,
                      size_t decimation) {
    return faar_dft_fir_init_paired(stage, taps, tap_count, fft_size,
                                    expansion, decimation, 1, 0);
}

int faar_dft_fir_init_paired(faar_dft_fir *stage, const double *taps,
                      size_t tap_count, size_t fft_size, size_t expansion,
                      size_t decimation, size_t lanes, int stacked) {
    return init_stage(stage, taps, tap_count, fft_size, expansion,
                      decimation, lanes, stacked, 0);
}

static size_t overlap_stride(const faar_dft_fir *stage) {
    size_t stored = (stage->tap_count - 1) / stage->expansion;
    return stored ? stored : 1;
}

#define PAIR_REAL faar_sample_t
#define PAIR_PLAN faar_fft_plan
#define PAIR_TAG
#define PAIR_FFT faar_fft_
#include "dft_fir_pair.inc"
#undef PAIR_REAL
#undef PAIR_PLAN
#undef PAIR_TAG
#undef PAIR_FFT
#ifdef FAAR_WIDE_HIGH
#define PAIR_REAL double
#define PAIR_PLAN faar_fftd_plan
#define PAIR_TAG _wide
#define PAIR_FFT faar_fftd_
#include "dft_fir_pair.inc"
#undef PAIR_REAL
#undef PAIR_PLAN
#undef PAIR_TAG
#undef PAIR_FFT
#endif
#include "dft_fir_float2.inc"
#include "dft_fir_wide.inc"

static int init_paired_buffers(faar_dft_fir *stage) {
    stage->overlap = (faar_sample_t *)faar_alloc_zeroed(stage->lanes * overlap_stride(stage),
                                      sizeof(faar_sample_t));
    stage->input_block = (faar_sample_t *)faar_alloc_zeroed(stage->lanes
            * stage->stream_in, sizeof(faar_sample_t));
    stage->output_block = (faar_sample_t *)faar_alloc_zeroed(stage->lanes
            * stage->stream_out, sizeof(faar_sample_t));
    return stage->overlap && stage->input_block && stage->output_block
        ? 0 : -1;
}

/* High's wide stages keep their spectrum only in double. */
static const void *paired_spectrum(const faar_dft_fir *stage) {
#ifdef FAAR_WIDE_HIGH
    if (stage->compensated) return stage->spec_wide;
#endif
    return stage->spec;
}

/* Spectrum of the taps in the e^{-j} convention, split into real then
 * imaginary parts. A float transform leaves about 2^-24 of absolute error
 * in every stopband bin, which caps image rejection near 145 dB, so this
 * setup-only radix-2 transform runs in double and rounds once. */
static int double_spectrum(faar_sample_t *out, const double *taps,
                           size_t tap_count, size_t n, double scale) {
    double *re = (double *)faar_alloc_zeroed(3 * n, sizeof(*re));
    if (!re) return -1;
    double *im = re + n, *tw = im + n;
    memcpy(re, taps, tap_count * sizeof(*re));
    for (size_t k = 0; k < n / 2; ++k) {
        const double angle = -2.0 * 3.14159265358979323846 * (double)k / (double)n;
        tw[2 * k] = cos(angle);
        tw[2 * k + 1] = sin(angle);
    }
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
        }
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const size_t step = n / len;
        for (size_t i = 0; i < n; i += len)
            for (size_t k = 0; k < len / 2; ++k) {
                const double wr = tw[2 * k * step], wi = tw[2 * k * step + 1];
                const size_t a = i + k, b = a + len / 2;
                const double xr = re[b] * wr - im[b] * wi;
                const double xi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - xr; im[b] = im[a] - xi;
                re[a] += xr; im[a] += xi;
            }
    }
    for (size_t i = 0; i < n; ++i) {
        out[i] = (faar_sample_t)(re[i] * scale);
        out[n + i] = (faar_sample_t)(im[i] * scale);
    }
    FreeMemory(re);
    return 0;
}

/* Float-sample counterpart of init_spectrum_wide; the stored spectrum is
 * float but computed in double. Other transform sizes keep the float path. */
static int init_spectrum_rounded(faar_dft_fir *stage, const double *taps) {
    const size_t n = stage->fft_size;
    if (n < 2 || (n & (n - 1)) || n > (size_t)INT_MAX)
        return init_spectrum(stage, taps);
    const double scale = 1.0 / ((double)(n / stage->decimation) * (double)stage->decimation);
    faar_sample_t *spectrum = (faar_sample_t *)AllocMemory(2 * n * sizeof(*spectrum));
    if (!spectrum) return -1;
    if (double_spectrum(spectrum, taps, stage->tap_count, n, scale)) {
        FreeMemory(spectrum);
        return -1;
    }
    stage->spec = spectrum;
    return init_fft_plans(stage);
}

static int init_paired(faar_dft_fir *stage, const double *taps) {
#ifndef FAAR_WIDE_HIGH
    if (stage->compensated)
        return init_precise_spectrum(stage, taps) || init_paired_buffers(stage) ? -1 : 0;
#endif
#ifdef FAAR_WIDE_HIGH
    if (stage->compensated)
        return init_spectrum_wide(stage, taps) || init_paired_buffers(stage) ? -1 : 0;
#endif
    return init_spectrum_rounded(stage, taps) || init_paired_buffers(stage) ? -1 : 0;
}

static int init_geometry(faar_dft_fir *stage, size_t tap_count,
                      size_t fft_size, size_t expansion, size_t decimation,
                      size_t lanes, int stacked) {
    if (!stage || !tap_count || tap_count > fft_size
            || fft_size > SIZE_MAX / (2 * sizeof(faar_sample_t))
            || (expansion != 1 && expansion != 2)
            || (decimation != 1 && decimation != 2 && decimation != 4)
            || (fft_size - tap_count + 1) % expansion
            || (tap_count - 1) % expansion
            || (fft_size - tap_count + 1) % decimation
            || (tap_count - 1) % decimation)
        return -1;
    memset(stage, 0, sizeof(*stage));
    stage->lanes = lanes;
    stage->fft_size = fft_size;
    stage->tap_count = tap_count;
    stage->expansion = expansion;
    stage->decimation = decimation;
    stage->block_size = fft_size - tap_count + 1;
    stage->output_block_size = stage->block_size / decimation;
    stage->input_block_size = stage->block_size / expansion;
    stage->stacked = stacked;
    stage->stream_in = stage->input_block_size * (stacked ? 2 : 1);
    stage->stream_out = stage->output_block_size * (stacked ? 2 : 1);
    return 0;
}

static int init_stage(faar_dft_fir *stage, const double *taps,
                      size_t tap_count, size_t fft_size, size_t expansion,
                      size_t decimation, size_t lanes, int stacked, int compensated) {
    if (!taps || (lanes != 1 && lanes != 2) || (stacked && lanes != 1)
            || (expansion && fft_size / expansion > (size_t)INT_MAX)
            || init_geometry(stage, tap_count, fft_size, expansion,
                             decimation, lanes, stacked))
        return -1;
    stage->compensated = compensated;
    if (init_paired(stage, taps)) {
        faar_dft_fir_clear(stage);
        return -1;
    }
    return 0;
}

static int init_halfband_buffers(faar_dft_fir *stage) {
#ifndef FAAR_WIDE_HIGH
    if (stage->compensated) {
        stage->precise_half_work = faar_alloc_zeroed(stage->block_size / 2, sizeof(faar_float2));
        if (!stage->precise_half_work) return -1;
    }
#endif
#ifdef FAAR_WIDE_HIGH
    if (stage->compensated) {
        stage->wide_half_work = faar_alloc_zeroed(stage->block_size / 2, sizeof(double));
        if (!stage->wide_half_work) return -1;
    }
#endif
    stage->work = (faar_sample_t *)faar_alloc_zeroed(stage->fft_size + 2, sizeof(faar_sample_t));
    stage->overlap = (faar_sample_t *)faar_alloc_zeroed(stage->tap_count - 1, sizeof(faar_sample_t));
    stage->input_block = (faar_sample_t *)faar_alloc_zeroed(stage->input_block_size,
                                          sizeof(faar_sample_t));
    stage->output_block = (faar_sample_t *)faar_alloc_zeroed(stage->block_size, sizeof(faar_sample_t));
    return stage->work && stage->overlap && stage->input_block
        && stage->output_block ? 0 : -1;
}

static int init_halfband(faar_dft_fir *stage, const double *taps,
                      size_t tap_count, int compensated) {
    if (!taps || tap_count < 5 || (tap_count - 1) % 4
            || tap_count > SIZE_MAX - FAAR_HALFBAND_BLOCK
            || init_geometry(stage, tap_count,
                             FAAR_HALFBAND_BLOCK + tap_count - 1, 1, 2, 1, 0))
        return -1;
    stage->halfband = 1;
    stage->compensated = compensated;
#ifndef FAAR_WIDE_HIGH
    if (compensated) {
        stage->precise_half_coeffs = AllocMemory((tap_count / 4 + 1) * sizeof(faar_float2));
        if (!stage->precise_half_coeffs) { faar_dft_fir_clear(stage); return -1; }
        for (size_t j = 0; j < tap_count / 4; ++j)
            stage->precise_half_coeffs[j] = ff2_setup(taps[2 * j + 1]);
        stage->precise_half_coeffs[tap_count / 4] = ff2_setup(taps[tap_count / 2]);
    }
#endif
#ifdef FAAR_WIDE_HIGH
    if (compensated) {
        stage->wide_half_coeffs = AllocMemory((tap_count / 4 + 1) * sizeof(double));
        if (!stage->wide_half_coeffs) { faar_dft_fir_clear(stage); return -1; }
        for (size_t j = 0; j < tap_count / 4; ++j)
            stage->wide_half_coeffs[j] = taps[2 * j + 1];
        stage->wide_half_coeffs[tap_count / 4] = taps[tap_count / 2];
    }
#endif
    /* Odd taps and the centre: the even non-centre taps are zero. */
    stage->filter_spectrum = (faar_sample_t *)faar_alloc_zeroed(tap_count / 4 + 1,
                                                     sizeof(faar_sample_t));
    if (!stage->filter_spectrum || init_halfband_buffers(stage)) {
        faar_dft_fir_clear(stage);
        return -1;
    }
    for (size_t j = 0; j < tap_count / 4; ++j)
        stage->filter_spectrum[j] = taps[2 * j + 1];
    stage->filter_spectrum[tap_count / 4] = taps[tap_count / 2];
    return 0;
}

int faar_dft_fir_init_halfband(faar_dft_fir *stage, const double *taps,
                      size_t tap_count) {
    return init_halfband(stage, taps, tap_count, 0);
}

int faar_dft_fir_init_compensated(faar_dft_fir *stage, const double *taps,
                      size_t tap_count, size_t fft_size, size_t expansion,
                      size_t decimation, size_t lanes, int stacked, int halfband) {
    return halfband ? init_halfband(stage, taps, tap_count, 1)
        : init_stage(stage, taps, tap_count, fft_size, expansion, decimation,
                      lanes, stacked, 1);
}

static int init_shared_internal(faar_dft_fir *stage,
                      const faar_dft_fir *source, size_t lanes, int stacked,
                      int private_workspace) {
    if (!stage || !source || stage == source
            || (lanes != 1 && lanes != 2)
            || (stacked != 0 && stacked != 1) || (stacked && lanes != 1)
            || (source->halfband && (lanes != 1 || stacked))
            || !(source->halfband ? source->filter_spectrum : paired_spectrum(source))
            || init_geometry(stage, source->tap_count, source->fft_size,
                             source->expansion, source->decimation, lanes, stacked))
        return -1;
    stage->shared_coeffs = 1;
    stage->halfband = source->halfband;
    stage->compensated = source->compensated;
#ifndef FAAR_WIDE_HIGH
    stage->spec_low = source->spec_low;
    stage->precise_half_coeffs = source->precise_half_coeffs;
#endif
#ifdef FAAR_WIDE_HIGH
    stage->spec_wide = source->spec_wide;
    stage->wide_half_coeffs = source->wide_half_coeffs;
#endif
    stage->filter_spectrum = source->filter_spectrum;
    stage->spec = source->spec;
    /* Ordinary channel stages execute sequentially and borrow FFT work.
     * Reusable filter clones instead build independent execution plans. */
    stage->shared_fft = !stage->halfband && !private_workspace;
    if (stage->shared_fft) {
        stage->in = source->in;
        stage->out = source->out;
#ifndef FAAR_WIDE_HIGH
        stage->precise_in = source->precise_in;
        stage->precise_out = source->precise_out;
#endif
#ifdef FAAR_WIDE_HIGH
        stage->in_wide = source->in_wide;
        stage->out_wide = source->out_wide;
#endif
    }
    int bad;
    if (stage->halfband) {
        bad = init_halfband_buffers(stage);
    } else if (private_workspace) {
        int plan_bad;
#ifndef FAAR_WIDE_HIGH
        if (stage->compensated)
            plan_bad = init_precise_plans(stage, source);
        else
#endif
#ifdef FAAR_WIDE_HIGH
        if (stage->compensated)
            plan_bad = init_fft_plans_filter_shared_wide(stage, source);
        else
#endif
            plan_bad = init_fft_plans_filter_shared(stage, source);
        bad = plan_bad || init_paired_buffers(stage);
    } else {
        bad = init_paired_buffers(stage);
    }
    if (bad) {
        faar_dft_fir_clear(stage);
        return -1;
    }
    return 0;
}

int faar_dft_fir_init_shared(faar_dft_fir *stage,
                      const faar_dft_fir *source, size_t lanes, int stacked) {
    return init_shared_internal(stage, source, lanes, stacked, 0);
}

int faar_dft_fir_init_filter_shared(faar_dft_fir *stage,
                      const faar_dft_fir *source, size_t lanes, int stacked) {
    return init_shared_internal(stage, source, lanes, stacked, 1);
}

void faar_dft_fir_release_workspace(faar_dft_fir *stage) {
    if (!stage) return;
    FreeMemory(stage->work);
    stage->work = NULL;
#ifndef FAAR_WIDE_HIGH
    FreeMemory(stage->precise_half_work);
    stage->precise_half_work = NULL;
    if (stage->shared_fft) {
        stage->precise_in.work = NULL;
        stage->precise_out.work = NULL;
    } else {
        FreeMemory(stage->precise_in.work);
        FreeMemory(stage->precise_out.work);
        stage->precise_in.work = NULL;
        stage->precise_out.work = NULL;
    }
#endif
#ifdef FAAR_WIDE_HIGH
    FreeMemory(stage->wide_half_work);
    stage->wide_half_work = NULL;
    if (stage->shared_fft) {
        stage->in_wide.work = NULL;
        stage->in_wide.scratch = NULL;
        stage->out_wide.work = NULL;
        stage->out_wide.scratch = NULL;
    } else {
        FreeMemory(stage->in_wide.work);
        FreeMemory(stage->out_wide.work);
        stage->in_wide.work = NULL;
        stage->in_wide.scratch = NULL;
        stage->out_wide.work = NULL;
        stage->out_wide.scratch = NULL;
    }
#endif
    if (stage->shared_fft) {
        stage->in.work = NULL;
        stage->in.scratch = NULL;
        stage->out.work = NULL;
        stage->out.scratch = NULL;
    } else {
        FreeMemory(stage->in.work);
        FreeMemory(stage->out.work);
        stage->in.work = NULL;
        stage->in.scratch = NULL;
        stage->out.work = NULL;
        stage->out.scratch = NULL;
    }
    FreeMemory(stage->overlap);
    FreeMemory(stage->input_block);
    FreeMemory(stage->output_block);
    stage->overlap = NULL;
    stage->input_block = NULL;
    stage->output_block = NULL;
    stage->input_fill = stage->output_count = stage->output_offset = 0;
    stage->finishing = stage->finished = 0;
}

void faar_dft_fir_prepare(faar_dft_fir *stage) {
    if (!stage) return;
    if (stage->halfband) {
        memset(stage->work, 0, (stage->fft_size + 2) * sizeof(faar_sample_t));
#ifndef FAAR_WIDE_HIGH
        if (stage->precise_half_work)
            memset(stage->precise_half_work, 0,
                    stage->block_size / 2 * sizeof(*stage->precise_half_work));
#endif
#ifdef FAAR_WIDE_HIGH
        if (stage->wide_half_work)
            memset(stage->wide_half_work, 0,
                    stage->block_size / 2 * sizeof(*stage->wide_half_work));
#endif
    }
#ifndef FAAR_WIDE_HIGH
    else if (stage->compensated) {
        faar_float2_prepare(&stage->precise_in);
        faar_float2_prepare(&stage->precise_out);
    }
#endif
#ifdef FAAR_WIDE_HIGH
    else if (stage->compensated) {
        faar_fftd_prepare(&stage->in_wide, !stage->out_wide.size);
        faar_fftd_prepare(&stage->out_wide, 0);
    }
#endif
    else {
        faar_fft_prepare(&stage->in, !stage->out.size);
        faar_fft_prepare(&stage->out, 0);
    }
    memset(stage->overlap, 0, stage->lanes * overlap_stride(stage) * sizeof(faar_sample_t));
    memset(stage->input_block, 0, stage->lanes * stage->stream_in * sizeof(faar_sample_t));
    size_t output = stage->halfband ? stage->block_size : stage->lanes * stage->stream_out;
    memset(stage->output_block, 0, output * sizeof(faar_sample_t));
}

void faar_dft_fir_clear(faar_dft_fir *stage) {
    if (!stage) return;
    if (!stage->shared_coeffs) {
        FreeMemory(stage->filter_spectrum);
        FreeMemory(stage->spec);
#ifndef FAAR_WIDE_HIGH
        FreeMemory(stage->spec_low);
        FreeMemory(stage->precise_half_coeffs);
#endif
#ifdef FAAR_WIDE_HIGH
        FreeMemory(stage->spec_wide);
        FreeMemory(stage->wide_half_coeffs);
#endif
    }
    FreeMemory(stage->work);
    FreeMemory(stage->overlap);
    FreeMemory(stage->input_block);
    FreeMemory(stage->output_block);
#ifndef FAAR_WIDE_HIGH
    FreeMemory(stage->precise_half_work);
    if (!stage->shared_fft) {
        faar_float2_clear(&stage->precise_in);
        faar_float2_clear(&stage->precise_out);
    }
#endif
#ifdef FAAR_WIDE_HIGH
    FreeMemory(stage->wide_half_work);
    if (!stage->shared_fft) {
        faar_fftd_clear(&stage->in_wide);
        faar_fftd_clear(&stage->out_wide);
    }
#endif
    if (!stage->shared_fft) {
        faar_fft_clear(&stage->in);
        faar_fft_clear(&stage->out);
    }
    memset(stage, 0, sizeof(*stage));
}


static void update_overlap(faar_dft_fir *stage, const faar_sample_t *input) {
    const size_t overlap = stage->tap_count - 1;
    if (!overlap) return;
    for (size_t lane = 0; lane < stage->lanes; ++lane) {
        const size_t block = stage->stream_in;
        const faar_sample_t *in = input + lane * block;
        faar_sample_t *keep = stage->overlap + lane * overlap_stride(stage);
        size_t stored_overlap = overlap / stage->expansion;
        if (block >= stored_overlap) {
            memcpy(keep, in + block - stored_overlap,
                   stored_overlap * sizeof(faar_sample_t));
        } else {
            memmove(keep, keep + block,
                    (stored_overlap - block) * sizeof(faar_sample_t));
            memcpy(keep + stored_overlap - block, in, block * sizeof(faar_sample_t));
        }
    }
}

int faar_dft_fir_process(faar_dft_fir *stage, const faar_sample_t *input,
                         faar_sample_t *output) {
    if (stage && stage->halfband && input && output) {
        const size_t n = stage->tap_count;
        const size_t block = stage->block_size;
        memcpy(stage->work, stage->overlap, (n - 1) * sizeof(faar_sample_t));
        memcpy(stage->work + n - 1, input, block * sizeof(faar_sample_t));
#ifndef FAAR_WIDE_HIGH
        if (stage->compensated) {
            process_precise_halfband(stage, output);
            memcpy(stage->overlap, stage->work + block, (n - 1) * sizeof(faar_sample_t));
            return 0;
        }
#endif
#ifdef FAAR_WIDE_HIGH
        if (stage->compensated) {
            process_wide_halfband(stage, output);
            memcpy(stage->overlap, stage->work + block, (n - 1) * sizeof(faar_sample_t));
            return 0;
        }
#endif
        for (size_t i = 0; i < block / 2; ++i)
            output[i] = stage->filter_spectrum[n / 4] * stage->work[2 * i + n / 2];
        /* Vectorize across independent outputs, without reassociating a
         * floating-point reduction or requiring compiler-specific flags. */
        for (size_t j = 0; j < n / 4; ++j) {
            const faar_sample_t coefficient = stage->filter_spectrum[j];
            const faar_sample_t *x = stage->work + 2 * j + 1;
            const faar_sample_t *y = stage->work + n - 2 - 2 * j;
            for (size_t i = 0; i < block / 2; ++i)
                output[i] += coefficient * (x[2 * i] + y[2 * i]);
        }
        memcpy(stage->overlap, stage->work + block, (n - 1) * sizeof(faar_sample_t));
        return 0;
    }
    if (!stage || !input || !output || !stage->overlap || !paired_spectrum(stage))
        return -1;
#ifndef FAAR_WIDE_HIGH
    if (stage->compensated) convolve_precise(stage, input, output);
    else
#endif
#ifdef FAAR_WIDE_HIGH
    if (stage->compensated) convolve_paired_wide(stage, input, output);
    else
#endif
        convolve_paired(stage, input, output);
    update_overlap(stage, input);
    return 0;
}

void faar_dft_fir_reset(faar_dft_fir *stage) {
    if (!stage || !stage->overlap) return;
    memset(stage->overlap, 0, stage->lanes * overlap_stride(stage)
                                  * sizeof(faar_sample_t));
    stage->input_fill = 0;
    stage->output_count = 0;
    stage->output_offset = 0;
    stage->finishing = 0;
    stage->finished = 0;
}

/* Lane l of the caller's output starts l * stride samples in. */
static size_t stream_drain(faar_dft_fir *stage, faar_sample_t *output, size_t stride,
                           size_t capacity) {
    size_t remaining = stage->output_count - stage->output_offset;
    size_t count = remaining < capacity ? remaining : capacity;
    if (count) {
        for (size_t lane = 0; lane < stage->lanes; ++lane)
            memcpy(output + lane * stride,
                   stage->output_block + lane * stage->stream_out
                       + stage->output_offset, count * sizeof(faar_sample_t));
        stage->output_offset += count;
    }
    if (stage->output_offset == stage->output_count) {
        stage->output_offset = 0;
        stage->output_count = 0;
    }
    return count;
}


int faar_dft_fir_stream(faar_dft_fir *stage, const faar_sample_t *input,
                        size_t input_count, size_t *consumed,
                        faar_sample_t *output, size_t output_capacity,
                        size_t *produced) {
    if (!stage || !consumed || !produced || (input_count && !input)
            || (output_capacity && !output) || stage->finishing
            || stage->finished) return -1;
    *consumed = 0;
    *produced = 0;
    *produced += stream_drain(stage, output, output_capacity,
                              output_capacity);
    while (*consumed < input_count) {
        if (stage->output_count) break;
        size_t available = stage->stream_in - stage->input_fill;
        size_t count = input_count - *consumed;
        if (count > available) count = available;
        for (size_t lane = 0; lane < stage->lanes; ++lane)
            memcpy(stage->input_block + lane * stage->stream_in
                       + stage->input_fill,
                   input + lane * input_count + *consumed,
                   count * sizeof(faar_sample_t));
        stage->input_fill += count;
        *consumed += count;
        if (stage->input_fill == stage->stream_in) {
            if (faar_dft_fir_process(stage, stage->input_block,
                                     stage->output_block)) return -1;
            stage->input_fill = 0;
            stage->output_count = stage->stream_out;
            *produced += stream_drain(stage,
                output ? output + *produced : NULL, output_capacity,
                output_capacity - *produced);
        }
    }
    return 0;
}

int faar_dft_fir_flush(faar_dft_fir *stage, faar_sample_t *output,
                       size_t output_capacity, size_t *produced) {
    if (!stage || !produced || (output_capacity && !output))
        return -1;
    *produced = 0;
    if (stage->finished) return 0;
    stage->finishing = 1;
    *produced += stream_drain(stage, output, output_capacity,
                              output_capacity);
    if (!stage->output_count && stage->input_fill) {
        const size_t real_count = stage->input_fill;
        for (size_t lane = 0; lane < stage->lanes; ++lane)
            memset(stage->input_block + lane * stage->stream_in
                       + real_count, 0,
                   (stage->stream_in - real_count) * sizeof(faar_sample_t));
        if (faar_dft_fir_process(stage, stage->input_block,
                                 stage->output_block)) return -1;
        stage->input_fill = 0;
        stage->output_offset = 0;
        stage->output_count = (real_count * stage->expansion
                + stage->decimation - 1) / stage->decimation;
        *produced += stream_drain(stage,
            output ? output + *produced : NULL, output_capacity,
            output_capacity - *produced);
    }
    if (!stage->input_fill && !stage->output_count) stage->finished = 1;
    return 0;
}
