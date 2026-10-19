/*
 * Independently implemented overlap-save FIR stage for FAAR's staged CPU
 * resampler. The block geometry follows standard linear convolution.
 */

#include "dft_fir.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef FAAR_OOURA_REAL_FFT
static void fold_real_spectrum_by_two(double *data, size_t n);
#endif

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
    if (!stage || !taps || !tap_count || tap_count > fft_size
            || fft_size > SIZE_MAX / (2 * sizeof(double))
            || (expansion != 1 && expansion != 2)
            || (decimation != 1 && decimation != 2 && decimation != 4)
            || (fft_size - tap_count + 1) % expansion
            || (tap_count - 1) % expansion
            || (fft_size - tap_count + 1) % decimation
            || (tap_count - 1) % decimation)
        return -1;
    memset(stage, 0, sizeof(*stage));
    if (faar_real_fft_plan_init(&stage->fft, fft_size)) return -1;

    stage->fft_size = fft_size;
    stage->tap_count = tap_count;
    stage->expansion = expansion;
    stage->decimation = decimation;
    stage->block_size = fft_size - tap_count + 1;
    stage->output_block_size = stage->block_size / decimation;
    stage->input_block_size = stage->block_size / expansion;
    if (expansion == 2) {
        if (faar_real_fft_plan_init(&stage->input_fft, fft_size / 2)) {
            faar_dft_fir_clear(stage);
            return -1;
        }
        stage->input_spectrum = (double *)calloc(fft_size / 2 + 2,
                                                  sizeof(double));
    }
#ifdef FAAR_OOURA_REAL_FFT
    if (decimation == 2
            && faar_real_fft_plan_init(&stage->decimated_fft, fft_size / 2)) {
        faar_dft_fir_clear(stage);
        return -1;
    }
    if (decimation == 4
            && faar_real_fft_plan_init(&stage->decimated_fft, fft_size / 4)) {
        faar_dft_fir_clear(stage);
        return -1;
    }
#endif
    stage->filter_spectrum = (double *)calloc(fft_size + 2, sizeof(double));
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
    if (expansion == 2 && !stage->input_spectrum) {
        faar_dft_fir_clear(stage);
        return -1;
    }
    for (size_t i = 0; i < tap_count; ++i)
        stage->filter_spectrum[i] = taps[i];
    faar_real_fft_execute_packed(&stage->fft, stage->filter_spectrum, 0);
    return 0;
}

void faar_dft_fir_clear(faar_dft_fir *stage) {
    if (!stage) return;
    faar_real_fft_plan_clear(&stage->fft);
    faar_real_fft_plan_clear(&stage->input_fft);
    faar_real_fft_plan_clear(&stage->decimated_fft);
    free(stage->filter_spectrum);
    free(stage->work);
    free(stage->overlap);
    free(stage->input_block);
    free(stage->input_spectrum);
    free(stage->output_block);
    memset(stage, 0, sizeof(*stage));
}

int faar_dft_fir_process(faar_dft_fir *stage, const double *input,
                         double *output) {
    if (!stage || !input || !output || !stage->work
            || !stage->filter_spectrum || !stage->overlap)
        return -1;
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
#ifdef FAAR_OOURA_REAL_FFT
        /* Fold frequency bins before the inverse transform. This computes
           every second convolution sample with an N/2 inverse FFT. */
        fold_real_spectrum_by_two(stage->work, n);
        faar_real_fft_execute_packed(&stage->decimated_fft,
                                     stage->work, 1);
        for (size_t i = 0; i < stage->output_block_size; ++i)
            output[i] = stage->work[overlap / 2 + i];
#else
        /* The portable real FFT backend does not expose the same packed-bin
           convention for alias folding. Keep this backend exact by using its
           full inverse and selecting the requested output phase. */
        faar_real_fft_execute_packed(&stage->fft, stage->work, 1);
        for (size_t i = 0; i < stage->output_block_size; ++i)
            output[i] = stage->work[overlap + 2 * i];
#endif
    } else if (stage->decimation == 4) {
#ifdef FAAR_OOURA_REAL_FFT
        /* Two spectral folds compute every fourth convolution sample with
           one N/4 inverse transform. */
        fold_real_spectrum_by_two(stage->work, n);
        fold_real_spectrum_by_two(stage->work, n / 2);
        faar_real_fft_execute_packed(&stage->decimated_fft,
                                     stage->work, 1);
        for (size_t i = 0; i < stage->output_block_size; ++i)
            output[i] = stage->work[overlap / 4 + i];
#else
        faar_real_fft_execute_packed(&stage->fft, stage->work, 1);
        for (size_t i = 0; i < stage->output_block_size; ++i)
            output[i] = stage->work[overlap + 4 * i];
#endif
    } else {
        faar_real_fft_execute_packed(&stage->fft, stage->work, 1);
        for (size_t i = 0; i < block; ++i)
            output[i] = stage->work[overlap + i];
    }

    if (overlap) {
        size_t stored_overlap = overlap / stage->expansion;
        if (stage->input_block_size >= stored_overlap) {
            memcpy(stage->overlap,
                   input + stage->input_block_size - stored_overlap,
                   stored_overlap * sizeof(double));
        } else {
            memmove(stage->overlap,
                    stage->overlap + stage->input_block_size,
                    (stored_overlap - stage->input_block_size)
                        * sizeof(double));
            memcpy(stage->overlap + stored_overlap - stage->input_block_size,
                   input, stage->input_block_size * sizeof(double));
        }
    }
    return 0;
}

