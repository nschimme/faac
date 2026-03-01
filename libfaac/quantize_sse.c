#include "quantize.h"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>

#define MAGIC_NUMBER 0.4054f

void quantize_sfb_sse2(int end, int gsize, faac_real sfacfix, const faac_real *xr, int *xi)
{
    int win, cnt;
    const __m128 sfac = _mm_set1_ps((float)sfacfix);
    const __m128 magic = _mm_set1_ps(MAGIC_NUMBER);
    const __m128 abs_mask = _mm_castsi128_ps(_mm_set1_epi32(0x7fffffff));

    for (win = 0; win < gsize; win++)
    {
        for (cnt = 0; cnt <= end - 8; cnt += 8)
        {
#ifdef FAAC_PRECISION_SINGLE
            __m128 x0 = _mm_loadu_ps(xr + cnt);
            __m128 x1 = _mm_loadu_ps(xr + cnt + 4);
#else
            __m128 x0 = _mm_setr_ps((float)xr[cnt], (float)xr[cnt+1], (float)xr[cnt+2], (float)xr[cnt+3]);
            __m128 x1 = _mm_setr_ps((float)xr[cnt+4], (float)xr[cnt+5], (float)xr[cnt+6], (float)xr[cnt+7]);
#endif
            __m128 x0_abs = _mm_and_ps(x0, abs_mask);
            __m128 x1_abs = _mm_and_ps(x1, abs_mask);
            __m128 tmp0 = _mm_mul_ps(x0_abs, sfac);
            __m128 tmp1 = _mm_mul_ps(x1_abs, sfac);
            tmp0 = _mm_mul_ps(tmp0, _mm_sqrt_ps(tmp0));
            tmp1 = _mm_mul_ps(tmp1, _mm_sqrt_ps(tmp1));
            tmp0 = _mm_sqrt_ps(tmp0);
            tmp1 = _mm_sqrt_ps(tmp1);
            tmp0 = _mm_add_ps(tmp0, magic);
            tmp1 = _mm_add_ps(tmp1, magic);

            __m128i q0 = _mm_cvttps_epi32(tmp0);
            __m128i q1 = _mm_cvttps_epi32(tmp1);

            __m128i sign0 = _mm_srai_epi32(_mm_castps_si128(x0), 31);
            __m128i sign1 = _mm_srai_epi32(_mm_castps_si128(x1), 31);
            q0 = _mm_sub_epi32(_mm_xor_si128(q0, sign0), sign0);
            q1 = _mm_sub_epi32(_mm_xor_si128(q1, sign1), sign1);

            _mm_storeu_si128((__m128i*)(xi + cnt), q0);
            _mm_storeu_si128((__m128i*)(xi + cnt + 4), q1);
        }
        if (cnt <= end - 4)
        {
#ifdef FAAC_PRECISION_SINGLE
            __m128 x = _mm_loadu_ps(xr + cnt);
#else
            __m128 x = _mm_setr_ps((float)xr[cnt], (float)xr[cnt+1], (float)xr[cnt+2], (float)xr[cnt+3]);
#endif
            __m128 x_abs = _mm_and_ps(x, abs_mask);
            __m128 tmp = _mm_mul_ps(x_abs, sfac);
            tmp = _mm_mul_ps(tmp, _mm_sqrt_ps(tmp));
            tmp = _mm_sqrt_ps(tmp);
            tmp = _mm_add_ps(tmp, magic);
            __m128i q = _mm_cvttps_epi32(tmp);
            __m128i sign = _mm_srai_epi32(_mm_castps_si128(x), 31);
            q = _mm_sub_epi32(_mm_xor_si128(q, sign), sign);
            _mm_storeu_si128((__m128i*)(xi + cnt), q);
            cnt += 4;
        }
        for (; cnt < end; cnt++)
        {
            faac_real tmp = FAAC_FABS(xr[cnt]);
            tmp *= sfacfix;
            tmp = FAAC_SQRT(tmp * FAAC_SQRT(tmp));
            xi[cnt] = (int)(tmp + (faac_real)MAGIC_NUMBER);
            if (xr[cnt] < 0) xi[cnt] = -xi[cnt];
        }
        xi += end;
        xr += BLOCK_LEN_SHORT;
    }
}
#else
void quantize_sfb_sse2(int end, int gsize, faac_real sfacfix, const faac_real *xr, int *xi) {}
#endif
