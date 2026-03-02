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
#include "faac_real.h"

#define MAGIC_NUMBER 0.4054

void quantize_sse2(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix)
{
    const __m128 zero = _mm_setzero_ps();
    const __m128 sfac = _mm_set1_ps(sfacfix);
    const __m128 magic = _mm_set1_ps(MAGIC_NUMBER);
    int cnt;

    for (cnt = 0; cnt < n; cnt += 4)
    {
        __m128 x = {xr[cnt], xr[cnt + 1], xr[cnt + 2], xr[cnt + 3]};

        x = _mm_max_ps(x, _mm_sub_ps(zero, x));
        x = _mm_mul_ps(x, sfac);
        x = _mm_mul_ps(x, _mm_sqrt_ps(x));
        x = _mm_sqrt_ps(x);
        x = _mm_add_ps(x, magic);

        _mm_storeu_si128((__m128i*)(xi + cnt), _mm_cvttps_epi32(x));
    }

    for (cnt = 0; cnt < n; cnt++)
    {
        if (xr[cnt] < 0)
            xi[cnt] = -xi[cnt];
    }
}
