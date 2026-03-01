#include "quantize.h"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>

#define MAGIC_NUMBER 0.4054f

void quantize_sfb_avx2(int end, int gsize, faac_real sfacfix, const faac_real *xr, int *xi)
{
    int win, cnt;

    for (win = 0; win < gsize; win++)
    {
#ifdef FAAC_PRECISION_SINGLE
        const __m256 vsfac = _mm256_set1_ps((float)sfacfix);
        const __m256 vmagic = _mm256_set1_ps(MAGIC_NUMBER);
        const __m256 abs_mask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7fffffff));

        for (cnt = 0; cnt <= end - 8; cnt += 8)
        {
            __m256 x = _mm256_loadu_ps(xr + cnt);
            __m256 t = _mm256_mul_ps(_mm256_and_ps(x, abs_mask), vsfac);
            t = _mm256_add_ps(_mm256_sqrt_ps(_mm256_mul_ps(t, _mm256_sqrt_ps(t))), vmagic);

            __m256i q = _mm256_cvttps_epi32(t);
            _mm256_storeu_si256((__m256i*)(xi + cnt), _mm256_sign_epi32(q, _mm256_castps_si256(x)));
        }
#else
        const __m256d vdsfac = _mm256_set1_pd(sfacfix);
        const __m256d vdmagic = _mm256_set1_pd((double)MAGIC_NUMBER);
        const __m256d dabs_mask = _mm256_castsi256_pd(_mm256_set1_epi64x(0x7fffffffffffffffULL));

        for (cnt = 0; cnt <= end - 8; cnt += 8)
        {
            __m256d x0 = _mm256_loadu_pd(xr + cnt);
            __m256d x1 = _mm256_loadu_pd(xr + cnt + 4);

            __m256d t0 = _mm256_mul_pd(_mm256_and_pd(x0, dabs_mask), vdsfac);
            __m256d t1 = _mm256_mul_pd(_mm256_and_pd(x1, dabs_mask), vdsfac);

            t0 = _mm256_add_pd(_mm256_sqrt_pd(_mm256_mul_pd(t0, _mm256_sqrt_pd(t0))), vdmagic);
            t1 = _mm256_add_pd(_mm256_sqrt_pd(_mm256_mul_pd(t1, _mm256_sqrt_pd(t1))), vdmagic);

            __m256i q = _mm256_insertf128_si256(_mm256_castsi128_si256(_mm256_cvttpd_epi32(t0)),
                                                _mm256_cvttpd_epi32(t1), 1);

            int s[8];
            int i;
            for(i=0; i<8; i++) s[i] = (xr[cnt+i] < 0) ? -1 : 0;
            __m256i g = _mm256_loadu_si256((__m256i*)s);

            _mm256_storeu_si256((__m256i*)(xi + cnt), _mm256_sub_epi32(_mm256_xor_si256(q, g), g));
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
void quantize_sfb_avx2(int end, int gsize, faac_real sfacfix, const faac_real *xr, int *xi) {}
#endif
