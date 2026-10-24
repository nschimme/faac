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
/* High's prefilter in mixed builds: double kernels over float sample planes.
 * Included after the ordinary kernels. */
#ifdef FAAR_WIDE_HIGH
static void process_wide_halfband(faar_dft_fir *stage, float *output) {
    size_t n = stage->tap_count, count = stage->block_size / 2;
    double *acc = stage->wide_half_work;
    for (size_t i = 0; i < count; ++i)
        acc[i] = stage->wide_half_coeffs[n / 4] * stage->work[2 * i + n / 2];
    for (size_t j = 0; j < n / 4; ++j) {
        const double coefficient = stage->wide_half_coeffs[j];
        const float *x = stage->work + 2 * j + 1;
        const float *y = stage->work + n - 2 - 2 * j;
        for (size_t i = 0; i < count; ++i)
            acc[i] += coefficient * ((double)x[2 * i] + y[2 * i]);
    }
    for (size_t i = 0; i < count; ++i) output[i] = (float)acc[i];
}
#endif
