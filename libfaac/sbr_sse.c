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

void sbr_qmf_64_modulation_sse2(const faac_real * restrict proto, const faac_real * restrict ovl,
                                faac_real cos_table[128][64], faac_real sin_table[128][64],
                                faac_real * restrict re, faac_real * restrict im)
{
#ifdef FAAC_PRECISION_SINGLE
    int n, k;
    for (n = 0; n < 128; n++) {
        faac_real un = proto[n] * ovl[639 - n] +
                       proto[n + 128] * ovl[511 - n] +
                       proto[n + 256] * ovl[383 - n] +
                       proto[n + 384] * ovl[255 - n] +
                       proto[n + 512] * ovl[127 - n];
        __m128 v_un = _mm_set1_ps(un);
        for (k = 0; k < 64; k += 4) {
            _mm_storeu_ps(&re[k], _mm_add_ps(_mm_loadu_ps(&re[k]), _mm_mul_ps(v_un, _mm_loadu_ps(&cos_table[n][k]))));
            _mm_storeu_ps(&im[k], _mm_add_ps(_mm_loadu_ps(&im[k]), _mm_mul_ps(v_un, _mm_loadu_ps(&sin_table[n][k]))));
        }
    }
#else
    float re_f[64], im_f[64];
    int n, k;
    memset(re_f, 0, 64 * sizeof(float));
    memset(im_f, 0, 64 * sizeof(float));

    for (n = 0; n < 128; n++) {
        double un_d = proto[n] * ovl[639 - n] +
                      proto[n + 128] * ovl[511 - n] +
                      proto[n + 256] * ovl[383 - n] +
                      proto[n + 384] * ovl[255 - n] +
                      proto[n + 512] * ovl[127 - n];
        __m128 v_un = _mm_set1_ps((float)un_d);

        for (k = 0; k < 64; k += 4) {
            __m128 cos_v = _mm_movelh_ps(_mm_cvtpd_ps(_mm_loadu_pd(&cos_table[n][k])),
                                         _mm_cvtpd_ps(_mm_loadu_pd(&cos_table[n][k + 2])));
            __m128 sin_v = _mm_movelh_ps(_mm_cvtpd_ps(_mm_loadu_pd(&sin_table[n][k])),
                                         _mm_cvtpd_ps(_mm_loadu_pd(&sin_table[n][k + 2])));

            _mm_storeu_ps(&re_f[k], _mm_add_ps(_mm_loadu_ps(&re_f[k]), _mm_mul_ps(v_un, cos_v)));
            _mm_storeu_ps(&im_f[k], _mm_add_ps(_mm_loadu_ps(&im_f[k]), _mm_mul_ps(v_un, sin_v)));
        }
    }

    for (k = 0; k < 64; k++) {
        re[k] = (double)re_f[k];
        im[k] = (double)im_f[k];
    }
#endif
}
