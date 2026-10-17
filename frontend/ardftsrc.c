/*
 * ARDFTSRC - Arbitrary Rate Discrete Fourier Transform Sample Rate Converter
 * Polyphase Table & Cache Footprint-Optimized C Translation for FAAC
 */

#include "ardftsrc.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define ARDFTSRC_NUM_PHASES 256 /* Fine fractional phase resolution for pre-calculated polyphase bank */
#define ARDFTSRC_STORED_PHASES (ARDFTSRC_NUM_PHASES / 2 + 1) /* 129 phases stored using linear phase symmetry */

/* Modified Bessel function I0(x) for Kaiser window calculation */
static double bessel_i0(double x) {
    double sum = 1.0;
    double u = 1.0;
    double half_x = x / 2.0;
    for (int k = 1; k < 30; k++) {
        u *= (half_x / k) * (half_x / k);
        sum += u;
        if (u < sum * 1e-15) break;
    }
    return sum;
}

/* Calculate Kaiser window coefficient */
static double kaiser_window(double n, double N, double beta) {
    if (beta <= 0.0) return 1.0;
    double arg = 2.0 * n / N - 1.0;
    double sq = 1.0 - arg * arg;
    if (sq <= 0.0) return 0.0;
    return bessel_i0(beta * sqrt(sq)) / bessel_i0(beta);
}

struct ArdftsrcResampler {
    uint32_t in_rate;
    uint32_t out_rate;
    uint16_t channels;
    int quality;
    size_t filter_len;
    double ratio;               /* out_rate / in_rate */
    double phase;               /* Fractional sample position [0.0, 1.0) */
    double beta;
    float *polyphase_table;     /* Symmetric polyphase filter bank table [129 x filter_len] */
    float **hist;               /* Per-channel overlap/history buffer */
    size_t hist_len;            /* Current frames in hist */
    size_t delay;
};

ArdftsrcResampler *ardftsrc_create(uint32_t in_rate, uint32_t out_rate, uint16_t channels, int quality) {
    if (in_rate == 0 || out_rate == 0 || channels == 0) return NULL;

    ArdftsrcResampler *r = (ArdftsrcResampler *)calloc(1, sizeof(ArdftsrcResampler));
    if (!r) return NULL;

    r->in_rate = in_rate;
    r->out_rate = out_rate;
    r->channels = channels;
    r->quality = quality;
    r->ratio = (double)out_rate / (double)in_rate;

    /* Filter length (taper window length) based on quality preset */
    switch (quality) {
        case 0:  r->filter_len = 127; r->beta = 24.0; break;
        case 2:  r->filter_len = 511; r->beta = 34.0; break;
        case 1:
        default: r->filter_len = 255; r->beta = 30.0; break;
    }

    /* Ensure filter length is odd for symmetric linear phase FIR */
    if ((r->filter_len % 2) == 0) r->filter_len++;

    /* Allocate 129-phase table (131 KB for 255 taps) */
    r->polyphase_table = (float *)malloc(ARDFTSRC_STORED_PHASES * r->filter_len * sizeof(float));
    if (!r->polyphase_table) {
        free(r);
        return NULL;
    }

    /* Calculate cutoff frequency with anti-aliasing guard band */
    double cutoff = (r->ratio < 1.0) ? 0.45 * r->ratio : 0.45;
    double half_len = (r->filter_len - 1) / 2.0;

    /* Pre-compute polyphase filter bank table across 129 phase steps [0.0 to 0.5] */
    for (size_t p = 0; p < ARDFTSRC_STORED_PHASES; p++) {
        double frac = (double)p / (double)ARDFTSRC_NUM_PHASES;
        float *phase_coeffs = r->polyphase_table + p * r->filter_len;
        double sum = 0.0;

        for (size_t j = 0; j < r->filter_len; j++) {
            double n = (double)j - half_len - frac;
            double sinc = (fabs(n) < 1e-9) ? (2.0 * cutoff) : sin(2.0 * M_PI * cutoff * n) / (M_PI * n);
            double win = kaiser_window((double)j, (double)r->filter_len - 1.0, r->beta);
            double val = sinc * win;
            phase_coeffs[j] = (float)val;
            sum += val;
        }

        /* Normalize phase slice for DC unity gain */
        if (sum > 0.0) {
            for (size_t j = 0; j < r->filter_len; j++) {
                phase_coeffs[j] /= (float)sum;
            }
        }
    }

    r->hist = (float **)malloc(channels * sizeof(float *));
    if (!r->hist) {
        free(r->polyphase_table);
        free(r);
        return NULL;
    }

    /* Allocate history buffer per channel */
    r->hist_len = 0;
    size_t hist_alloc = r->filter_len * 4 + 16384;
    for (uint16_t c = 0; c < channels; c++) {
        r->hist[c] = (float *)calloc(hist_alloc, sizeof(float));
        if (!r->hist[c]) {
            for (uint16_t k = 0; k < c; k++) free(r->hist[k]);
            free(r->hist);
            free(r->polyphase_table);
            free(r);
            return NULL;
        }
    }

    r->delay = (size_t)half_len;
    return r;
}

size_t ardftsrc_get_delay(const ArdftsrcResampler *r) {
    return r ? r->delay : 0;
}

