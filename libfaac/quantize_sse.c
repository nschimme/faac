#include <immintrin.h>
#include <math.h>
#include "coder.h"

#define MAGIC_NUMBER 0.4054

void quantize_sse2(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix)
{
    const __m128 sfac = _mm_set1_ps((float)sfacfix);
    const __m128 magic = _mm_set1_ps(MAGIC_NUMBER);
    const __m128 zero = _mm_setzero_ps();
    int cnt;
    int n4 = n & ~3;

    for (cnt = 0; cnt < n4; cnt += 4)
    {
        __m128 x;
#ifdef FAAC_PRECISION_DOUBLE
        // Original quirk: cast double to float for SSE2
        x = _mm_set_ps((float)xr[cnt + 3], (float)xr[cnt + 2], (float)xr[cnt + 1], (float)xr[cnt]);
#else
        x = _mm_loadu_ps(xr + cnt);
#endif

        // Original logic: abs(x)
        x = _mm_max_ps(x, _mm_sub_ps(zero, x));

        // tmp = abs(x) * sfacfix
        x = _mm_mul_ps(x, sfac);

        // tmp = sqrt(tmp * sqrt(tmp))
        x = _mm_mul_ps(x, _mm_sqrt_ps(x));
        x = _mm_sqrt_ps(x);

        // q = (int)(tmp + MAGIC_NUMBER)
        x = _mm_add_ps(x, magic);

        _mm_storeu_si128((__m128i*)(xi + cnt), _mm_cvttps_epi32(x));
    }

    // Original logic: separate scalar loop for sign application
    // and also handles remainder
    for (cnt = 0; cnt < n; cnt++)
    {
        if (xr[cnt] < 0)
            xi[cnt] = -xi[cnt];
    }

    // Remainder quantization (if not handled by the loop above because n was not multiple of 4)
    // Actually the original SSE2 loop in quantize.c was:
    /*
              for (cnt = 0; cnt < end; cnt += 4)
              {
                  __m128 x = {xr[cnt], xr[cnt + 1], xr[cnt + 2], xr[cnt + 3]};

                  x = _mm_max_ps(x, _mm_sub_ps(zero, x));
                  x = _mm_mul_ps(x, sfac);
                  x = _mm_mul_ps(x, _mm_sqrt_ps(x));
                  x = _mm_sqrt_ps(x);
                  x = _mm_add_ps(x, magic);

                  *(__m128i*)(xi + cnt) = _mm_cvttps_epi32(x);
              }
              for (cnt = 0; cnt < end; cnt++)
              {
                  if (xr[cnt] < 0)
                      xi[cnt] = -xi[cnt];
              }
    */
    // Wait, the original code DID have a buffer overflow if end was not a multiple of 4!
    // My n4 loop handles the SIMD part safely.
    // I need to make sure the remainder is quantized too if I use n4.

    for (cnt = n4; cnt < n; cnt++)
    {
        float val = (float)xr[cnt];
        float tmp = (val < 0.0f) ? -val : val;
        tmp *= (float)sfacfix;
        tmp = sqrtf(tmp * sqrtf(tmp));
        xi[cnt] = (int)(tmp + MAGIC_NUMBER);
        if (val < 0.0f)
            xi[cnt] = -xi[cnt];
    }
}
