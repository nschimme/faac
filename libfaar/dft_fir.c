/*
 * Independently implemented overlap-save FIR stage for FAAR's staged CPU
 * resampler. The block geometry follows standard linear convolution.
 */

#include "dft_fir.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int init_stage(faar_dft_fir *stage, const double *taps,
                      size_t tap_count, size_t fft_size, size_t expansion,
                      size_t decimation, size_t lanes, int stacked,
                      int single);


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
    return init_stage(stage, taps, tap_count, fft_size, expansion,
                      decimation, 0, 0, 0);
}

#ifdef FAAR_PAIRED_FFT
int faar_dft_fir_init_paired(faar_dft_fir *stage, const double *taps,
                      size_t tap_count, size_t fft_size, size_t expansion,
                      size_t decimation, size_t lanes, int stacked,
                      int single) {
    if ((lanes != 1 && lanes != 2) || (stacked && lanes != 1)) return -1;
    return init_stage(stage, taps, tap_count, fft_size, expansion,
                      decimation, lanes, stacked, single);
}
#endif

static size_t overlap_stride(const faar_dft_fir *stage) {
    size_t stored = (stage->tap_count - 1) / stage->expansion;
    return stored ? stored : 1;
}

#ifdef FAAR_PAIRED_FFT
#define PAIR_REAL float
#define PAIR_PLAN faar_rfftf_plan
#define PAIR_TAG _f
#define PAIR_FFT faar_rfftf_
#include "dft_fir_pair.inc"
#undef PAIR_REAL
#undef PAIR_PLAN
#undef PAIR_TAG
#undef PAIR_FFT
#define PAIR_REAL double
#define PAIR_PLAN faar_rfftd_plan
#define PAIR_TAG _d
#define PAIR_FFT faar_rfftd_
#include "dft_fir_pair.inc"
#undef PAIR_REAL
#undef PAIR_PLAN
#undef PAIR_TAG
#undef PAIR_FFT

/* Filter spectrum in the e^{-j} convention, pre-scaled by the inverse FFT
 * and decimation averaging. Both scales are powers of two, so exact. It is
 * always computed in double and rounded once to the stage's precision. */
static int init_paired(faar_dft_fir *stage, const double *taps) {
    const size_t n = stage->fft_size, nout = n / stage->decimation;
    const double scale = 1.0 / ((double)nout * (double)stage->decimation);
    faar_rfftd_plan plan;
    double *h, *spectrum;
    int bad;
    if (n > (size_t)INT_MAX || faar_rfftd_cinit(&plan, (int)n)) return -1;
    h = faar_rfftd_cbuffer(&plan, 0);
    memset(h, 0, 2 * n * sizeof(double));
    memcpy(h, taps, stage->tap_count * sizeof(double));
    spectrum = (double *)malloc(2 * n * sizeof(double));
    if (!spectrum) { faar_rfftd_clear(&plan); return -1; }
    h = faar_rfftd_cforward(&plan, h);
    for (size_t i = 0; i < 2 * n; ++i) spectrum[i] = h[i] * scale;
    faar_rfftd_clear(&plan);
    bad = stage->single ? init_transforms_f(stage, spectrum)
                        : init_transforms_d(stage, spectrum);
    free(spectrum);
    if (bad) return -1;
    stage->overlap = (double *)calloc(stage->lanes * overlap_stride(stage),
                                      sizeof(double));
    stage->input_block = (double *)calloc(stage->lanes
            * stage->stream_in, sizeof(double));
    stage->output_block = (double *)calloc(stage->lanes
            * stage->stream_out, sizeof(double));
    return stage->overlap && stage->input_block && stage->output_block
        ? 0 : -1;
}
#endif

