#include "quantize.h"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>

#define MAGIC_NUMBER 0.4054f

void quantize_sfb_avx2(int end, int gsize, faac_real sfacfix, const faac_real *xr, int *xi)
{
    int win, cnt;
    const __m256 zero = _mm256_setzero_ps();
    const __m256 sfac = _mm256_set1_ps((float)sfacfix);
    const __m256 magic = _mm256_set1_ps(MAGIC_NUMBER);

    for (win = 0; win < gsize; win++)
    {
        for (cnt = 0; cnt < end; cnt += 8)
        {
#ifdef FAAC_PRECISION_SINGLE
            __m256 x = _mm256_loadu_ps(xr + cnt);
#else
            __m256 x = _mm256_setr_ps((float)xr[cnt], (float)xr[cnt+1], (float)xr[cnt+2], (float)xr[cnt+3],
                                       (float)xr[cnt+4], (float)xr[cnt+5], (float)xr[cnt+6], (float)xr[cnt+7]);
#endif
            x = _mm256_max_ps(x, _mm256_sub_ps(zero, x));
            x = _mm256_mul_ps(x, sfac);
            x = _mm256_mul_ps(x, _mm256_sqrt_ps(x));
            x = _mm256_sqrt_ps(x);
            x = _mm256_add_ps(x, magic);

            _mm256_storeu_si256((__m256i*)(xi + cnt), _mm256_cvttps_epi32(x));
        }
        for (cnt = 0; cnt < end; cnt++)
        {
            if (xr[cnt] < 0)
                xi[cnt] = -xi[cnt];
        }
        xi += end;
        xr += BLOCK_LEN_SHORT;
    }
}
#else
void quantize_sfb_avx2(int end, int gsize, faac_real sfacfix, const faac_real *xr, int *xi) {}
#endif
