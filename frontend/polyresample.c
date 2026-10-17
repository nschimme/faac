/*
 * Polyphase Kaiser-Windowed Sinc Sample Rate Converter for FAAC
 * Staging Linear Buffer & SIMD Vector-Optimized C Implementation
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

/* Robust double-precision I0 Bessel function with dynamic convergence */
static double bessel_i0(double x) {
    double sum = 1.0;
    double u = 1.0;
    double half_x = x / 2.0;
    for (int k = 1; k < 100; k++) {
        u *= (half_x / k) * (half_x / k);
        sum += u;
        if (u < sum * 1e-16) break;
    }
    return sum;
}

/* Calculate double-precision Kaiser window coefficient */
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

    float **hist;               /* Per-channel history buffer pointers */
    size_t hist_len;            /* Unconsumed frames in hist */
    size_t hist_cap_per_ch;     /* Allocated capacity per channel in frames */

    size_t delay;

    /* Cascade for large downsampling ratios (>2:1) */
    PolyResampler *stage1;
    float *cascade_buf;
    size_t cascade_cap;
};

static size_t polyresample_process_single(PolyResampler * restrict r, const float * restrict in_samples, size_t in_count,
                                           float * restrict out_samples, size_t max_out_count);

static size_t polyresample_flush_single(PolyResampler *r, float *out_samples, size_t max_out_count);