static int init_stage(faar_dft_fir *stage, const double *taps,
                      size_t tap_count, size_t fft_size, size_t expansion,
                      size_t decimation, size_t lanes, int stacked,
                      int single) {
    int halfband = fft_size == 0;
    if (halfband) {
        if (tap_count < 5 || (tap_count - 1) % 4
                || tap_count > SIZE_MAX - 255 || expansion != 1 || decimation != 2)
            return -1;
        fft_size = 256 + tap_count - 1;
    }
    if (!stage || !taps || !tap_count || tap_count > fft_size
            || fft_size > SIZE_MAX / (2 * sizeof(double))
            || (expansion != 1 && expansion != 2)
            || (decimation != 1 && decimation != 2 && decimation != 4)
            || (fft_size - tap_count + 1) % expansion
            || (tap_count - 1) % expansion
            || (fft_size - tap_count + 1) % decimation
            || (tap_count - 1) % decimation)
        return -1;
    if (lanes && (halfband || fft_size / expansion > (size_t)INT_MAX))
        return -1;
#if defined(FAAR_PAIRED_FFT) && !defined(FAAR_TEST_UNPAIRED)
    if (!lanes && !halfband) return -1;
#endif
    memset(stage, 0, sizeof(*stage));
    stage->lanes = lanes ? lanes : 1;
    stage->paired = lanes != 0;
    stage->single = single;
#if !defined(FAAR_PAIRED_FFT) || defined(FAAR_TEST_UNPAIRED)
    if (!lanes && !halfband
            && faar_real_fft_plan_init(&stage->fft, fft_size)) return -1;
#endif
    stage->halfband = halfband;

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
#ifdef FAAR_PAIRED_FFT
    if (lanes) {
        if (init_paired(stage, taps)) {
            faar_dft_fir_clear(stage);
            return -1;
        }
        return 0;
    }
#endif
#if !defined(FAAR_PAIRED_FFT) || defined(FAAR_TEST_UNPAIRED)
    if (expansion == 2) {
        if (faar_real_fft_plan_init(&stage->input_fft, fft_size / 2)) {
            faar_dft_fir_clear(stage);
            return -1;
        }
        stage->input_spectrum = (double *)calloc(fft_size / 2 + 2,
                                                  sizeof(double));
    }
#endif
    stage->filter_spectrum = (double *)calloc(halfband ? tap_count / 4 + 1
                                                   : fft_size + 2, sizeof(double));
    stage->work = (double *)calloc(fft_size + 2, sizeof(double));
    stage->overlap = (double *)calloc((tap_count - 1) / expansion
                                      ? (tap_count - 1) / expansion : 1,
                                      sizeof(double));
    stage->input_block = (double *)calloc(stage->input_block_size,
                                          sizeof(double));
    stage->output_block = (double *)calloc(stage->block_size, sizeof(double));
    if (!stage->filter_spectrum || !stage->work || !stage->overlap
            || !stage->input_block || !stage->output_block) {
        faar_dft_fir_clear(stage);
        return -1;
    }
#if !defined(FAAR_PAIRED_FFT) || defined(FAAR_TEST_UNPAIRED)
    if (expansion == 2 && !stage->input_spectrum) {
        faar_dft_fir_clear(stage);
        return -1;
    }
#endif
    if (halfband) {
        for (size_t j = 0; j < tap_count / 4; ++j)
            stage->filter_spectrum[j] = taps[2 * j + 1];
        stage->filter_spectrum[tap_count / 4] = taps[tap_count / 2];
    }
#if !defined(FAAR_PAIRED_FFT) || defined(FAAR_TEST_UNPAIRED)
    else {
        for (size_t i = 0; i < tap_count; ++i)
            stage->filter_spectrum[i] = taps[i];
        faar_real_fft_execute_packed(&stage->fft, stage->filter_spectrum, 0);
    }
#endif
    return 0;
}

