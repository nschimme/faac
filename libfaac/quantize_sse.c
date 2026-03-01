#include "quantize.h"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>

#define MAGIC_NUMBER 0.4054f

void quantize_sfb_sse2(int end, int gsize, faac_real sfacfix, const faac_real *xr, int *xi)
{
    int win, cnt;
    const __m128 vzero = _mm_setzero_ps();
    const __m128 vsfac = _mm_set1_ps((float)sfacfix);
    const __m128 vmagic = _mm_set1_ps(MAGIC_NUMBER);

    for (win = 0; win < gsize; win++)
    {
        /* Intentional overflow for performance. xitab is sufficiently padded. */
        for (cnt = 0; cnt < end; cnt += 4)
        {
#ifdef FAAC_PRECISION_SINGLE
            __m128 x = _mm_loadu_ps(xr + cnt);
#else
            __m128 x = _mm_movelh_ps(_mm_cvtpd_ps(_mm_loadu_pd(xr + cnt)), _mm_cvtpd_ps(_mm_loadu_pd(xr + cnt + 2)));
#endif
            __m128 t = _mm_mul_ps(_mm_max_ps(x, _mm_sub_ps(vzero, x)), vsfac);
            t = _mm_add_ps(_mm_sqrt_ps(_mm_mul_ps(t, _mm_sqrt_ps(t))), vmagic);

            __m128i q = _mm_cvttps_epi32(t);
            __m128i s = _mm_srai_epi32(_mm_castps_si128(x), 31);
            _mm_storeu_si128((__m128i*)(xi + cnt), _mm_sub_epi32(_mm_xor_si128(q, s), s));
        }
        xi += end;
        xr += BLOCK_LEN_SHORT;
    }
}
#else
void quantize_sfb_sse2(int end, int gsize, faac_real sfacfix, const faac_real *xr, int *xi) {}
#endif
