#include "quantize.h"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>

#define MAGIC_NUMBER 0.4054f

void quantize_sfb_avx2(int end, int gsize, faac_real sfacfix, const faac_real *xr, int *xi)
{
    int win, cnt;
    const __m256 sfac = _mm256_set1_ps((float)sfacfix);
    const __m256 magic = _mm256_set1_ps(MAGIC_NUMBER);
    const __m256 abs_mask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7fffffff));

    for (win = 0; win < gsize; win++)
    {
        for (cnt = 0; cnt <= end - 16; cnt += 16)
        {
#ifdef FAAC_PRECISION_SINGLE
            __m256 x0 = _mm256_loadu_ps(xr + cnt);
            __m256 x1 = _mm256_loadu_ps(xr + cnt + 8);
#else
            __m256 x0 = _mm256_setr_ps((float)xr[cnt], (float)xr[cnt+1], (float)xr[cnt+2], (float)xr[cnt+3],
                                       (float)xr[cnt+4], (float)xr[cnt+5], (float)xr[cnt+6], (float)xr[cnt+7]);
            __m256 x1 = _mm256_setr_ps((float)xr[cnt+8], (float)xr[cnt+9], (float)xr[cnt+10], (float)xr[cnt+11],
                                       (float)xr[cnt+12], (float)xr[cnt+13], (float)xr[cnt+14], (float)xr[cnt+15]);
#endif
            __m256 x0_abs = _mm256_and_ps(x0, abs_mask);
            __m256 x1_abs = _mm256_and_ps(x1, abs_mask);
            __m256 tmp0 = _mm256_mul_ps(x0_abs, sfac);
            __m256 tmp1 = _mm256_mul_ps(x1_abs, sfac);
            tmp0 = _mm256_mul_ps(tmp0, _mm256_sqrt_ps(tmp0));
            tmp1 = _mm256_mul_ps(tmp1, _mm256_sqrt_ps(tmp1));
            tmp0 = _mm256_sqrt_ps(tmp0);
            tmp1 = _mm256_sqrt_ps(tmp1);
            tmp0 = _mm256_add_ps(tmp0, magic);
            tmp1 = _mm256_add_ps(tmp1, magic);

            __m256i q0 = _mm256_cvttps_epi32(tmp0);
            __m256i q1 = _mm256_cvttps_epi32(tmp1);

            __m256i sign0 = _mm256_srai_epi32(_mm256_castps_si256(x0), 31);
            __m256i sign1 = _mm256_srai_epi32(_mm256_castps_si256(x1), 31);
            q0 = _mm256_sub_epi32(_mm256_xor_si256(q0, sign0), sign0);
            q1 = _mm256_sub_epi32(_mm256_xor_si256(q1, sign1), sign1);

            _mm256_storeu_si256((__m256i*)(xi + cnt), q0);
            _mm256_storeu_si256((__m256i*)(xi + cnt + 8), q1);
        }
        if (cnt <= end - 8)
        {
#ifdef FAAC_PRECISION_SINGLE
            __m256 x = _mm256_loadu_ps(xr + cnt);
#else
            __m256 x = _mm256_setr_ps((float)xr[cnt], (float)xr[cnt+1], (float)xr[cnt+2], (float)xr[cnt+3],
                                       (float)xr[cnt+4], (float)xr[cnt+5], (float)xr[cnt+6], (float)xr[cnt+7]);
#endif
            __m256 x_abs = _mm256_and_ps(x, abs_mask);
            __m256 tmp = _mm256_mul_ps(x_abs, sfac);
            tmp = _mm256_mul_ps(tmp, _mm256_sqrt_ps(tmp));
            tmp = _mm256_sqrt_ps(tmp);
            tmp = _mm256_add_ps(tmp, magic);
            __m256i q = _mm256_cvttps_epi32(tmp);
            __m256i sign = _mm256_srai_epi32(_mm256_castps_si256(x), 31);
            q = _mm256_sub_epi32(_mm256_xor_si256(q, sign), sign);
            _mm256_storeu_si256((__m256i*)(xi + cnt), q);
            cnt += 8;
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
void quantize_sfb_avx2(int end, int gsize, faac_real sfacfix, const faac_real *xr, int *xi) {}
#endif