PolyResampler *polyresample_create(uint32_t in_rate, uint32_t out_rate, uint16_t channels, polyresample_quality_t quality) {
    if (in_rate == 0 || out_rate == 0 || channels == 0) return NULL;

    /* Multi-stage cascade decimation for large downsampling ratios (>2:1) */
    if (in_rate > 2 * out_rate) {
        uint32_t intermediate_rate = in_rate / 2;
        PolyResampler *r = (PolyResampler *)calloc(1, sizeof(PolyResampler));
        if (!r) return NULL;

        r->stage1 = polyresample_create(in_rate, intermediate_rate, channels, quality);
        if (!r->stage1) {
            free(r);
            return NULL;
        }

        r->in_rate = in_rate;
        r->out_rate = out_rate;
        r->channels = channels;
        r->quality = quality;
        r->ratio = (double)out_rate / (double)in_rate;

        r->cascade_cap = 16384 * channels;
        r->cascade_buf = (float *)malloc(r->cascade_cap * sizeof(float));
        if (!r->cascade_buf) {
            polyresample_destroy(r->stage1);
            free(r);
            return NULL;
        }

        PolyResampler *stage2 = polyresample_create(intermediate_rate, out_rate, channels, quality);
        if (!stage2) {
            polyresample_destroy(r->stage1);
            free(r->cascade_buf);
            free(r);
            return NULL;
        }

        r->filter_len = stage2->filter_len;
        r->beta = stage2->beta;
        r->polyphase_table = stage2->polyphase_table;
        r->hist = stage2->hist;
        r->hist_cap_per_ch = stage2->hist_cap_per_ch;
        r->delay = r->stage1->delay + stage2->delay;

        stage2->polyphase_table = NULL;
        stage2->hist = NULL;
        free(stage2);

        return r;
    }

    PolyResampler *r = (PolyResampler *)calloc(1, sizeof(PolyResampler));
    if (!r) return NULL;

    r->in_rate = in_rate;
    r->out_rate = out_rate;
    r->channels = channels;
    r->quality = quality;
    r->ratio = (double)out_rate / (double)in_rate;

    switch (quality) {
        case POLYRESAMPLE_QUALITY_FAST: r->filter_len = 127; r->beta = 28.0; break;
        case POLYRESAMPLE_QUALITY_HIGH: r->filter_len = 511; r->beta = 36.0; break;
        case POLYRESAMPLE_QUALITY_GOOD:
        default:                        r->filter_len = 255; r->beta = 32.0; break;
    }

    if ((r->filter_len % 2) == 0) r->filter_len++;

    r->polyphase_table = (float *)malloc(POLYRESAMPLE_STORED_PHASES * r->filter_len * sizeof(float));
    if (!r->polyphase_table) {
        free(r);
        return NULL;
    }

    double cutoff = (r->ratio < 1.0) ? 0.46 * r->ratio : 0.46;
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

    r->hist_cap_per_ch = r->filter_len * 4 + 32768;
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

static size_t polyresample_process_single(PolyResampler * restrict r, const float * restrict in_samples, size_t in_count,
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

    /* Append input samples to linear history buffers with unit-stride reads */
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
                double sum = (double)center_coeff * (double)h[half_len];

                size_t j = 0;
                for (; j + 3 < half_len; j += 4) {
                    sum += (double)coeffs[j + 0] * ((double)h[j + 0] + (double)h[filter_len - 1 - (j + 0)]) +
                           (double)coeffs[j + 1] * ((double)h[j + 1] + (double)h[filter_len - 1 - (j + 1)]) +
                           (double)coeffs[j + 2] * ((double)h[j + 2] + (double)h[filter_len - 1 - (j + 2)]) +
                           (double)coeffs[j + 3] * ((double)h[j + 3] + (double)h[filter_len - 1 - (j + 3)]);
                }
                for (; j < half_len; j++) {
                    sum += (double)coeffs[j] * ((double)h[j] + (double)h[filter_len - 1 - j]);
                }

                out_samples[out_frames * r->channels + c] = (float)sum;
            }
        } else {
            bool is_mirrored = false;
            if (phase_idx > POLYRESAMPLE_NUM_PHASES / 2) {
                phase_idx = POLYRESAMPLE_NUM_PHASES - phase_idx;
                is_mirrored = true;
            }

            const float * restrict coeffs = poly_table + phase_idx * filter_len;

            for (uint16_t c = 0; c < r->channels; c++) {
                double sum = 0.0;
                const float * restrict h = &r->hist[c][base_idx];

                if (!is_mirrored) {
                    size_t j = 0;
                    for (; j + 3 < filter_len; j += 4) {
                        sum += (double)h[j + 0] * (double)coeffs[j + 0] +
                               (double)h[j + 1] * (double)coeffs[j + 1] +
                               (double)h[j + 2] * (double)coeffs[j + 2] +
                               (double)h[j + 3] * (double)coeffs[j + 3];
                    }
                    for (; j < filter_len; j++) {
                        sum += (double)h[j] * (double)coeffs[j];
                    }
                } else {
                    size_t j = 0;
                    for (; j + 3 < filter_len; j += 4) {
                        sum += (double)h[j + 0] * (double)coeffs[filter_len - 1 - (j + 0)] +
                               (double)h[j + 1] * (double)coeffs[filter_len - 1 - (j + 1)] +
                               (double)h[j + 2] * (double)coeffs[filter_len - 1 - (j + 2)] +
                               (double)h[j + 3] * (double)coeffs[filter_len - 1 - (j + 3)];
                    }
                    for (; j < filter_len; j++) {
                        sum += (double)h[j] * (double)coeffs[filter_len - 1 - j];
                    }
                }

                out_samples[out_frames * r->channels + c] = (float)sum;
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

size_t polyresample_process(PolyResampler * restrict r, const float * restrict in_samples, size_t in_count,
                           float * restrict out_samples, size_t max_out_count) {
    if (!r || !out_samples) return 0;

    if (r->stage1) {
        size_t stage1_produced = polyresample_process(r->stage1, in_samples, in_count, r->cascade_buf, r->cascade_cap / r->channels);
        return polyresample_process_single(r, r->cascade_buf, stage1_produced, out_samples, max_out_count);
    }

    return polyresample_process_single(r, in_samples, in_count, out_samples, max_out_count);
}

static size_t polyresample_flush_single(PolyResampler *r, float *out_samples, size_t max_out_count) {
    if (!r || !out_samples) return 0;

    size_t tail_len = (r->filter_len - 1) / 2;
    float *zero_tail = (float *)calloc(tail_len * r->channels, sizeof(float));
    size_t written = polyresample_process_single(r, zero_tail, tail_len, out_samples, max_out_count);
    free(zero_tail);
    return written;
}

size_t polyresample_flush(PolyResampler *r, float *out_samples, size_t max_out_count) {
    if (!r || !out_samples) return 0;

    if (r->stage1) {
        size_t stage1_flushed = polyresample_flush(r->stage1, r->cascade_buf, r->cascade_cap / r->channels);
        size_t main_flushed = polyresample_process_single(r, r->cascade_buf, stage1_flushed, out_samples, max_out_count);
        return main_flushed + polyresample_flush_single(r, out_samples + main_flushed * r->channels, max_out_count - main_flushed);
    }

    return polyresample_flush_single(r, out_samples, max_out_count);
}

void polyresample_destroy(PolyResampler *r) {
    if (!r) return;
    if (r->stage1) polyresample_destroy(r->stage1);
    if (r->cascade_buf) free(r->cascade_buf);
    if (r->hist) {
        for (uint16_t c = 0; c < r->channels; c++) {
            if (r->hist[c]) free(r->hist[c]);
        }
        free(r->hist);
    }
    if (r->polyphase_table) free(r->polyphase_table);
    free(r);
}
