#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <immintrin.h>
#include <math.h>
#include <stdint.h>
#include "coder.h"
#include "faac_real.h"

#define MAGIC_NUMBER 0.4054

#ifdef FAAC_PRECISION_DOUBLE
static inline __m128i apply_sign_pd(__m128i i, __m256d s, __m256i vidx)
{
    __m256i s_i = _mm256_castpd_si256(s);
    __m256i s_packed = _mm256_permutevar8x32_epi32(s_i, vidx);
    __m128i s_128 = _mm256_castsi256_si128(s_packed);
    return _mm_sub_epi32(_mm_xor_si128(i, s_128), s_128);
}
#endif

void quantize_avx2(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix)
{
    int cnt = 0;
#ifdef FAAC_PRECISION_DOUBLE
    const __m256d sfac = _mm256_set1_pd(sfacfix);
    const __m256d magic = _mm256_set1_pd(MAGIC_NUMBER);
    const __m256d zero = _mm256_setzero_pd();
    const __m256d abs_mask = _mm256_castsi256_pd(_mm256_set1_epi64x(0x7FFFFFFFFFFFFFFFULL));
    const __m256i vidx = _mm256_set_epi32(7, 5, 3, 1, 6, 4, 2, 0);

    if (n >= 4)
    {
        int n4 = n & ~3;
        for (; cnt < n4; cnt += 4)
        {
            __m256d x = _mm256_loadu_pd(xr + cnt);
            __m256d a = _mm256_and_pd(x, abs_mask);
            __m256d t = _mm256_mul_pd(a, sfac);
            t = _mm256_mul_pd(t, _mm256_sqrt_pd(t));
            t = _mm256_sqrt_pd(t);
            __m128i i = _mm256_cvttpd_epi32(_mm256_add_pd(t, magic));
            __m256d s = _mm256_cmp_pd(x, zero, _CMP_LT_OQ);
            i = apply_sign_pd(i, s, vidx);
            // Use unaligned store because individual bands may not be aligned
            _mm_storeu_si128((__m128i*)(xi + cnt), i);
        }
    }
#else
    const __m256 sfac = _mm256_set1_ps(sfacfix);
    const __m256 magic = _mm256_set1_ps(MAGIC_NUMBER);
    const __m256 zero = _mm256_setzero_ps();
    const __m256 abs_mask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7FFFFFFF));

    if (n >= 8)
    {
        int n8 = n & ~7;
        for (; cnt < n8; cnt += 8)
        {
            __m256 x = _mm256_loadu_ps(xr + cnt);
            __m256 a = _mm256_and_ps(x, abs_mask);
            __m256 t = _mm256_mul_ps(a, sfac);
            t = _mm256_mul_ps(t, _mm256_sqrt_ps(t));
            t = _mm256_sqrt_ps(t);
            __m256i i = _mm256_cvttps_epi32(_mm256_add_ps(t, magic));
            __m256i s = _mm256_castps_si256(_mm256_cmp_ps(x, zero, _CMP_LT_OQ));
            i = _mm256_sub_epi32(_mm256_xor_si256(i, s), s);
            // Use unaligned store because individual bands may not be aligned
            _mm256_storeu_si256((__m256i*)(xi + cnt), i);
        }
    }
#endif

    // Bit-exact residual loop matching quantize_scalar
    for (; cnt < n; cnt++)
    {
        faac_real val = xr[cnt];
        faac_real tmp = FAAC_FABS(val);
        tmp *= sfacfix;
        tmp = FAAC_SQRT(tmp * FAAC_SQRT(tmp));
        int q = (int)(tmp + (faac_real)MAGIC_NUMBER);
        xi[cnt] = (val < 0) ? -q : q;
    }
}
