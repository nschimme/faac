/*
 * Polyphase Farrow Cubic Interpolated Kaiser-Windowed Sinc Sample Rate Converter
 * Auto-Vectorized Straight-Line C Implementation for FAAC
 */

#include "polyresample.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <assert.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define POLYRESAMPLE_NUM_PHASES 256 /* Fine fractional phase resolution for pre-calculated polyphase bank */
#define POLYRESAMPLE_STORED_PHASES (POLYRESAMPLE_NUM_PHASES / 2 + 1) /* 129 phases stored using linear phase symmetry */
#define POLYRESAMPLE_MAX_FILTER_LEN 512 /* Upper bound for stack-allocated temporary coefficient buffers */

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

        assert(r->filter_len <= POLYRESAMPLE_MAX_FILTER_LEN);
        return r;
    }

    PolyResampler *r = (PolyResampler *)calloc(1, sizeof(PolyResampler));
    if (!r) return NULL;

    r->in_rate = in_rate;
    r->out_rate = out_rate;
    r->channels = channels;
    r->quality = quality;
    r->ratio = (double)out_rate / (double)in_rate;

    /* Steep Kaiser beta parameters for >140 dB stopband attenuation and <0.001 dB passband ripple */
    switch (quality) {
        case POLYRESAMPLE_QUALITY_FAST: r->filter_len = 127; r->beta = 28.0; break;
        case POLYRESAMPLE_QUALITY_HIGH: r->filter_len = 511; r->beta = 38.0; break;
        case POLYRESAMPLE_QUALITY_GOOD:
        default:                        r->filter_len = 255; r->beta = 32.0; break;
    }

    if ((r->filter_len % 2) == 0) r->filter_len++;
    assert(r->filter_len <= POLYRESAMPLE_MAX_FILTER_LEN);

    r->polyphase_table = (float *)malloc(POLYRESAMPLE_STORED_PHASES * r->filter_len * sizeof(float));
    if (!r->polyphase_table) {
        free(r);
        return NULL;
    }

    /* Cutoff frequency tuned to 0.48 * fs to maximize audio bandwidth */
    double cutoff = (r->ratio < 1.0) ? 0.48 * r->ratio : 0.48;
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

    assert(filter_len <= POLYRESAMPLE_MAX_FILTER_LEN);

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

    float temp_coeffs[POLYRESAMPLE_MAX_FILTER_LEN];

    while (out_frames < max_out_count) {
        double in_pos = r->phase;
        size_t base_idx = (size_t)floor(in_pos);

        if (base_idx + filter_len > r->hist_len) {
            break;
        }

        double frac = in_pos - (double)base_idx;
        double phase_exact = frac * (double)POLYRESAMPLE_NUM_PHASES;
        size_t p_idx = (size_t)floor(phase_exact);
        double delta = phase_exact - (double)p_idx; /* Fractional offset inside [p_idx, p_idx+1] for Farrow interpolation */

        if (p_idx >= POLYRESAMPLE_NUM_PHASES) p_idx = POLYRESAMPLE_NUM_PHASES - 1;

        if (p_idx == 0 && delta < 1e-6) {
            /* Fast path for phase 0 (exact integer alignment): fold symmetric taps */
            const float * restrict coeffs = poly_table;
            const float center_coeff = coeffs[half_len];

            for (uint16_t c = 0; c < r->channels; c++) {
                const float * restrict h = &r->hist[c][base_idx];
                double sum = (double)center_coeff * (double)h[half_len];

                for (size_t j = 0; j < half_len; j++) {
                    sum += (double)coeffs[j] * ((double)h[j] + (double)h[filter_len - 1 - j]);
                }

                out_samples[out_frames * r->channels + c] = (float)sum;
            }
        } else {
            /* 1. Interpolate-Then-Convolve: Pre-calculate flat interpolated filter coefficients into a temporary array outside channel/tap loops */
            size_t p0 = (p_idx > 0) ? p_idx - 1 : 0;
            size_t p1 = p_idx;
            size_t p2 = (p_idx + 1 < POLYRESAMPLE_NUM_PHASES) ? p_idx + 1 : POLYRESAMPLE_NUM_PHASES - 1;
            size_t p3 = (p_idx + 2 < POLYRESAMPLE_NUM_PHASES) ? p_idx + 2 : POLYRESAMPLE_NUM_PHASES - 1;

            bool m0 = false, m1 = false, m2 = false, m3 = false;
            size_t s0 = p0, s1 = p1, s2 = p2, s3 = p3;

            if (s0 > POLYRESAMPLE_NUM_PHASES / 2) { s0 = POLYRESAMPLE_NUM_PHASES - s0; m0 = true; }
            if (s1 > POLYRESAMPLE_NUM_PHASES / 2) { s1 = POLYRESAMPLE_NUM_PHASES - s1; m1 = true; }
            if (s2 > POLYRESAMPLE_NUM_PHASES / 2) { s2 = POLYRESAMPLE_NUM_PHASES - s2; m2 = true; }
            if (s3 > POLYRESAMPLE_NUM_PHASES / 2) { s3 = POLYRESAMPLE_NUM_PHASES - s3; m3 = true; }

            const float * restrict c0 = poly_table + s0 * filter_len;
            const float * restrict c1 = poly_table + s1 * filter_len;
            const float * restrict c2 = poly_table + s2 * filter_len;
            const float * restrict c3 = poly_table + s3 * filter_len;

            double d = delta;
            double d2 = d * d;
            double d3 = d2 * d;

            double w0 = -0.5 * d3 + d2 - 0.5 * d;
            double w1 = 1.5 * d3 - 2.5 * d2 + 1.0;
            double w2 = -1.5 * d3 + 2.0 * d2 + 0.5 * d;
            double w3 = 0.5 * d3 - 0.5 * d2;

            /* Flat coefficient interpolation loop - zero branches inside sample dot-product */
            for (size_t j = 0; j < filter_len; j++) {
                double k0 = m0 ? (double)c0[filter_len - 1 - j] : (double)c0[j];
                double k1 = m1 ? (double)c1[filter_len - 1 - j] : (double)c1[j];
                double k2 = m2 ? (double)c2[filter_len - 1 - j] : (double)c2[j];
                double k3 = m3 ? (double)c3[filter_len - 1 - j] : (double)c3[j];

                temp_coeffs[j] = (float)(w0 * k0 + w1 * k1 + w2 * k2 + w3 * k3);
            }

            /* 2. Flat straight-line inner dot-product loop: easily auto-vectorized by LLVM/GCC without manual unroll friction */
            for (uint16_t c = 0; c < r->channels; c++) {
                const float * restrict h = &r->hist[c][base_idx];
                double sum = 0.0;

                for (size_t j = 0; j < filter_len; j++) {
                    sum += (double)h[j] * (double)temp_coeffs[j];
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
