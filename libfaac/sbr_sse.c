/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2001 Menno Bakker
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <immintrin.h>
#include <string.h>
#include "faac_real.h"
#include "sbr.h"

/**
 * SSE2 optimized 64-band modulation kernel.
 * Uses pre-converted float modulation tables for maximum speed in all builds.
 */
void sbr_qmf_64_modulation_sse2(const SBRInfo *sbr, const faac_real * restrict proto,
                                const faac_real * restrict ovl, faac_real * restrict re,
                                faac_real * restrict im)
{
    float re_f[64], im_f[64];
    int n, k;
    memset(re_f, 0, 64 * sizeof(float));
    memset(im_f, 0, 64 * sizeof(float));

    for (n = 0; n < 128; n++) {
        /* proto and ovl may be double, but we only need float precision for SBR. */
        float un = (float)(proto[n] * ovl[639 - n] +
                          proto[n + 128] * ovl[511 - n] +
                          proto[n + 256] * ovl[383 - n] +
                          proto[n + 384] * ovl[255 - n] +
                          proto[n + 512] * ovl[127 - n]);
        __m128 v_un = _mm_set1_ps(un);

        const float *cos_row = sbr->cos_table64F[n];
        const float *sin_row = sbr->sin_table64F[n];

        for (k = 0; k < 64; k += 4) {
            _mm_storeu_ps(&re_f[k], _mm_add_ps(_mm_loadu_ps(&re_f[k]), _mm_mul_ps(v_un, _mm_loadu_ps(&cos_row[k]))));
            _mm_storeu_ps(&im_f[k], _mm_add_ps(_mm_loadu_ps(&im_f[k]), _mm_mul_ps(v_un, _mm_loadu_ps(&sin_row[k]))));
        }
    }

    /* Convert back to faac_real (might be double). */
    for (k = 0; k < 64; k++) {
        re[k] = (faac_real)re_f[k];
        im[k] = (faac_real)im_f[k];
    }
}
