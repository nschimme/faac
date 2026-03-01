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

#define MAGIC_NUMBER 0.4054

void quantize_sse2(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix)
{
    const __m128 sfac = _mm_set1_ps((float)sfacfix);
    const __m128 magic = _mm_set1_ps(MAGIC_NUMBER);
    const __m128 zero = _mm_setzero_ps();
    // 0x7FFFFFFF mask to clear the sign bit (extract absolute value)
    const __m128 abs_mask = _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF));
    int cnt = 0;

    // 8-wide loop to overlap high-latency sqrtps instructions
    if (n >= 8)
    {
        int n8 = n & ~7;
        for (; cnt < n8; cnt += 8)
        {
            __m128 x0, x1;
#ifdef FAAC_PRECISION_DOUBLE
            // Load doubles and convert to floats (original quirk)
            x0 = _mm_movelh_ps(_mm_cvtpd_ps(_mm_loadu_pd(xr + cnt)),
                               _mm_cvtpd_ps(_mm_loadu_pd(xr + cnt + 2)));
            x1 = _mm_movelh_ps(_mm_cvtpd_ps(_mm_loadu_pd(xr + cnt + 4)),
                               _mm_cvtpd_ps(_mm_loadu_pd(xr + cnt + 6)));
#else
            // Load unaligned floats
            x0 = _mm_loadu_ps(xr + cnt);
            x1 = _mm_loadu_ps(xr + cnt + 4);
#endif
            // Extract sign mask: 0xFFFFFFFF if negative, 0x00000000 if positive
            __m128i s0 = _mm_castps_si128(_mm_cmplt_ps(x0, zero));
            __m128i s1 = _mm_castps_si128(_mm_cmplt_ps(x1, zero));

            // abs(x) = x & 0x7FFFFFFF
            __m128 a0 = _mm_and_ps(x0, abs_mask);
            __m128 a1 = _mm_and_ps(x1, abs_mask);

            // tmp = abs(x) * sfacfix
            __m128 t0 = _mm_mul_ps(a0, sfac);
            __m128 t1 = _mm_mul_ps(a1, sfac);

            // tmp = tmp * sqrt(tmp) (results in tmp^1.5)
            __m128 r0 = _mm_sqrt_ps(t0);
            __m128 r1 = _mm_sqrt_ps(t1);
            t0 = _mm_mul_ps(t0, r0);
            t1 = _mm_mul_ps(t1, r1);

            // result = sqrt(tmp^1.5) = tmp^0.75
            t0 = _mm_sqrt_ps(t0);
            t1 = _mm_sqrt_ps(t1);

            // q = (int)(result + MAGIC_NUMBER)
            __m128i i0 = _mm_cvttps_epi32(_mm_add_ps(t0, magic));
            __m128i i1 = _mm_cvttps_epi32(_mm_add_ps(t1, magic));

            // Branchless sign application:
            // if (negative) i = -i  =>  i = (i ^ mask) - mask
            // (i ^ 0xFFFFFFFF) = ~i; ~i - 0xFFFFFFFF = ~i + 1 = two's complement negation
            i0 = _mm_sub_epi32(_mm_xor_si128(i0, s0), s0);
            i1 = _mm_sub_epi32(_mm_xor_si128(i1, s1), s1);

            // Store results to potentially unaligned memory
            _mm_storeu_si128((__m128i*)(xi + cnt), i0);
            _mm_storeu_si128((__m128i*)(xi + cnt + 4), i1);
        }
    }

    // 4-wide loop for remaining multiple of 4
    if (cnt <= n - 4)
    {
        __m128 x;
#ifdef FAAC_PRECISION_DOUBLE
        x = _mm_movelh_ps(_mm_cvtpd_ps(_mm_loadu_pd(xr + cnt)),
                           _mm_cvtpd_ps(_mm_loadu_pd(xr + cnt + 2)));
#else
        x = _mm_loadu_ps(xr + cnt);
#endif
        __m128i s = _mm_castps_si128(_mm_cmplt_ps(x, zero));
        __m128 a = _mm_and_ps(x, abs_mask);
        __m128 t = _mm_mul_ps(a, sfac);
        t = _mm_mul_ps(t, _mm_sqrt_ps(t));
        t = _mm_sqrt_ps(t);
        __m128i i = _mm_cvttps_epi32(_mm_add_ps(t, magic));
        i = _mm_sub_epi32(_mm_xor_si128(i, s), s);
        _mm_storeu_si128((__m128i*)(xi + cnt), i);
        cnt += 4;
    }

    // Safe scalar remainder loop for widths not multiple of 4
    for (; cnt < n; cnt++)
    {
        float val = (float)xr[cnt];
        float tmp = (val < 0.0f) ? -val : val;
        tmp *= (float)sfacfix;
        tmp = sqrtf(tmp * sqrtf(tmp));
        int q = (int)(tmp + MAGIC_NUMBER);
        xi[cnt] = (val < 0.0f) ? -q : q;
    }
}
