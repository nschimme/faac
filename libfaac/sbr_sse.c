/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Nils Schimmelmann
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <immintrin.h>
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
    int n, k;
    for (n = 0; n < 128; n++) {
        faac_real un = proto[n] * ovl[639 - n] +
                       proto[n + 128] * ovl[511 - n] +
                       proto[n + 256] * ovl[383 - n] +
                       proto[n + 384] * ovl[255 - n] +
                       proto[n + 512] * ovl[127 - n];
        for (k = 0; k < 64; k++) {
            re[k] += un * cos_table[n][k];
            im[k] += un * sin_table[n][k];
        }
    }
#endif
}
