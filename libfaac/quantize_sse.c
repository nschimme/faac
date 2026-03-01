#include "quantize_internal.h"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>

#define MAGIC_NUMBER 0.4054f

void quantize_lines_sse2(int end, faac_real sfacfix, const faac_real *xr, int *xi)
{
    int cnt = 0;
#ifdef FAAC_PRECISION_SINGLE
    const __m128 sfac = _mm_set1_ps((float)sfacfix);
    const __m128 magic = _mm_set1_ps(MAGIC_NUMBER);
    const __m128 abs_mask = _mm_castsi128_ps(_mm_set1_epi32(0x7fffffff));

    for (cnt = 0; cnt <= end - 4; cnt += 4)
    {
        __m128 x = _mm_loadu_ps(xr + cnt);
        __m128 x_abs = _mm_and_ps(x, abs_mask);
        __m128 tmp = _mm_mul_ps(x_abs, sfac);
        tmp = _mm_mul_ps(tmp, _mm_sqrt_ps(tmp));
        tmp = _mm_sqrt_ps(tmp);
        tmp = _mm_add_ps(tmp, magic);

        __m128i q = _mm_cvttps_epi32(tmp);

        /* Apply sign */
        __m128i sign = _mm_srai_epi32(_mm_castps_si128(x), 31);
        q = _mm_sub_epi32(_mm_xor_si128(q, sign), sign);

        _mm_storeu_si128((__m128i*)(xi + cnt), q);
    }
#else
    /* Double precision */
    const __m128d sfac = _mm_set1_pd(sfacfix);
    const __m128d magic = _mm_set1_pd((double)MAGIC_NUMBER);
    const __m128d abs_mask = _mm_castsi128_pd(_mm_set_epi64x(0x7fffffffffffffffULL, 0x7fffffffffffffffULL));

    for (cnt = 0; cnt <= end - 2; cnt += 2)
    {
        __m128d x = _mm_loadu_pd(xr + cnt);
        __m128d x_abs = _mm_and_pd(x, abs_mask);
        __m128d tmp = _mm_mul_pd(x_abs, sfac);
        tmp = _mm_mul_pd(tmp, _mm_sqrt_pd(tmp));
        tmp = _mm_sqrt_pd(tmp);
        tmp = _mm_add_pd(tmp, magic);

        __m128i q = _mm_cvttpd_epi32(tmp);

        int sign0 = (xr[cnt] < 0) ? -1 : 0;
        int sign1 = (xr[cnt+1] < 0) ? -1 : 0;
        __m128i sign = _mm_setr_epi32(sign0, sign1, 0, 0);

        q = _mm_sub_epi32(_mm_xor_si128(q, sign), sign);

        _mm_storel_epi64((__m128i*)(xi + cnt), q);
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
void quantize_lines_sse2(int end, faac_real sfacfix, const faac_real *xr, int *xi) {}
#endif
