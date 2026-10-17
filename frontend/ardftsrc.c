/*
 * ARDFTSRC - Arbitrary Rate Discrete Fourier Transform Sample Rate Converter
 * Fast C Translation for FAAC
 */

#include "ardftsrc.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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
    float *filter_coeffs;       /* Sinc-Kaiser lowpass FIR coefficients */
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

    r->filter_coeffs = (float *)malloc(r->filter_len * sizeof(float));
    if (!r->filter_coeffs) {
        free(r);
        return NULL;
    }

    /* Calculate cutoff frequency with anti-aliasing guard band */
    double cutoff = (r->ratio < 1.0) ? 0.45 * r->ratio : 0.45;
    double half_len = (r->filter_len - 1) / 2.0;

    double sum = 0.0;
    for (size_t i = 0; i < r->filter_len; i++) {
        double n = (double)i - half_len;
        double sinc = (n == 0.0) ? (2.0 * cutoff) : sin(2.0 * M_PI * cutoff * n) / (M_PI * n);
        double win = kaiser_window((double)i, (double)r->filter_len - 1.0, r->beta);
        double val = sinc * win;
        r->filter_coeffs[i] = (float)val;
        sum += val;
    }

    /* Normalize filter for unity gain at DC */
    if (sum > 0.0) {
        for (size_t i = 0; i < r->filter_len; i++) {
            r->filter_coeffs[i] /= (float)sum;
        }
    }

    r->hist = (float **)malloc(channels * sizeof(float *));
    if (!r->hist) {
        free(r->filter_coeffs);
        free(r);
        return NULL;
    }

    /* Allocate history buffer per channel */
    r->hist_len = 0;
    size_t hist_alloc = r->filter_len * 4 + 4096;
    for (uint16_t c = 0; c < channels; c++) {
        r->hist[c] = (float *)calloc(hist_alloc, sizeof(float));
        if (!r->hist[c]) {
            for (uint16_t k = 0; k < c; k++) free(r->hist[k]);
            free(r->hist);
            free(r->filter_coeffs);
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
    const float * restrict coeffs = r->filter_coeffs;
    const double beta = r->beta;

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

    /* Process resampling output samples using vectorized/restricted FIR loops */
    while (out_frames < max_out_count) {
        double in_pos = r->phase;
        size_t base_idx = (size_t)floor(in_pos);

        if (base_idx + filter_len > r->hist_len) {
            break; /* Need more input samples */
        }

        double frac = in_pos - (double)base_idx;

        for (uint16_t c = 0; c < r->channels; c++) {
            float sum = 0.0f;
            const float * restrict h = &r->hist[c][base_idx];

            if (frac < 1e-6) {
                /* Fast path for aligned samples - unroll 4-way for auto-vectorization */
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
                /* Polyphase fractional delay interpolation */
                for (size_t j = 0; j < filter_len; j++) {
                    double n = (double)j - (double)half_len - frac;
                    double sinc = (fabs(n) < 1e-9) ? 1.0 : sin(M_PI * n) / (M_PI * n);
                    double win = kaiser_window((double)j, (double)filter_len - 1.0, beta);
                    sum += h[j] * (float)(sinc * win);
                }
            }
            out_samples[out_frames * r->channels + c] = sum;
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
    if (r->filter_coeffs) free(r->filter_coeffs);
    free(r);
}
