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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 */
/* Independently implemented Kaiser-windowed low-pass coefficient designer. */
#include "dft_coeffs.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846264338327950288
#endif

static double bessel_i0(double x) {
    const double half = x * 0.5;
    double sum = 1.0, term = 1.0;
    for (unsigned k = 1; k < 256; ++k) {
        term *= (half / (double)k) * (half / (double)k);
        sum += term;
        if (term <= sum * 2.0e-16) break;
    }
    return sum;
}

int faar_dft_design_lowpass(double *taps, size_t tap_count,
                            double cutoff, double beta) {
    if (!taps || tap_count < 3 || !(tap_count & 1)
            || !isfinite(cutoff) || cutoff <= 0.0 || cutoff >= 0.5
            || !isfinite(beta) || beta < 0.0 || beta > 50.0)
        return -1;

    const double center = (double)(tap_count - 1) * 0.5;
    const double window_denominator = bessel_i0(beta);
    double sum = 0.0;
    for (size_t i = 0; i < tap_count; ++i) {
        const double x = (double)i - center;
        const double arg = 2.0 * cutoff * x;
        const double ideal = x == 0.0 ? 2.0 * cutoff
            : sin(M_PI * arg) / (M_PI * x);
        const double position = x / center;
        const double radicand = 1.0 - position * position;
        const double window = bessel_i0(beta * sqrt(radicand > 0.0
                                                   ? radicand : 0.0))
            / window_denominator;
        taps[i] = ideal * window;
        sum += taps[i];
    }
    if (!isfinite(sum) || fabs(sum) < 1.0e-15) return -1;
    for (size_t i = 0; i < tap_count; ++i) taps[i] /= sum;
    return 0;
}
