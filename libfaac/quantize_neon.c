#include "quantize.h"

#if defined(__aarch64__) || defined(__arm__)
#include <arm_neon.h>

#define MAGIC_NUMBER 0.4054f

void quantize_sfb_neon(int end, int gsize, faac_real sfacfix, const faac_real *xr, int *xi)
{
    int win, cnt;
    float32x4_t vsfac = vdupq_n_f32((float)sfacfix);
    float32x4_t vmagic = vdupq_n_f32(MAGIC_NUMBER);
    float32x4_t vzero = vdupq_n_f32(0.0f);

    for (win = 0; win < gsize; win++)
    {
        for (cnt = 0; cnt <= end - 4; cnt += 4)
        {
#ifdef FAAC_PRECISION_SINGLE
            float32x4_t x = vld1q_f32(xr + cnt);
#else
#ifdef __aarch64__
            /* Correctly load doubles and convert to float SIMD on AArch64 */
            float32x4_t x = vcombine_f32(vcvt_f32_f64(vld1q_f64(xr + cnt)), vcvt_f32_f64(vld1q_f64(xr + cnt + 2)));
#else
            /* Scalar fallback for 32-bit ARM double precision */
            float x_arr[4] = {(float)xr[cnt], (float)xr[cnt+1], (float)xr[cnt+2], (float)xr[cnt+3]};
            float32x4_t x = vld1q_f32(x_arr);
#endif
#endif
            float32x4_t x_abs = vabsq_f32(x);
            float32x4_t tmp = vmulq_f32(x_abs, vsfac);

#if defined(__aarch64__)
            /* vsqrtq_f32 is AArch64 only */
            float32x4_t s = vsqrtq_f32(tmp);
            tmp = vmulq_f32(tmp, s);
            tmp = vaddq_f32(vsqrtq_f32(tmp), vmagic);
#else
            /* Newton-Raphson approximation for ARMv7 */
            uint32x4_t m = vcltq_f32(vzero, tmp);
            float32x4_t r = vrsqrteq_f32(tmp);
            r = vmulq_f32(r, vrsqrtsq_f32(vmulq_f32(r, r), tmp));
            float32x4_t sv = vbslq_f32(m, vmulq_f32(tmp, r), vzero);
            tmp = vmulq_f32(tmp, sv);
            m = vcltq_f32(vzero, tmp);
            r = vrsqrteq_f32(tmp);
            r = vmulq_f32(r, vrsqrtsq_f32(vmulq_f32(r, r), tmp));
            tmp = vaddq_f32(vbslq_f32(m, vmulq_f32(tmp, r), vzero), vmagic);
#endif
            int32x4_t q = vcvtq_s32_f32(tmp);
            int32x4_t g = vreinterpretq_s32_u32(vcltq_f32(x, vzero));
            vst1q_s32(xi + cnt, vsubq_s32(veorq_s32(q, g), g));
        }
        for (; cnt < end; cnt++)
        {
            faac_real tmp = FAAC_FABS(xr[cnt]);
            tmp *= sfacfix;
            tmp = FAAC_SQRT(tmp * FAAC_SQRT(tmp));
            xi[cnt] = (int)(tmp + (faac_real)MAGIC_NUMBER);
            if (xr[cnt] < 0) xi[cnt] = -xi[cnt];
        }
        xi += end;
        xr += BLOCK_LEN_SHORT;
    }
}
#else
void quantize_sfb_neon(int end, int gsize, faac_real sfacfix, const faac_real *xr, int *xi) {}
#endif
