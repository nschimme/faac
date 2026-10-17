/*
 * Polyphase Kaiser-Windowed Sinc Sample Rate Converter for FAAC
 */

#include "polyresample.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define POLYRESAMPLE_NUM_PHASES 256 /* Fine fractional phase resolution for pre-calculated polyphase bank */
#define POLYRESAMPLE_STORED_PHASES (POLYRESAMPLE_NUM_PHASES / 2 + 1) /* 129 phases stored using linear phase symmetry */

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

struct PolyResampler {
    uint32_t in_rate;
    uint32_t out_rate;
    uint16_t channels;
    polyresample_quality_t quality;
    size_t filter_len;
    double ratio;               /* out_rate / in_rate */
    double phase;               /* Fractional sample position [0.0, 1.0) */
    double beta;
    float *polyphase_table;     /* Symmetric polyphase filter bank table [129 x filter_len] */
    float **hist;               /* Per-channel overlap/history buffer pointers */
    size_t hist_len;            /* Current frames in hist */
    size_t hist_cap_per_ch;     /* Allocated capacity per channel in frames */
    size_t delay;
};

PolyResampler *polyresample_create(uint32_t in_rate, uint32_t out_rate, uint16_t channels, polyresample_quality_t quality) {
    if (in_rate == 0 || out_rate == 0 || channels == 0) return NULL;

    PolyResampler *r = (PolyResampler *)calloc(1, sizeof(PolyResampler));
    if (!r) return NULL;

    r->in_rate = in_rate;
    r->out_rate = out_rate;
    r->channels = channels;
    r->quality = quality;
    r->ratio = (double)out_rate / (double)in_rate;

    switch (quality) {
        case POLYRESAMPLE_QUALITY_FAST: r->filter_len = 127; r->beta = 24.0; break;
        case POLYRESAMPLE_QUALITY_HIGH: r->filter_len = 511; r->beta = 34.0; break;
        case POLYRESAMPLE_QUALITY_GOOD:
        default:                        r->filter_len = 255; r->beta = 30.0; break;
    }

    if ((r->filter_len % 2) == 0) r->filter_len++;

    r->polyphase_table = (float *)malloc(POLYRESAMPLE_STORED_PHASES * r->filter_len * sizeof(float));
    if (!r->polyphase_table) {
        free(r);
        return NULL;
    }

    double cutoff = (r->ratio < 1.0) ? 0.45 * r->ratio : 0.45;
    double half_len = (r->filter_len - 1) / 2.0;

    for (size_t p = 0; p < POLYRESAMPLE_STORED_PHASES; p++) {
        double frac = (double)p / (double)POLYRESAMPLE_NUM_PHASES;
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

        if (sum > 0.0) {
            for (size_t j = 0; j < r->filter_len; j++) {
                phase_coeffs[j] /= (float)sum;
            }
        }
    }

    r->hist_cap_per_ch = r->filter_len * 4 + 16384;
    r->hist = (float **)calloc(channels, sizeof(float *));
    if (!r->hist) {
        free(r->polyphase_table);
        free(r);
        return NULL;
    }

    for (uint16_t c = 0; c < channels; c++) {
        r->hist[c] = (float *)calloc(r->hist_cap_per_ch, sizeof(float));
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

size_t polyresample_get_delay(const PolyResampler *r) {
    return r ? r->delay : 0;
}

size_t polyresample_process(PolyResampler * restrict r, const float * restrict in_samples, size_t in_count,
                           float * restrict out_samples, size_t max_out_count) {
    if (!r || !out_samples) return 0;
    if (r->in_rate == r->out_rate) {
        size_t frames_to_copy = in_count < max_out_count ? in_count : max_out_count;
        if (in_samples && frames_to_copy > 0) {
            memcpy(out_samples, in_samples, frames_to_copy * r->channels * sizeof(float));
        }
        return frames_to_copy;
    }

    const size_t filter_len = r->filter_len;
    const size_t half_len = (filter_len - 1) / 2;
    const float * restrict poly_table = r->polyphase_table;

    /* Dynamic buffer capacity expansion for safe continuous streaming */
    if (in_samples && in_count > 0) {
        size_t needed_cap = r->hist_len + in_count;
        if (needed_cap > r->hist_cap_per_ch) {
            size_t new_cap = r->hist_cap_per_ch ? r->hist_cap_per_ch : 4096;
            while (new_cap < needed_cap) new_cap *= 2;

            for (uint16_t c = 0; c < r->channels; c++) {
                float *new_buf = (float *)realloc(r->hist[c], new_cap * sizeof(float));
                if (!new_buf) return 0;
                r->hist[c] = new_buf;
            }
            r->hist_cap_per_ch = new_cap;
        }

        for (size_t i = 0; i < in_count; i++) {
            for (uint16_t c = 0; c < r->channels; c++) {
                r->hist[c][r->hist_len + i] = in_samples[i * r->channels + c];
            }
        }
        r->hist_len += in_count;
    }

    size_t out_frames = 0;
    const double step = 1.0 / r->ratio;

    while (out_frames < max_out_count) {
        double in_pos = r->phase;
        size_t base_idx = (size_t)floor(in_pos);

        if (base_idx + filter_len > r->hist_len) {
            break;
        }

        double frac = in_pos - (double)base_idx;
        size_t phase_idx = (size_t)(frac * (double)POLYRESAMPLE_NUM_PHASES + 0.5);
        if (phase_idx >= POLYRESAMPLE_NUM_PHASES) phase_idx = POLYRESAMPLE_NUM_PHASES - 1;

        if (phase_idx == 0) {
            const float * restrict coeffs = poly_table;
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
            bool is_mirrored = false;
            if (phase_idx > POLYRESAMPLE_NUM_PHASES / 2) {
                phase_idx = POLYRESAMPLE_NUM_PHASES - phase_idx;
                is_mirrored = true;
            }

            const float * restrict coeffs = poly_table + phase_idx * filter_len;

            for (uint16_t c = 0; c < r->channels; c++) {
                float sum = 0.0f;
                const float * restrict h = &r->hist[c][base_idx];

                if (!is_mirrored) {
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

size_t polyresample_flush(PolyResampler *r, float *out_samples, size_t max_out_count) {
    if (!r || !out_samples) return 0;

    size_t tail_len = (r->filter_len - 1) / 2;
    float *zero_tail = (float *)calloc(tail_len * r->channels, sizeof(float));
    size_t written = polyresample_process(r, zero_tail, tail_len, out_samples, max_out_count);
    free(zero_tail);
    return written;
}

void polyresample_destroy(PolyResampler *r) {
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
