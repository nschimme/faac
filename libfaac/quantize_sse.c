#include "quantize.h"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>

#define MAGIC_NUMBER 0.4054f

void quantize_sfb_sse2(int end, int gsize, faac_real sfacfix, const faac_real *xr, int *xi)
{
    int win, cnt;
    const __m128 zero = _mm_setzero_ps();
    const __m128 sfac = _mm_set1_ps((float)sfacfix);
    const __m128 magic = _mm_set1_ps(MAGIC_NUMBER);

    for (win = 0; win < gsize; win++)
    {
        /* Intentional "overflow" past 'end' to maintain bit-exactness and performance.
         * SFB buffers are large enough (8*MAXSHORTBAND) to accommodate the extra writes. */
        for (cnt = 0; cnt < end; cnt += 4)
        {
#ifdef FAAC_PRECISION_SINGLE
            __m128 x = _mm_loadu_ps(xr + cnt);
#else
            __m128 x = _mm_setr_ps((float)xr[cnt], (float)xr[cnt+1], (float)xr[cnt+2], (float)xr[cnt+3]);
#endif
            x = _mm_max_ps(x, _mm_sub_ps(zero, x));
            x = _mm_mul_ps(x, sfac);
            x = _mm_mul_ps(x, _mm_sqrt_ps(x));
            x = _mm_sqrt_ps(x);
            x = _mm_add_ps(x, magic);

            _mm_storeu_si128((__m128i*)(xi + cnt), _mm_cvttps_epi32(x));
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
void quantize_sfb_sse2(int end, int gsize, faac_real sfacfix, const faac_real *xr, int *xi) {}
#endif
