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

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <immintrin.h>
#include <math.h>
#include "coder.h"

#define MAGIC_NUMBER_REAL ((faac_real)0.4054)

void quantize_avx2(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix)
{
    int cnt = 0;
#ifdef FAAC_PRECISION_DOUBLE
    const __m256d sfac = _mm256_set1_pd(sfacfix);
    const __m256d magic = _mm256_set1_pd(MAGIC_NUMBER_REAL);
    const __m256d sign_mask = _mm256_set1_pd(-0.0);
    const __m256i abs_mask_i = _mm256_set1_epi64x(0x7FFFFFFFFFFFFFFFLL);
    const __m256d abs_mask = _mm256_castsi256_pd(abs_mask_i);

    if (n >= 16)
    {
        for (; cnt <= n - 16; cnt += 16)
        {
            __m256d x0 = _mm256_loadu_pd(xr + cnt);
            __m256d x1 = _mm256_loadu_pd(xr + cnt + 4);
            __m256d x2 = _mm256_loadu_pd(xr + cnt + 8);
            __m256d x3 = _mm256_loadu_pd(xr + cnt + 12);

            __m256d t0 = _mm256_and_pd(x0, abs_mask);
            __m256d t1 = _mm256_and_pd(x1, abs_mask);
            __m256d t2 = _mm256_and_pd(x2, abs_mask);
            __m256d t3 = _mm256_and_pd(x3, abs_mask);

            t0 = _mm256_mul_pd(t0, sfac);
            t1 = _mm256_mul_pd(t1, sfac);
            t2 = _mm256_mul_pd(t2, sfac);
            t3 = _mm256_mul_pd(t3, sfac);

            t0 = _mm256_sqrt_pd(_mm256_mul_pd(t0, _mm256_sqrt_pd(t0)));
            t1 = _mm256_sqrt_pd(_mm256_mul_pd(t1, _mm256_sqrt_pd(t1)));
            t2 = _mm256_sqrt_pd(_mm256_mul_pd(t2, _mm256_sqrt_pd(t2)));
            t3 = _mm256_sqrt_pd(_mm256_mul_pd(t3, _mm256_sqrt_pd(t3)));

            _mm_storeu_si128((__m128i*)(xi + cnt),      _mm256_cvttpd_epi32(_mm256_blendv_pd(_mm256_add_pd(t0, magic), _mm256_xor_pd(_mm256_add_pd(t0, magic), sign_mask), x0)));
            _mm_storeu_si128((__m128i*)(xi + cnt + 4),  _mm256_cvttpd_epi32(_mm256_blendv_pd(_mm256_add_pd(t1, magic), _mm256_xor_pd(_mm256_add_pd(t1, magic), sign_mask), x1)));
            _mm_storeu_si128((__m128i*)(xi + cnt + 8),  _mm256_cvttpd_epi32(_mm256_blendv_pd(_mm256_add_pd(t2, magic), _mm256_xor_pd(_mm256_add_pd(t2, magic), sign_mask), x2)));
            _mm_storeu_si128((__m128i*)(xi + cnt + 12), _mm256_cvttpd_epi32(_mm256_blendv_pd(_mm256_add_pd(t3, magic), _mm256_xor_pd(_mm256_add_pd(t3, magic), sign_mask), x3)));
        }
    }
#else
    const __m256 sfac = _mm256_set1_ps(sfacfix);
    const __m256 magic = _mm256_set1_ps((float)MAGIC_NUMBER_REAL);
    const __m256 sign_mask = _mm256_set1_ps(-0.0f);
    const __m256 abs_mask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7FFFFFFF));

    if (n >= 64)
    {
        for (; cnt <= n - 64; cnt += 64)
        {
            __m256 x0 = _mm256_loadu_ps(xr + cnt);
            __m256 x1 = _mm256_loadu_ps(xr + cnt + 8);
            __m256 x2 = _mm256_loadu_ps(xr + cnt + 16);
            __m256 x3 = _mm256_loadu_ps(xr + cnt + 24);
            __m256 x4 = _mm256_loadu_ps(xr + cnt + 32);
            __m256 x5 = _mm256_loadu_ps(xr + cnt + 40);
            __m256 x6 = _mm256_loadu_ps(xr + cnt + 48);
            __m256 x7 = _mm256_loadu_ps(xr + cnt + 56);

            __m256 t0 = _mm256_mul_ps(_mm256_and_ps(x0, abs_mask), sfac);
            __m256 t1 = _mm256_mul_ps(_mm256_and_ps(x1, abs_mask), sfac);
            __m256 t2 = _mm256_mul_ps(_mm256_and_ps(x2, abs_mask), sfac);
            __m256 t3 = _mm256_mul_ps(_mm256_and_ps(x3, abs_mask), sfac);
            __m256 t4 = _mm256_mul_ps(_mm256_and_ps(x4, abs_mask), sfac);
            __m256 t5 = _mm256_mul_ps(_mm256_and_ps(x5, abs_mask), sfac);
            __m256 t6 = _mm256_mul_ps(_mm256_and_ps(x6, abs_mask), sfac);
            __m256 t7 = _mm256_mul_ps(_mm256_and_ps(x7, abs_mask), sfac);

            t0 = _mm256_sqrt_ps(_mm256_mul_ps(t0, _mm256_sqrt_ps(t0)));
            t1 = _mm256_sqrt_ps(_mm256_mul_ps(t1, _mm256_sqrt_ps(t1)));
            t2 = _mm256_sqrt_ps(_mm256_mul_ps(t2, _mm256_sqrt_ps(t2)));
            t3 = _mm256_sqrt_ps(_mm256_mul_ps(t3, _mm256_sqrt_ps(t3)));
            t4 = _mm256_sqrt_ps(_mm256_mul_ps(t4, _mm256_sqrt_ps(t4)));
            t5 = _mm256_sqrt_ps(_mm256_mul_ps(t5, _mm256_sqrt_ps(t5)));
            t6 = _mm256_sqrt_ps(_mm256_mul_ps(t6, _mm256_sqrt_ps(t6)));
            t7 = _mm256_sqrt_ps(_mm256_mul_ps(t7, _mm256_sqrt_ps(t7)));

            _mm256_storeu_si256((__m256i*)(xi + cnt),      _mm256_cvttps_epi32(_mm256_blendv_ps(_mm256_add_ps(t0, magic), _mm256_xor_ps(_mm256_add_ps(t0, magic), sign_mask), x0)));
            _mm256_storeu_si256((__m256i*)(xi + cnt + 8),  _mm256_cvttps_epi32(_mm256_blendv_ps(_mm256_add_ps(t1, magic), _mm256_xor_ps(_mm256_add_ps(t1, magic), sign_mask), x1)));
            _mm256_storeu_si256((__m256i*)(xi + cnt + 16), _mm256_cvttps_epi32(_mm256_blendv_ps(_mm256_add_ps(t2, magic), _mm256_xor_ps(_mm256_add_ps(t2, magic), sign_mask), x2)));
            _mm256_storeu_si256((__m256i*)(xi + cnt + 24), _mm256_cvttps_epi32(_mm256_blendv_ps(_mm256_add_ps(t3, magic), _mm256_xor_ps(_mm256_add_ps(t3, magic), sign_mask), x3)));
            _mm256_storeu_si256((__m256i*)(xi + cnt + 32), _mm256_cvttps_epi32(_mm256_blendv_ps(_mm256_add_ps(t4, magic), _mm256_xor_ps(_mm256_add_ps(t4, magic), sign_mask), x4)));
            _mm256_storeu_si256((__m256i*)(xi + cnt + 40), _mm256_cvttps_epi32(_mm256_blendv_ps(_mm256_add_ps(t5, magic), _mm256_xor_ps(_mm256_add_ps(t5, magic), sign_mask), x5)));
            _mm256_storeu_si256((__m256i*)(xi + cnt + 48), _mm256_cvttps_epi32(_mm256_blendv_ps(_mm256_add_ps(t6, magic), _mm256_xor_ps(_mm256_add_ps(t6, magic), sign_mask), x6)));
            _mm256_storeu_si256((__m256i*)(xi + cnt + 56), _mm256_cvttps_epi32(_mm256_blendv_ps(_mm256_add_ps(t7, magic), _mm256_xor_ps(_mm256_add_ps(t7, magic), sign_mask), x7)));
        }
    }
#endif

    for (; cnt < n; cnt++)
    {
        faac_real val = xr[cnt];
        faac_real tmp = FAAC_FABS(val);
        tmp *= sfacfix;
        tmp = FAAC_SQRT(tmp * FAAC_SQRT(tmp));
        int q = (int)(tmp + MAGIC_NUMBER_REAL);
        xi[cnt] = (val < 0) ? -q : q;
    }
    _mm256_zeroupper();
}
