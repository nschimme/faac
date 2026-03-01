#include "quantize.h"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>

#define MAGIC_NUMBER 0.4054f

void quantize_sfb_sse2(int end, int gsize, faac_real sfacfix, const faac_real *xr, int *xi)
{
    int win, cnt;

    for (win = 0; win < gsize; win++)
    {
#ifdef FAAC_PRECISION_SINGLE
        const __m128 vsfac = _mm_set1_ps((float)sfacfix);
        const __m128 vmagic = _mm_set1_ps(MAGIC_NUMBER);
        const __m128 abs_mask = _mm_castsi128_ps(_mm_set1_epi32(0x7fffffff));

        for (cnt = 0; cnt <= end - 4; cnt += 4)
        {
            __m128 x = _mm_loadu_ps(xr + cnt);
            __m128 t = _mm_mul_ps(_mm_and_ps(x, abs_mask), vsfac);
            t = _mm_add_ps(_mm_sqrt_ps(_mm_mul_ps(t, _mm_sqrt_ps(t))), vmagic);

            __m128i q = _mm_cvttps_epi32(t);
            __m128i s = _mm_srai_epi32(_mm_castps_si128(x), 31);
            _mm_storeu_si128((__m128i*)(xi + cnt), _mm_sub_epi32(_mm_xor_si128(q, s), s));
        }
#else
        const __m128d vdsfac = _mm_set1_pd(sfacfix);
        const __m128d vdmagic = _mm_set1_pd((double)MAGIC_NUMBER);
        const __m128d dabs_mask = _mm_castsi128_pd(_mm_set_epi64x(0x7fffffffffffffffULL, 0x7fffffffffffffffULL));

        for (cnt = 0; cnt <= end - 4; cnt += 4)
        {
            __m128d x0 = _mm_loadu_pd(xr + cnt);
            __m128d x1 = _mm_loadu_pd(xr + cnt + 2);

            __m128d t0 = _mm_mul_pd(_mm_and_pd(x0, dabs_mask), vdsfac);
            __m128d t1 = _mm_mul_pd(_mm_and_pd(x1, dabs_mask), vdsfac);

            t0 = _mm_add_pd(_mm_sqrt_pd(_mm_mul_pd(t0, _mm_sqrt_pd(t0))), vdmagic);
            t1 = _mm_add_pd(_mm_sqrt_pd(_mm_mul_pd(t1, _mm_sqrt_pd(t1))), vdmagic);

            __m128i q = _mm_unpacklo_epi64(_mm_cvttpd_epi32(t0), _mm_cvttpd_epi32(t1));

            __m128i s0 = _mm_srai_epi32(_mm_castpd_si128(x0), 31);
            __m128i s1 = _mm_srai_epi32(_mm_castpd_si128(x1), 31);
            __m128i s = _mm_unpacklo_epi64(_mm_shuffle_epi32(s0, _MM_SHUFFLE(3, 1, 3, 1)),
                                           _mm_shuffle_epi32(s1, _MM_SHUFFLE(3, 1, 3, 1)));

            _mm_storeu_si128((__m128i*)(xi + cnt), _mm_sub_epi32(_mm_xor_si128(q, s), s));
        }
#endif
        for (; cnt < end; cnt++)
        {
            faac_real tmp = FAAC_FABS(xr[cnt]);
            tmp *= sfacfix;
            tmp = FAAC_SQRT(tmp * FAAC_SQRT(tmp));
            xi[cnt] = (int)(tmp + (faac_real)0.4054);
            if (xr[cnt] < 0) xi[cnt] = -xi[cnt];
        }
        xi += end;
        xr += BLOCK_LEN_SHORT;
    }
}
#else
void quantize_sfb_sse2(int end, int gsize, faac_real sfacfix, const faac_real *xr, int *xi) {}
#endif
