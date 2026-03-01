#include "quantize.h"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>

void quantize_sfb_avx2(int end, int gsize, faac_real sfacfix, const faac_real *xr, int *xi)
{
    int win, cnt;

    for (win = 0; win < gsize; win++)
    {
#ifdef FAAC_PRECISION_SINGLE
        const __m256 vsfac = _mm256_set1_ps((float)sfacfix);
        const __m256 vmagic = _mm256_set1_ps(0.4054f);
        const __m256 abs_mask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7fffffff));

        if (end >= 8)
        {
            for (cnt = 0; cnt <= end - 8; cnt += 8)
            {
                __m256 x = _mm256_loadu_ps(xr + cnt);
                __m256 t = _mm256_mul_ps(_mm256_and_ps(x, abs_mask), vsfac);
                t = _mm256_add_ps(_mm256_sqrt_ps(_mm256_mul_ps(t, _mm256_sqrt_ps(t))), vmagic);
                __m256i q = _mm256_cvttps_epi32(t);
                _mm256_storeu_si256((__m256i*)(xi + cnt), _mm256_sign_epi32(q, _mm256_castps_si256(x)));
            }
            if (cnt < end)
            {
                cnt = end - 8;
                __m256 x = _mm256_loadu_ps(xr + cnt);
                __m256 t = _mm256_mul_ps(_mm256_and_ps(x, abs_mask), vsfac);
                t = _mm256_add_ps(_mm256_sqrt_ps(_mm256_mul_ps(t, _mm256_sqrt_ps(t))), vmagic);
                __m256i q = _mm256_cvttps_epi32(t);
                _mm256_storeu_si256((__m256i*)(xi + cnt), _mm256_sign_epi32(q, _mm256_castps_si256(x)));
            }
            cnt = end;
        }
        else cnt = 0;
#else
        const __m256d vdsfac = _mm256_set1_pd(sfacfix);
        const __m256d vdmagic = _mm256_set1_pd(0.4054);
        const __m256d d_abs_mask = _mm256_castsi256_pd(_mm256_set1_epi64x(0x7fffffffffffffffULL));
        const __m256d d_vzero = _mm256_setzero_pd();

        if (end >= 8)
        {
            for (cnt = 0; cnt <= end - 8; cnt += 8)
            {
                __m256d x0 = _mm256_loadu_pd(xr + cnt);
                __m256d x1 = _mm256_loadu_pd(xr + cnt + 4);

                __m256d t0 = _mm256_mul_pd(_mm256_and_pd(x0, d_abs_mask), vdsfac);
                t0 = _mm256_add_pd(_mm256_sqrt_pd(_mm256_mul_pd(t0, _mm256_sqrt_pd(t0))), vdmagic);

                __m256d t1 = _mm256_mul_pd(_mm256_and_pd(x1, d_abs_mask), vdsfac);
                t1 = _mm256_add_pd(_mm256_sqrt_pd(_mm256_mul_pd(t1, _mm256_sqrt_pd(t1))), vdmagic);

                __m128i q0 = _mm256_cvttpd_epi32(t0);
                __m128i q1 = _mm256_cvttpd_epi32(t1);
                __m256i q = _mm256_insertf128_si256(_mm256_castsi128_si256(q0), q1, 1);

                __m256i si0 = _mm256_castpd_si256(_mm256_cmp_pd(x0, d_vzero, _CMP_LT_OS));
                __m256i si1 = _mm256_castpd_si256(_mm256_cmp_pd(x1, d_vzero, _CMP_LT_OS));

                // Sign bits are in 64-bit lanes. We need them in 32-bit lanes for _mm256_sign_epi32.
                // Pack 8x64b sign masks into 8x32b.
                __m128i s0 = _mm_shuffle_epi32(_mm256_castsi256_si128(si0), _MM_SHUFFLE(3, 1, 3, 1));
                __m128i s1 = _mm_shuffle_epi32(_mm256_extractf128_si256(si0, 1), _MM_SHUFFLE(3, 1, 3, 1));
                __m128i s2 = _mm_shuffle_epi32(_mm256_castsi256_si128(si1), _MM_SHUFFLE(3, 1, 3, 1));
                __m128i s3 = _mm_shuffle_epi32(_mm256_extractf128_si256(si1, 1), _MM_SHUFFLE(3, 1, 3, 1));

                __m128i sa = _mm_unpacklo_epi64(s0, s1);
                __m128i sb = _mm_unpacklo_epi64(s2, s3);
                __m256i s = _mm256_insertf128_si256(_mm256_castsi128_si256(sa), sb, 1);

                _mm256_storeu_si256((__m256i*)(xi + cnt), _mm256_sub_epi32(_mm256_xor_si256(q, s), s));
            }
            if (cnt < end)
            {
                cnt = end - 8;
                __m256d x0 = _mm256_loadu_pd(xr + cnt);
                __m256d x1 = _mm256_loadu_pd(xr + cnt + 4);
                __m256d t0 = _mm256_mul_pd(_mm256_and_pd(x0, d_abs_mask), vdsfac);
                t0 = _mm256_add_pd(_mm256_sqrt_pd(_mm256_mul_pd(t0, _mm256_sqrt_pd(t0))), vdmagic);
                __m256d t1 = _mm256_mul_pd(_mm256_and_pd(x1, d_abs_mask), vdsfac);
                t1 = _mm256_add_pd(_mm256_sqrt_pd(_mm256_mul_pd(t1, _mm256_sqrt_pd(t1))), vdmagic);
                __m256i q = _mm256_insertf128_si256(_mm256_castsi128_si256(_mm256_cvttpd_epi32(t0)), _mm256_cvttpd_epi32(t1), 1);
                __m256i si0 = _mm256_castpd_si256(_mm256_cmp_pd(x0, d_vzero, _CMP_LT_OS));
                __m256i si1 = _mm256_castpd_si256(_mm256_cmp_pd(x1, d_vzero, _CMP_LT_OS));
                __m128i sa = _mm_unpacklo_epi64(_mm_shuffle_epi32(_mm256_castsi256_si128(si0), _MM_SHUFFLE(3, 1, 3, 1)), _mm_shuffle_epi32(_mm256_extractf128_si256(si0, 1), _MM_SHUFFLE(3, 1, 3, 1)));
                __m128i sb = _mm_unpacklo_epi64(_mm_shuffle_epi32(_mm256_castsi256_si128(si1), _MM_SHUFFLE(3, 1, 3, 1)), _mm_shuffle_epi32(_mm256_extractf128_si256(si1, 1), _MM_SHUFFLE(3, 1, 3, 1)));
                __m256i s = _mm256_insertf128_si256(_mm256_castsi128_si256(sa), sb, 1);
                _mm256_storeu_si256((__m256i*)(xi + cnt), _mm256_sub_epi32(_mm256_xor_si256(q, s), s));
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
void quantize_sfb_avx2(int end, int gsize, faac_real sfacfix, const faac_real *xr, int *xi) {}
#endif
