#include "quantize.h"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>

void quantize_sfb_sse2(int end, int gsize, faac_real sfacfix, const faac_real *xr, int *xi)
{
    int win, cnt;

    for (win = 0; win < gsize; win++)
    {
#ifdef FAAC_PRECISION_SINGLE
        const __m128 vsfac = _mm_set1_ps((float)sfacfix);
        const __m128 vmagic = _mm_set1_ps(0.4054f);
        const __m128 abs_mask = _mm_castsi128_ps(_mm_set1_epi32(0x7fffffff));
        const __m128 vzero = _mm_setzero_ps();

        if (end >= 4)
        {
            for (cnt = 0; cnt <= end - 4; cnt += 4)
            {
                __m128 x = _mm_loadu_ps(xr + cnt);
                __m128 t = _mm_mul_ps(_mm_and_ps(x, abs_mask), vsfac);
                t = _mm_add_ps(_mm_sqrt_ps(_mm_mul_ps(t, _mm_sqrt_ps(t))), vmagic);
                __m128i q = _mm_cvttps_epi32(t);
                __m128i s = _mm_srai_epi32(_mm_castps_si128(x), 31);
                _mm_storeu_si128((__m128i*)(xi + cnt), _mm_sub_epi32(_mm_xor_si128(q, s), s));
            }
            if (cnt < end)
            {
                cnt = end - 4;
                __m128 x = _mm_loadu_ps(xr + cnt);
                __m128 t = _mm_mul_ps(_mm_and_ps(x, abs_mask), vsfac);
                t = _mm_add_ps(_mm_sqrt_ps(_mm_mul_ps(t, _mm_sqrt_ps(t))), vmagic);
                __m128i q = _mm_cvttps_epi32(t);
                __m128i s = _mm_srai_epi32(_mm_castps_si128(x), 31);
                _mm_storeu_si128((__m128i*)(xi + cnt), _mm_sub_epi32(_mm_xor_si128(q, s), s));
            }
            cnt = end;
        }
        else cnt = 0;
#else
        const __m128d vdsfac = _mm_set1_pd(sfacfix);
        const __m128d vdmagic = _mm_set1_pd(0.4054);
        const __m128d d_abs_mask = _mm_castsi128_pd(_mm_set1_epi64x(0x7fffffffffffffffULL));
        const __m128d d_vzero = _mm_setzero_pd();

        if (end >= 4)
        {
            for (cnt = 0; cnt <= end - 4; cnt += 4)
            {
                __m128d x0 = _mm_loadu_pd(xr + cnt);
                __m128d x1 = _mm_loadu_pd(xr + cnt + 2);

                __m128d t0 = _mm_mul_pd(_mm_and_pd(x0, d_abs_mask), vdsfac);
                t0 = _mm_add_pd(_mm_sqrt_pd(_mm_mul_pd(t0, _mm_sqrt_pd(t0))), vdmagic);

                __m128d t1 = _mm_mul_pd(_mm_and_pd(x1, d_abs_mask), vdsfac);
                t1 = _mm_add_pd(_mm_sqrt_pd(_mm_mul_pd(t1, _mm_sqrt_pd(t1))), vdmagic);

                __m128i q0 = _mm_cvttpd_epi32(t0); // [q0, q1, 0, 0]
                __m128i q1 = _mm_cvttpd_epi32(t1); // [q2, q3, 0, 0]
                __m128i q = _mm_unpacklo_epi64(q0, q1); // [q0, q1, q2, q3]

                __m128i si0 = _mm_castpd_si128(_mm_cmplt_pd(x0, d_vzero));
                __m128i si1 = _mm_castpd_si128(_mm_cmplt_pd(x1, d_vzero));
                __m128i s = _mm_unpacklo_epi64(
                    _mm_shuffle_epi32(si0, _MM_SHUFFLE(3, 1, 3, 1)),
                    _mm_shuffle_epi32(si1, _MM_SHUFFLE(3, 1, 3, 1))
                );

                _mm_storeu_si128((__m128i*)(xi + cnt), _mm_sub_epi32(_mm_xor_si128(q, s), s));
            }
            if (cnt < end)
            {
                cnt = end - 4;
                __m128d x0 = _mm_loadu_pd(xr + cnt);
                __m128d x1 = _mm_loadu_pd(xr + cnt + 2);
                __m128d t0 = _mm_mul_pd(_mm_and_pd(x0, d_abs_mask), vdsfac);
                t0 = _mm_add_pd(_mm_sqrt_pd(_mm_mul_pd(t0, _mm_sqrt_pd(t0))), vdmagic);
                __m128d t1 = _mm_mul_pd(_mm_and_pd(x1, d_abs_mask), vdsfac);
                t1 = _mm_add_pd(_mm_sqrt_pd(_mm_mul_pd(t1, _mm_sqrt_pd(t1))), vdmagic);
                __m128i q = _mm_unpacklo_epi64(_mm_cvttpd_epi32(t0), _mm_cvttpd_epi32(t1));
                __m128i si0 = _mm_castpd_si128(_mm_cmplt_pd(x0, d_vzero));
                __m128i si1 = _mm_castpd_si128(_mm_cmplt_pd(x1, d_vzero));
                __m128i s = _mm_unpacklo_epi64(_mm_shuffle_epi32(si0, _MM_SHUFFLE(3, 1, 3, 1)), _mm_shuffle_epi32(si1, _MM_SHUFFLE(3, 1, 3, 1)));
                _mm_storeu_si128((__m128i*)(xi + cnt), _mm_sub_epi32(_mm_xor_si128(q, s), s));
            }
            cnt = end;
        }
        else cnt = 0;
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
