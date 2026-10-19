#include "dft_coeffs.h"
#include "dft_prefilter.h"
#include "libfaar_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static int run_boundary(int hold, size_t tap_count, size_t fft_size,
                        size_t frames, size_t input_chunk,
                        size_t output_capacity,
                        size_t channels) {
    const size_t CHANNELS = channels;
    double *taps = (double *)malloc(tap_count * sizeof(*taps));
    float *input = (float *)malloc(frames * CHANNELS * sizeof(*input));
    float *output = (float *)malloc(frames * CHANNELS * sizeof(*output));
    float *chunk = (float *)malloc(output_capacity * CHANNELS * sizeof(*chunk));
    if (!taps || !input || !output || !chunk) {
        free(taps); free(input); free(output); free(chunk);
        return fail("test buffers allocation failed");
    }
    for (size_t i = 0; i < frames; ++i)
        for (size_t c = 0; c < CHANNELS; ++c)
            input[i * CHANNELS + c] = (float)(0.2 * sin(0.071 * i + c * 0.4)
                                                   + c * 0.1);
    if (faar_dft_design_lowpass(taps, tap_count, 0.18, 5.0)) {
        free(taps); free(input); free(output); free(chunk);
        return fail("test filter design failed");
    }
    faar_dft_prefilter *stage = faar_dft_prefilter_create_rate(CHANNELS, taps,
            tap_count, fft_size, 1, 1);
    if (!stage || faar_dft_prefilter_set_boundary(stage, hold)) {
        faar_dft_prefilter_destroy(stage);
        free(taps); free(input); free(output); free(chunk);
        return fail("prefilter create or boundary setup failed");
    }
    size_t in_at = 0, out_at = 0;
    while (in_at < frames) {
        size_t count = frames - in_at;
        if (count > input_chunk) count = input_chunk;
        size_t produced = 0;
        if (faar_dft_prefilter_process(stage, input + in_at * CHANNELS,
                count, chunk, output_capacity, &produced)) {
            faar_dft_prefilter_destroy(stage);
            free(taps); free(input); free(output); free(chunk);
            return fail("prefilter process failed");
        }
        for (size_t c = 0; c < CHANNELS; ++c)
            memcpy(output + c * frames + out_at,
                   chunk + c * produced, produced * sizeof(*output));
        in_at += count;
        out_at += produced;
    }
    for (;;) {
        size_t produced = 0;
        if (faar_dft_prefilter_flush(stage, chunk,
                                     output_capacity, &produced)) {
            faar_dft_prefilter_destroy(stage);
            free(taps); free(input); free(output); free(chunk);
            return fail("prefilter flush failed");
        }
        for (size_t c = 0; c < CHANNELS; ++c)
            memcpy(output + c * frames + out_at,
                   chunk + c * produced, produced * sizeof(*output));
        out_at += produced;
        if (!produced) break;
    }
    if (out_at != frames) {
        faar_dft_prefilter_destroy(stage);
        free(taps); free(input); free(output); free(chunk);
        return fail("prefilter output count differs from input count");
    }
    for (size_t n = 0; n < frames; ++n) {
        for (size_t c = 0; c < CHANNELS; ++c) {
            double expected = 0.0;
            for (size_t k = 0; k < tap_count; ++k) {
                long index = (long)n + (long)(tap_count - 1) / 2 - (long)k;
                double sample;
                if (index < 0)
                    sample = hold ? input[c] : 0.0;
                else if ((size_t)index >= frames)
                    sample = hold ? input[(frames - 1) * CHANNELS + c] : 0.0;
                else sample = input[(size_t)index * CHANNELS + c];
                expected += taps[k] * sample;
            }
            if (fabs(output[c * frames + n] - expected) > (sizeof(faar_sample_t) == sizeof(float) ? 2e-6 : 2e-7)) {
                fprintf(stderr, "boundary %d frame %zu channel %zu\n",
                        hold, n, c);
                faar_dft_prefilter_destroy(stage);
                free(taps); free(input); free(output); free(chunk);
                return fail("centered prefilter output differs from direct convolution");
            }
        }
    }
    size_t produced = 1;
    if (faar_dft_prefilter_flush(stage, chunk, output_capacity, &produced) || produced) {
        faar_dft_prefilter_destroy(stage);
        free(taps); free(input); free(output); free(chunk);
        return fail("repeated prefilter flush was not empty");
    }
    faar_dft_prefilter_reset(stage);
    faar_dft_prefilter_destroy(stage);
    free(taps); free(input); free(output); free(chunk);
    return 0;
}

int main(void) {
    /* With float pairs, one channel stacks blocks, two share a transform and
     * three make a pair plus a lone lane. */
        for (size_t channels = 1; channels <= 3; ++channels)
            if (run_boundary(0, 31, 128, 257, 17, 7, channels)
                    || run_boundary(1, 31, 128, 257, 17, 7, channels)
                    || run_boundary(0, 601, 4096, 4000, 257, 97, channels)
                    || run_boundary(1, 601, 4096, 4000, 257, 97, channels))
                return 1;
    puts("DFT prefilter: centered alignment, short/601-tap plans, multi-channel isolation, boundaries, chunking and drain passed");
    return 0;
}