void faar_dft_fir_reset(faar_dft_fir *stage) {
    if (!stage || !stage->overlap) return;
    memset(stage->overlap, 0, ((stage->tap_count - 1) / stage->expansion
            ? (stage->tap_count - 1) / stage->expansion : 1) * sizeof(double));
    stage->input_fill = 0;
    stage->output_count = 0;
    stage->output_offset = 0;
    stage->finishing = 0;
    stage->finished = 0;
}

static size_t stream_drain(faar_dft_fir *stage, double *output,
                           size_t capacity) {
    size_t remaining = stage->output_count - stage->output_offset;
    size_t count = remaining < capacity ? remaining : capacity;
    if (count) {
        memcpy(output, stage->output_block + stage->output_offset,
               count * sizeof(double));
        stage->output_offset += count;
    }
    if (stage->output_offset == stage->output_count) {
        stage->output_offset = 0;
        stage->output_count = 0;
    }
    return count;
}

#ifdef FAAR_OOURA_REAL_FFT
static void fold_real_spectrum_by_two(double *data, size_t n) {
    const size_t half = n / 2;
    const double dc = data[0];
    const double nyquist = data[1];
    data[0] = 0.5 * (dc + nyquist);
    for (size_t k = 1; k < half / 2; ++k) {
        const size_t a = 2 * k;
        const size_t b = n - 2 * k;
        const double ar = data[a];
        const double ai = data[a + 1];
        const double br = data[b];
        const double bi = data[b + 1];
        data[a] = 0.5 * (ar + br);
        data[a + 1] = 0.5 * (ai - bi);
    }
    data[1] = data[half];
}
#endif

int faar_dft_fir_stream(faar_dft_fir *stage, const double *input,
                        size_t input_count, size_t *consumed,
                        double *output, size_t output_capacity,
                        size_t *produced) {
    if (!stage || !consumed || !produced || (input_count && !input)
            || (output_capacity && !output) || stage->finishing
            || stage->finished) return -1;
    *consumed = 0;
    *produced = 0;
    *produced += stream_drain(stage, output, output_capacity);
    while (*consumed < input_count) {
        if (stage->output_count) break;
        size_t available = stage->input_block_size - stage->input_fill;
        size_t count = input_count - *consumed;
        if (count > available) count = available;
        memcpy(stage->input_block + stage->input_fill, input + *consumed,
               count * sizeof(double));
        stage->input_fill += count;
        *consumed += count;
        if (stage->input_fill == stage->input_block_size) {
            if (faar_dft_fir_process(stage, stage->input_block,
                                     stage->output_block)) return -1;
            stage->input_fill = 0;
            stage->output_count = stage->output_block_size;
            *produced += stream_drain(stage,
                output ? output + *produced : NULL,
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
    *produced += stream_drain(stage, output, output_capacity);
    if (!stage->output_count && stage->input_fill) {
        const size_t real_count = stage->input_fill;
        memset(stage->input_block + real_count, 0,
               (stage->input_block_size - real_count) * sizeof(double));
        if (faar_dft_fir_process(stage, stage->input_block,
                                 stage->output_block)) return -1;
        stage->input_fill = 0;
        stage->output_offset = 0;
        stage->output_count = (real_count * stage->expansion
                + stage->decimation - 1) / stage->decimation;
        *produced += stream_drain(stage,
            output ? output + *produced : NULL,
            output_capacity - *produced);
    }
    if (!stage->input_fill && !stage->output_count) stage->finished = 1;
    return 0;
}
