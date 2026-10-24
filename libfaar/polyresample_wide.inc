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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */
/* Mixed builds' High phase bank: double coefficients and accumulators over
 * float sample histories, where float builds use float2. */
#ifdef FAAR_WIDE_HIGH
static void filter_wide_phase(const faar_resampler *r, size_t base,
        size_t phase, double d, bool reverse, float *output) {
    const size_t len = r->filter_len, half_len = (len - 1) / 2;
    const double *c1 = r->polyphase_wide_table + (phase + 1) * len;
#if defined(_MSC_VER)
    __declspec(align(64)) double coeffs[POLYRESAMPLE_MAX_FILTER_LEN];
#else
    alignas(64) double coeffs[POLYRESAMPLE_MAX_FILTER_LEN];
#endif
    if ((len & 1) && phase == 0 && d < exact_phase_eps) {
        for (uint16_t c = 0; c < r->channels; ++c) {
            const float *h = r->hist[c] + base;
            double sum = c1[half_len] * h[half_len];
            for (size_t j = 0; j < half_len; ++j)
                sum += c1[j] * ((double)h[j] + h[len - 1 - j]);
            output[c] = (float)sum;
        }
        return;
    }
    const double *row = c1;
    if (d < exact_phase_eps) {
        if (reverse && r->polyphase_wide_reverse) {
            row = r->polyphase_wide_reverse + (phase + 1) * len;
        } else if (reverse) {
            for (size_t j = 0; j < len; ++j) coeffs[len - 1 - j] = c1[j];
            row = coeffs;
        }
    } else {
        /* Four-point Lagrange weights, as in the ordinary bank. */
        const double *c0 = c1 - len, *c2 = c1 + len, *c3 = c2 + len;
        double d2 = d * d, d3 = d2 * d;
        double w0 = (-d3 + 3 * d2 - 2 * d) / 6;
        double w1 = (d3 - 2 * d2 - d + 2) / 2;
        double w2 = (-d3 + d2 + 2 * d) / 2;
        double w3 = (d3 - d) / 6;
        for (size_t j = 0; j < len; ++j)
            coeffs[reverse ? len - 1 - j : j]
                = w0 * c0[j] + w1 * c1[j] + w2 * c2[j] + w3 * c3[j];
        row = coeffs;
    }
    for (uint16_t c = 0; c < r->channels; ++c) {
        const float *h = r->hist[c] + base;
        double a0 = 0, a1 = 0, a2 = 0, a3 = 0;
        size_t j = 0;
        for (; j + 3 < len; j += 4) {
            a0 += (double)h[j] * row[j];
            a1 += (double)h[j + 1] * row[j + 1];
            a2 += (double)h[j + 2] * row[j + 2];
            a3 += (double)h[j + 3] * row[j + 3];
        }
        double sum = a0 + a1 + a2 + a3;
        for (; j < len; ++j) sum += (double)h[j] * row[j];
        output[c] = (float)sum;
    }
}
#endif
