/*
 * FAAC - Freeware Advanced Audio Coder
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

/* The SSE2 quantizer is a drop-in for the scalar one: for every input the library can
 * dispatch it on, both must return the same indices and the same maximum. The inputs sit
 * on and either side of each rounding boundary, where a different operation order shows.
 * Exact equality needs SSE-precision arithmetic in the scalar path, so builds that
 * evaluate floats wider (x87) skip. */
#include <float.h>
#include <stdio.h>
#include "quantize.c"
#include "../../tests/ftest.h"

#if defined(HAVE_SSE2) && FLT_EVAL_METHOD == 0

enum { N4 = 64, N = 4 * N4 };

static void compare(const float *xr, float gain)
{
    int a[N], b[N];
    int ma = quantize_scalar(xr, a, N4, gain);
    int mb = quantize_sse2(xr, b, N4, gain);
    if (ma != mb) fprintf(stderr, "gain %.9g: ", gain);
    FT_CHECK_INT(mb, ma);
    for (int i = 0; i < N; i++) {
        if (a[i] != b[i]) fprintf(stderr, "gain %.9g, x %.9g: ", gain, xr[i]);
        FT_CHECK_INT(b[i], a[i]);
    }
}

int main(void)
{
    uint32_t rng = 0x1234567u;
    float xr[N];

    if (!(get_cpu_caps() & CPU_CAP_SSE2)) return 77;

    /* sfac steps are quarter-dB: gain = 2^(sfac/4) over the scalefactor range. */
    for (int sfac = -64; sfac <= 96; sfac += 8) {
        float gain = powf(2.0f, (float)sfac / 4.0f);
        int fill = 0;
        /* Every rounding boundary q = (x*gain)^(3/4) + MAGIC, and its float neighbours. */
        for (int q = 1; q <= MAX_HUFF_ESC_VAL; q++) {
            float edge = powf((float)q - MAGIC_NUMBER, 4.0f / 3.0f) / gain;
            float v[3] = { nextafterf(edge, 0.0f), edge, nextafterf(edge, INFINITY) };
            for (int k = 0; k < 3; k++) {
                xr[fill++] = (q & 1) ? v[k] : -v[k];
                if (fill == N) { compare(xr, gain); fill = 0; }
            }
        }
        if (fill) { for (; fill < N; fill++) xr[fill] = 0.0f; compare(xr, gain); }
        /* Spectrum-like values: small, signed, zeros and a few large. */
        for (int rep = 0; rep < 50; rep++) {
            for (int i = 0; i < N; i++) {
                uint32_t r = ft_rand(&rng);
                float mag = (float)(r & 0xFFFF) / 65536.0f * ((r >> 16) % 7 == 0 ? 20000.0f : 100.0f) / gain;
                xr[i] = (r >> 30) == 0 ? 0.0f : ((r >> 29) & 1 ? -mag : mag);
            }
            compare(xr, gain);
        }
    }
    return ft_done();
}

#else

int main(void)
{
    fprintf(stderr, "scalar/SSE2 equality needs SSE2 and FLT_EVAL_METHOD == 0\n");
    return 77;
}

#endif
