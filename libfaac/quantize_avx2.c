#include "quantize_internal.h"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>

#define MAGIC_NUMBER 0.4054f

void quantize_lines_avx2(int end, faac_real sfacfix, const faac_real *xr, int *xi)
{
    int cnt = 0;
#ifdef FAAC_PRECISION_SINGLE
    const __m256 sfac = _mm256_set1_ps((float)sfacfix);
    const __m256 magic = _mm256_set1_ps(MAGIC_NUMBER);
    const __m256 abs_mask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7fffffff));

    for (cnt = 0; cnt <= end - 8; cnt += 8)
    {
        __m256 x = _mm256_loadu_ps(xr + cnt);
        __m256 x_abs = _mm256_and_ps(x, abs_mask);
        __m256 tmp = _mm256_mul_ps(x_abs, sfac);
        tmp = _mm256_mul_ps(tmp, _mm256_sqrt_ps(tmp));
        tmp = _mm256_sqrt_ps(tmp);
        tmp = _mm256_add_ps(tmp, magic);

        __m256i q = _mm256_cvttps_epi32(tmp);

        /* Apply sign */
        __m256i sign = _mm256_srai_epi32(_mm256_castps_si256(x), 31);
        q = _mm256_sub_epi32(_mm256_xor_si256(q, sign), sign);

        _mm256_storeu_si256((__m256i*)(xi + cnt), q);
    }
#else
    /* Double precision with AVX2 */
    const __m256d sfac = _mm256_set1_pd(sfacfix);
    const __m256d magic = _mm256_set1_pd((double)MAGIC_NUMBER);
    const __m256d abs_mask = _mm256_castsi256_pd(_mm256_set1_epi64x(0x7fffffffffffffffULL));

    for (cnt = 0; cnt <= end - 4; cnt += 4)
    {
        __m256d x = _mm256_loadu_pd(xr + cnt);
        __m256d x_abs = _mm256_and_pd(x, abs_mask);
        __m256d tmp = _mm256_mul_pd(x_abs, sfac);
        tmp = _mm256_mul_pd(tmp, _mm256_sqrt_pd(tmp));
        tmp = _mm256_sqrt_pd(tmp);
        tmp = _mm256_add_pd(tmp, magic);

        __m128i q = _mm256_cvttpd_epi32(tmp);

        int s[4];
        int i;
        for(i=0; i<4; i++) s[i] = (xr[cnt+i] < 0) ? -1 : 0;
        __m128i sign = _mm_loadu_si128((__m128i*)s);

        q = _mm_sub_epi32(_mm_xor_si128(q, sign), sign);

        _mm_storeu_si128((__m128i*)(xi + cnt), q);
    }
#endif

    for (; cnt < end; cnt++)
    {
        faac_real tmp = FAAC_FABS(xr[cnt]);
        tmp *= sfacfix;
        tmp = FAAC_SQRT(tmp * FAAC_SQRT(tmp));
        xi[cnt] = (int)(tmp + (faac_real)MAGIC_NUMBER);
        if (xr[cnt] < 0)
            xi[cnt] = -xi[cnt];
    }
}
#else
void quantize_lines_avx2(int end, faac_real sfacfix, const faac_real *xr, int *xi) {}
#endif