void faar_dft_fir_clear(faar_dft_fir *stage) {
    if (!stage) return;
#if !defined(FAAR_PAIRED_FFT) || defined(FAAR_TEST_UNPAIRED)
    faar_real_fft_plan_clear(&stage->fft);
    faar_real_fft_plan_clear(&stage->input_fft);
#endif
    free(stage->filter_spectrum);
    free(stage->work);
    free(stage->overlap);
    free(stage->input_block);
#if !defined(FAAR_PAIRED_FFT) || defined(FAAR_TEST_UNPAIRED)
    free(stage->input_spectrum);
#endif
    free(stage->output_block);
#ifdef FAAR_PAIRED_FFT
    faar_rfftf_clear(&stage->in_f);
    faar_rfftf_clear(&stage->out_f);
    faar_rfftd_clear(&stage->in_d);
    faar_rfftd_clear(&stage->out_d);
#endif
    free(stage->spec_f);
    free(stage->spec_d);
    memset(stage, 0, sizeof(*stage));
}

#if !defined(FAAR_PAIRED_FFT) || defined(FAAR_TEST_UNPAIRED)
static void convolve_double(faar_dft_fir *stage, const double *input,
                            double *output) {
    const size_t n = stage->fft_size;
    const size_t overlap = stage->tap_count - 1;
    const size_t block = stage->block_size;
    if (stage->expansion == 1) {
        for (size_t i = 0; i < overlap; ++i)
            stage->work[i] = stage->overlap[i];
        for (size_t i = 0; i < block; ++i)
            stage->work[overlap + i] = input[i];
        faar_real_fft_execute_packed(&stage->fft, stage->work, 0);
    } else {
        const size_t half = n / 2;
        const size_t raw_overlap = overlap / 2;
        for (size_t i = 0; i < raw_overlap; ++i)
            stage->input_spectrum[i] = stage->overlap[i];
        for (size_t i = 0; i < stage->input_block_size; ++i)
            stage->input_spectrum[raw_overlap + i] = input[i];
        faar_real_fft_execute_packed(&stage->input_fft,
                                     stage->input_spectrum, 0);
        /* Zero insertion repeats the input spectrum at twice the FFT size.
           Build directly in the packed-real layout: DC and the new Nyquist
           bin share the input DC, then copy and mirror the half-size bins. */
        const size_t nyquist = half / 2;
        stage->work[0] = stage->input_spectrum[0];
        stage->work[1] = stage->input_spectrum[0];
        if (nyquist > 1)
            memcpy(stage->work + 2, stage->input_spectrum + 2,
                   2 * (nyquist - 1) * sizeof(double));
        stage->work[2 * nyquist] = stage->input_spectrum[1];
        stage->work[2 * nyquist + 1] = 0.0;
        for (size_t k = nyquist + 1; k < half; ++k) {
            size_t mirror = half - k;
            stage->work[2 * k] = stage->input_spectrum[2 * mirror];
            stage->work[2 * k + 1] = -stage->input_spectrum[2 * mirror + 1];
        }
    }
    stage->work[0] *= stage->filter_spectrum[0];
    stage->work[1] *= stage->filter_spectrum[1];
    for (size_t i = 2; i < n; i += 2) {
        double xr = stage->work[i];
        double xi = stage->work[i + 1];
        double hr = stage->filter_spectrum[i];
        double hi = stage->filter_spectrum[i + 1];
        stage->work[i] = xr * hr - xi * hi;
        stage->work[i + 1] = xr * hi + xi * hr;
    }
    if (stage->decimation == 2) {
        /* The portable real FFT backend does not expose the same packed-bin
           convention for alias folding. Keep this backend exact by using its
           full inverse and selecting the requested output phase. */
        faar_real_fft_execute_packed(&stage->fft, stage->work, 1);
        for (size_t i = 0; i < stage->output_block_size; ++i)
            output[i] = stage->work[overlap + 2 * i];
    } else if (stage->decimation == 4) {
        faar_real_fft_execute_packed(&stage->fft, stage->work, 1);
        for (size_t i = 0; i < stage->output_block_size; ++i)
            output[i] = stage->work[overlap + 4 * i];
    } else {
        faar_real_fft_execute_packed(&stage->fft, stage->work, 1);
        for (size_t i = 0; i < block; ++i)
            output[i] = stage->work[overlap + i];
    }
}

