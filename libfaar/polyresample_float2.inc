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
/* Compensated High phase bank. Sample histories remain ordinary float. */
#ifndef FAAR_WIDE_HIGH
static inline faar_float2 precise_coefficient(const faar_resampler *r, size_t at) {
    return (faar_float2){r->polyphase_table[at], r->polyphase_low_table[at]};
}

static void filter_precise_phase(const faar_resampler *r, size_t base,
        size_t phase, faar_float2 d, bool reverse, float *output) {
    const size_t len = r->filter_len;
#if defined(_MSC_VER)
    __declspec(align(64)) faar_float2 coeffs[POLYRESAMPLE_MAX_FILTER_LEN];
#else
    alignas(64) faar_float2 coeffs[POLYRESAMPLE_MAX_FILTER_LEN];
#endif
    if (ff2_round(d) < exact_phase_eps || (d.hi == 1.0f && d.lo == 0.0f)) {
        size_t row = phase + 1 + (d.hi == 1.0f && d.lo == 0.0f);
        for (size_t j = 0; j < len; ++j)
            coeffs[reverse ? len - 1 - j : j] = precise_coefficient(r, row * len + j);
    } else {
        faar_float2 d2 = ff2_mul(d, d), d3 = ff2_mul(d2, d);
        faar_float2 w0 = ff2_div(ff2_sub(ff2_sub(ff2_mulf(d2, 3), d3),
                                                ff2_mulf(d, 2)), ff2(6));
        faar_float2 w1 = ff2_mulf(ff2_add(ff2_sub(ff2_sub(d3, ff2_mulf(d2, 2)), d), ff2(2)), .5f);
        faar_float2 w2 = ff2_mulf(ff2_add(ff2_sub(d2, d3), ff2_mulf(d, 2)), .5f);
        faar_float2 w3 = ff2_div(ff2_sub(d3, d), ff2(6));
        for (size_t j = 0; j < len; ++j) {
            size_t at = (phase + 1) * len + j;
            faar_float2 v = ff2_add(ff2_mul(w0, precise_coefficient(r, at - len)),
                                    ff2_mul(w1, precise_coefficient(r, at)));
            v = ff2_add(v, ff2_mul(w2, precise_coefficient(r, at + len)));
            v = ff2_add(v, ff2_mul(w3, precise_coefficient(r, at + 2 * len)));
            coeffs[reverse ? len - 1 - j : j] = v;
        }
    }
    for (uint16_t c = 0; c < r->channels; ++c) {
        const float *h = r->hist[c] + base;
        faar_float2 a = ff2(0), b = ff2(0), e = ff2(0), f = ff2(0);
        size_t j = 0;
        for (; j + 3 < len; j += 4) {
            a = ff2_add(a, ff2_mulf(coeffs[j], h[j]));
            b = ff2_add(b, ff2_mulf(coeffs[j + 1], h[j + 1]));
            e = ff2_add(e, ff2_mulf(coeffs[j + 2], h[j + 2]));
            f = ff2_add(f, ff2_mulf(coeffs[j + 3], h[j + 3]));
        }
        faar_float2 sum = ff2_add(ff2_add(a, b), ff2_add(e, f));
        for (; j < len; ++j) sum = ff2_add(sum, ff2_mulf(coeffs[j], h[j]));
        output[c] = ff2_round(sum);
    }
}
#endif
