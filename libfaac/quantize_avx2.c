#include "quantize.h"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>

#define MAGIC_NUMBER 0.4054f

void quantize_sfb_avx2(int end, int gsize, faac_real sfacfix, const faac_real *xr, int *xi)
{
    int win, cnt;
    const __m256 vzero = _mm256_setzero_ps();
    const __m256 vsfac = _mm256_set1_ps((float)sfacfix);
    const __m256 vmagic = _mm256_set1_ps(MAGIC_NUMBER);

    for (win = 0; win < gsize; win++)
    {
        for (cnt = 0; cnt < end; cnt += 8)
        {
#ifdef FAAC_PRECISION_SINGLE
            __m256 x = _mm256_loadu_ps(xr + cnt);
#else
            __m256 x = _mm256_insertf128_ps(_mm256_castps128_ps256(_mm256_cvtpd_ps(_mm256_loadu_pd(xr + cnt))),
                                             _mm256_cvtpd_ps(_mm256_loadu_pd(xr + cnt + 4)), 1);
#endif
            __m256 t = _mm256_mul_ps(_mm256_max_ps(x, _mm256_sub_ps(vzero, x)), vsfac);
            t = _mm256_add_ps(_mm256_sqrt_ps(_mm256_mul_ps(t, _mm256_sqrt_ps(t))), vmagic);

            __m256i q = _mm256_cvttps_epi32(t);
            _mm256_storeu_si256((__m256i*)(xi + cnt), _mm256_sign_epi32(q, _mm256_castps_si256(x)));
        }
        xi += end;
        xr += BLOCK_LEN_SHORT;
    }
}
#else
void quantize_sfb_avx2(int end, int gsize, faac_real sfacfix, const faac_real *xr, int *xi) {}
#endif
