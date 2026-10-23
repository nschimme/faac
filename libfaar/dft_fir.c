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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int init_stage(faar_dft_fir *stage, const double *taps,
                      size_t tap_count, size_t fft_size, size_t expansion,
                      size_t decimation, size_t lanes, int stacked);


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
                      decimation, lanes, stacked);
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

/* Filter spectrum in the e^{-j} convention, pre-scaled by the inverse FFT
 * and decimation averaging. Both scales are powers of two, so exact. It is
 * computed once in the selected precision. */
static int init_paired(faar_dft_fir *stage, const double *taps) {
    const size_t n = stage->fft_size, nout = n / stage->decimation;
    const faar_sample_t scale = 1.0 / ((faar_sample_t)nout * (faar_sample_t)stage->decimation);
    faar_fft_plan plan;
    faar_sample_t *h, *spectrum;
    int bad;
    if (n > (size_t)INT_MAX || faar_fft_cinit(&plan, (int)n)) return -1;
    h = faar_fft_cbuffer(&plan, 0);
    memset(h, 0, 2 * n * sizeof(faar_sample_t));
    for (size_t i = 0; i < stage->tap_count; ++i) h[i] = taps[i];
    spectrum = (faar_sample_t *)malloc(2 * n * sizeof(faar_sample_t));
    if (!spectrum) { faar_fft_clear(&plan); return -1; }
    h = faar_fft_cforward(&plan, h);
    for (size_t i = 0; i < 2 * n; ++i) spectrum[i] = h[i] * scale;
    faar_fft_clear(&plan);
    bad = init_transforms(stage, spectrum);
    free(spectrum);
    if (bad) return -1;
    stage->overlap = (faar_sample_t *)calloc(stage->lanes * overlap_stride(stage),
                                      sizeof(faar_sample_t));
    stage->input_block = (faar_sample_t *)calloc(stage->lanes
            * stage->stream_in, sizeof(faar_sample_t));
    stage->output_block = (faar_sample_t *)calloc(stage->lanes
            * stage->stream_out, sizeof(faar_sample_t));
    return stage->overlap && stage->input_block && stage->output_block
        ? 0 : -1;
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
                      size_t decimation, size_t lanes, int stacked) {
    if (!taps || (lanes != 1 && lanes != 2) || (stacked && lanes != 1)
            || (expansion && fft_size / expansion > (size_t)INT_MAX)
            || init_geometry(stage, tap_count, fft_size, expansion,
                             decimation, lanes, stacked))
        return -1;
    if (init_paired(stage, taps)) {
        faar_dft_fir_clear(stage);
        return -1;
    }
    return 0;
}

int faar_dft_fir_init_halfband(faar_dft_fir *stage, const double *taps,
                      size_t tap_count) {
    if (!taps || tap_count < 5 || (tap_count - 1) % 4
            || tap_count > SIZE_MAX - FAAR_HALFBAND_BLOCK
            || init_geometry(stage, tap_count,
                             FAAR_HALFBAND_BLOCK + tap_count - 1, 1, 2, 1, 0))
        return -1;
    stage->halfband = 1;
    /* Odd taps and the centre: the even non-centre taps are zero. */
    stage->filter_spectrum = (faar_sample_t *)calloc(tap_count / 4 + 1,
                                                     sizeof(faar_sample_t));
    stage->work = (faar_sample_t *)calloc(stage->fft_size + 2, sizeof(faar_sample_t));
    stage->overlap = (faar_sample_t *)calloc(tap_count - 1, sizeof(faar_sample_t));
    stage->input_block = (faar_sample_t *)calloc(stage->input_block_size,
                                          sizeof(faar_sample_t));
    stage->output_block = (faar_sample_t *)calloc(stage->block_size, sizeof(faar_sample_t));
    if (!stage->filter_spectrum || !stage->work || !stage->overlap
            || !stage->input_block || !stage->output_block) {
        faar_dft_fir_clear(stage);
        return -1;
    }
    for (size_t j = 0; j < tap_count / 4; ++j)
        stage->filter_spectrum[j] = taps[2 * j + 1];
    stage->filter_spectrum[tap_count / 4] = taps[tap_count / 2];
    return 0;
}

void faar_dft_fir_clear(faar_dft_fir *stage) {
    if (!stage) return;
    free(stage->filter_spectrum);
    free(stage->work);
    free(stage->overlap);
    free(stage->input_block);
    free(stage->output_block);
    faar_fft_clear(&stage->in);
    faar_fft_clear(&stage->out);
    free(stage->spec);
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
    if (!stage || !input || !output || !stage->overlap || !stage->spec)
        return -1;
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