#endif

static void update_overlap(faar_dft_fir *stage, const double *input) {
    const size_t overlap = stage->tap_count - 1;
    if (!overlap) return;
    for (size_t lane = 0; lane < stage->lanes; ++lane) {
        const size_t block = stage->stream_in;
        const double *in = input + lane * block;
        double *keep = stage->overlap + lane * overlap_stride(stage);
        size_t stored_overlap = overlap / stage->expansion;
        if (block >= stored_overlap) {
            memcpy(keep, in + block - stored_overlap,
                   stored_overlap * sizeof(double));
        } else {
            memmove(keep, keep + block,
                    (stored_overlap - block) * sizeof(double));
            memcpy(keep + stored_overlap - block, in, block * sizeof(double));
        }
    }
}

int faar_dft_fir_process(faar_dft_fir *stage, const double *input,
                         double *output) {
    if (stage && stage->halfband && input && output) {
        const size_t n = stage->tap_count;
        const size_t block = stage->block_size;
        memcpy(stage->work, stage->overlap, (n - 1) * sizeof(double));
        memcpy(stage->work + n - 1, input, block * sizeof(double));
        for (size_t i = 0; i < block / 2; ++i)
            output[i] = stage->filter_spectrum[n / 4] * stage->work[2 * i + n / 2];
        /* Vectorize across independent outputs, without reassociating a
         * floating-point reduction or requiring compiler-specific flags. */
        for (size_t j = 0; j < n / 4; ++j) {
            const double coefficient = stage->filter_spectrum[j];
            const double *x = stage->work + 2 * j + 1;
            const double *y = stage->work + n - 2 - 2 * j;
            for (size_t i = 0; i < block / 2; ++i)
                output[i] += coefficient * (x[2 * i] + y[2 * i]);
        }
        memcpy(stage->overlap, stage->work + block, (n - 1) * sizeof(double));
        return 0;
    }
    if (!stage || !input || !output || !stage->overlap
            || (stage->paired ? !stage->spec_f && !stage->spec_d
                              : !stage->work || !stage->filter_spectrum))
        return -1;
#ifdef FAAR_PAIRED_FFT
    if (stage->paired && stage->single)
        convolve_paired_f(stage, input, output);
    else if (stage->paired) convolve_paired_d(stage, input, output);
    else
#endif
#if !defined(FAAR_PAIRED_FFT) || defined(FAAR_TEST_UNPAIRED)
    convolve_double(stage, input, output);
#else
        return -1;
#endif
    update_overlap(stage, input);
    return 0;
}

void faar_dft_fir_reset(faar_dft_fir *stage) {
    if (!stage || !stage->overlap) return;
    memset(stage->overlap, 0, stage->lanes * overlap_stride(stage)
                                  * sizeof(double));
    stage->input_fill = 0;
    stage->output_count = 0;
    stage->output_offset = 0;
    stage->finishing = 0;
    stage->finished = 0;
}

/* Lane l of the caller's output starts l * stride samples in. */
static size_t stream_drain(faar_dft_fir *stage, double *output, size_t stride,
                           size_t capacity) {
    size_t remaining = stage->output_count - stage->output_offset;
    size_t count = remaining < capacity ? remaining : capacity;
    if (count) {
        for (size_t lane = 0; lane < stage->lanes; ++lane)
            memcpy(output + lane * stride,
                   stage->output_block + lane * stage->stream_out
                       + stage->output_offset, count * sizeof(double));
        stage->output_offset += count;
    }
    if (stage->output_offset == stage->output_count) {
        stage->output_offset = 0;
        stage->output_count = 0;
    }
    return count;
}


int faar_dft_fir_stream(faar_dft_fir *stage, const double *input,
                        size_t input_count, size_t *consumed,
                        double *output, size_t output_capacity,
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
                   count * sizeof(double));
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

int faar_dft_fir_flush(faar_dft_fir *stage, double *output,
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
                   (stage->stream_in - real_count) * sizeof(double));
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
