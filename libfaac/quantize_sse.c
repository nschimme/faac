#include <immintrin.h>
#include <math.h>
#include "coder.h"

#define MAGIC_NUMBER 0.4054

void quantize_sse2(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix)
{
    const __m128 sfac = _mm_set1_ps((float)sfacfix);
    const __m128 magic = _mm_set1_ps(MAGIC_NUMBER);
    const __m128 zero = _mm_setzero_ps();
    const __m128 abs_mask = _mm_set1_ps(-0.0f); // Sign bit set
    int cnt = 0;

    // 8-wide loop to overlap sqrt latency
    if (n >= 8)
    {
        int n8 = n & ~7;
        for (; cnt < n8; cnt += 8)
        {
            __m128 x0, x1;
#ifdef FAAC_PRECISION_DOUBLE
            x0 = _mm_movelh_ps(_mm_cvtpd_ps(_mm_loadu_pd(xr + cnt)),
                               _mm_cvtpd_ps(_mm_loadu_pd(xr + cnt + 2)));
            x1 = _mm_movelh_ps(_mm_cvtpd_ps(_mm_loadu_pd(xr + cnt + 4)),
                               _mm_cvtpd_ps(_mm_loadu_pd(xr + cnt + 6)));
#else
            x0 = _mm_loadu_ps(xr + cnt);
            x1 = _mm_loadu_ps(xr + cnt + 4);
#endif
            __m128i s0 = _mm_castps_si128(_mm_cmplt_ps(x0, zero));
            __m128i s1 = _mm_castps_si128(_mm_cmplt_ps(x1, zero));

            __m128 a0 = _mm_andnot_ps(abs_mask, x0);
            __m128 a1 = _mm_andnot_ps(abs_mask, x1);

            __m128 t0 = _mm_mul_ps(a0, sfac);
            __m128 t1 = _mm_mul_ps(a1, sfac);

            __m128 r0 = _mm_sqrt_ps(t0);
            __m128 r1 = _mm_sqrt_ps(t1);

            t0 = _mm_mul_ps(t0, r0);
            t1 = _mm_mul_ps(t1, r1);

            t0 = _mm_sqrt_ps(t0);
            t1 = _mm_sqrt_ps(t1);

            __m128i i0 = _mm_cvttps_epi32(_mm_add_ps(t0, magic));
            __m128i i1 = _mm_cvttps_epi32(_mm_add_ps(t1, magic));

            i0 = _mm_sub_epi32(_mm_xor_si128(i0, s0), s0);
            i1 = _mm_sub_epi32(_mm_xor_si128(i1, s1), s1);

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
        __m128 a = _mm_andnot_ps(abs_mask, x);
        __m128 t = _mm_mul_ps(a, sfac);
        t = _mm_mul_ps(t, _mm_sqrt_ps(t));
        t = _mm_sqrt_ps(t);
        __m128i i = _mm_cvttps_epi32(_mm_add_ps(t, magic));
        i = _mm_sub_epi32(_mm_xor_si128(i, s), s);
        _mm_storeu_si128((__m128i*)(xi + cnt), i);
        cnt += 4;
    }

    // Scalar remainder
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