size_t ardftsrc_process(ArdftsrcResampler * restrict r, const float * restrict in_samples, size_t in_count,
                        float * restrict out_samples, size_t max_out_count) {
    if (!r || !out_samples) return 0;
    if (r->in_rate == r->out_rate) {
        /* Passthrough mode if rates match */
        size_t frames_to_copy = in_count < max_out_count ? in_count : max_out_count;
        if (in_samples && frames_to_copy > 0) {
            memcpy(out_samples, in_samples, frames_to_copy * r->channels * sizeof(float));
        }
        return frames_to_copy;
    }

    const size_t filter_len = r->filter_len;
    const size_t half_len = (filter_len - 1) / 2;
    const float * restrict poly_table = r->polyphase_table;

    /* Append input samples to channel history buffers */
    if (in_samples && in_count > 0) {
        for (size_t i = 0; i < in_count; i++) {
            for (uint16_t c = 0; c < r->channels; c++) {
                r->hist[c][r->hist_len + i] = in_samples[i * r->channels + c];
            }
        }
        r->hist_len += in_count;
    }

    size_t out_frames = 0;
    const double step = 1.0 / r->ratio; /* Input step per output sample */

    /* Process resampling output samples leveraging 256-step polyphase filter bank */
    while (out_frames < max_out_count) {
        double in_pos = r->phase;
        size_t base_idx = (size_t)floor(in_pos);

        if (base_idx + filter_len > r->hist_len) {
            break; /* Need more input samples */
        }

        double frac = in_pos - (double)base_idx;
        size_t phase_idx = (size_t)(frac * (double)ARDFTSRC_NUM_PHASES + 0.5);
        if (phase_idx >= ARDFTSRC_NUM_PHASES) phase_idx = ARDFTSRC_NUM_PHASES - 1;

        if (phase_idx == 0) {
            /* Fast path for phase 0 (exact integer alignment): fold symmetric taps to halve mults */
            const float * restrict coeffs = poly_table; /* Phase 0 slice */
            const float center_coeff = coeffs[half_len];

            for (uint16_t c = 0; c < r->channels; c++) {
                const float * restrict h = &r->hist[c][base_idx];
                float sum = center_coeff * h[half_len];

                size_t j = 0;
                for (; j + 3 < half_len; j += 4) {
                    sum += coeffs[j + 0] * (h[j + 0] + h[filter_len - 1 - (j + 0)]) +
                           coeffs[j + 1] * (h[j + 1] + h[filter_len - 1 - (j + 1)]) +
                           coeffs[j + 2] * (h[j + 2] + h[filter_len - 1 - (j + 2)]) +
                           coeffs[j + 3] * (h[j + 3] + h[filter_len - 1 - (j + 3)]);
                }
                for (; j < half_len; j++) {
                    sum += coeffs[j] * (h[j] + h[filter_len - 1 - j]);
                }

                out_samples[out_frames * r->channels + c] = sum;
            }
        } else {
            /* 256-phase polyphase path (stored as 129 phase slices using linear phase symmetry) */
            bool is_mirrored = false;
            if (phase_idx > ARDFTSRC_NUM_PHASES / 2) {
                phase_idx = ARDFTSRC_NUM_PHASES - phase_idx;
                is_mirrored = true;
            }

            const float * restrict coeffs = poly_table + phase_idx * filter_len;

            for (uint16_t c = 0; c < r->channels; c++) {
                float sum = 0.0f;
                const float * restrict h = &r->hist[c][base_idx];

                if (!is_mirrored) {
                    /* Forward filter convolution loop */
                    size_t j = 0;
                    for (; j + 3 < filter_len; j += 4) {
                        sum += h[j + 0] * coeffs[j + 0] +
                               h[j + 1] * coeffs[j + 1] +
                               h[j + 2] * coeffs[j + 2] +
                               h[j + 3] * coeffs[j + 3];
                    }
                    for (; j < filter_len; j++) {
                        sum += h[j] * coeffs[j];
                    }
                } else {
                    /* Mirrored filter convolution loop utilizing linear phase symmetry */
                    size_t j = 0;
                    for (; j + 3 < filter_len; j += 4) {
                        sum += h[j + 0] * coeffs[filter_len - 1 - (j + 0)] +
                               h[j + 1] * coeffs[filter_len - 1 - (j + 1)] +
                               h[j + 2] * coeffs[filter_len - 1 - (j + 2)] +
                               h[j + 3] * coeffs[filter_len - 1 - (j + 3)];
                    }
                    for (; j < filter_len; j++) {
                        sum += h[j] * coeffs[filter_len - 1 - j];
                    }
                }

                out_samples[out_frames * r->channels + c] = sum;
            }
        }

        out_frames++;
        r->phase += step;
    }

    /* Shift consumed history buffer */
    size_t consumed = (size_t)floor(r->phase);
    if (consumed > 0 && consumed <= r->hist_len) {
        for (uint16_t c = 0; c < r->channels; c++) {
            memmove(r->hist[c], r->hist[c] + consumed, (r->hist_len - consumed) * sizeof(float));
        }
        r->hist_len -= consumed;
        r->phase -= (double)consumed;
    }

    return out_frames;
}

size_t ardftsrc_flush(ArdftsrcResampler *r, float *out_samples, size_t max_out_count) {
    if (!r || !out_samples) return 0;

    /* Append silence tail equal to half filter length to flush final frames */
    size_t tail_len = (r->filter_len - 1) / 2;
    float *zero_tail = (float *)calloc(tail_len * r->channels, sizeof(float));
    size_t written = ardftsrc_process(r, zero_tail, tail_len, out_samples, max_out_count);
    free(zero_tail);
    return written;
}

void ardftsrc_destroy(ArdftsrcResampler *r) {
    if (!r) return;
    if (r->hist) {
        for (uint16_t c = 0; c < r->channels; c++) {
            if (r->hist[c]) free(r->hist[c]);
        }
        free(r->hist);
    }
    if (r->polyphase_table) free(r->polyphase_table);
    free(r);
}
