#include <immintrin.h>
#include <math.h>
#include "faac_real.h"
#include "coder.h"
#include "huff2.h"

#define MAGIC_NUMBER 0.4054

void quantize_sse2(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix)
{
    const __m128 zero = _mm_setzero_ps();
    const __m128 sfac = _mm_set1_ps((float)sfacfix);
    const __m128 magic = _mm_set1_ps(MAGIC_NUMBER);
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

        // Compute absolute value
        __m128 abs_x = _mm_max_ps(x, _mm_sub_ps(zero, x));

        // tmp = abs(x) * sfacfix
        __m128 tmp = _mm_mul_ps(abs_x, sfac);

        // tmp = sqrt(tmp * sqrt(tmp))
        tmp = _mm_mul_ps(tmp, _mm_sqrt_ps(tmp));
        tmp = _mm_sqrt_ps(tmp);

        // q = (int)(tmp + MAGIC_NUMBER)
        __m128 q = _mm_add_ps(tmp, magic);
        __m128i iq = _mm_cvttps_epi32(q);

        // Apply sign: if x < 0, iq = -iq
        __m128 sign_mask = _mm_cmplt_ps(x, zero);
        __m128i isign_mask = _mm_castps_si128(sign_mask);

        // Result = (iq ^ mask) - mask
        iq = _mm_sub_epi32(_mm_xor_si128(iq, isign_mask), isign_mask);

        _mm_storeu_si128((__m128i*)(xi + cnt), iq);
    }

    // Handle remainder
    for (; cnt < n; cnt++)
    {
        float val = (float)xr[cnt];
        float tmp = (val < 0.0f) ? -val : val;
        tmp *= (float)sfacfix;
        tmp = sqrtf(tmp * sqrtf(tmp));
        int q = (int)(tmp + MAGIC_NUMBER);
        xi[cnt] = (val < 0.0f) ? -q : q;
    }
}
