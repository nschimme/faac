#include "quantize.h"

#if defined(__aarch64__) || defined(__arm__)
#include <arm_neon.h>

#define MAGIC_NUMBER 0.4054f

void quantize_sfb_neon(int end, int gsize, faac_real sfacfix, const faac_real *xr, int *xi)
{
    int win, cnt;
    float32x4_t sfac = vdupq_n_f32((float)sfacfix);
    float32x4_t magic = vdupq_n_f32(MAGIC_NUMBER);
    float32x4_t zero = vdupq_n_f32(0.0f);

    for (win = 0; win < gsize; win++)
    {
        for (cnt = 0; cnt < end; cnt += 4)
        {
#ifdef FAAC_PRECISION_SINGLE
            float32x4_t x = vld1q_f32(xr + cnt);
#else
            /* Efficiently convert double to float using NEON (AArch64) */
#ifdef __aarch64__
            float32x4_t x = vcombine_f32(vcvt_f32_f64(vld1q_f64(xr + cnt)), vcvt_f32_f64(vld1q_f64(xr + cnt + 2)));
#else
            float32x4_t x = (float32x4_t){(float)xr[cnt], (float)xr[cnt+1], (float)xr[cnt+2], (float)xr[cnt+3]};
#endif
#endif
            float32x4_t x_abs = vabsq_f32(x);
            float32x4_t tmp = vmulq_f32(x_abs, sfac);

#if defined(__aarch64__)
            float32x4_t sqrt_tmp = vsqrtq_f32(tmp);
            tmp = vmulq_f32(tmp, sqrt_tmp);
            tmp = vsqrtq_f32(tmp);
#else
            /* Sequence of reciprocal square root steps for ARMv7, handling zero */
            uint32x4_t mask = vcltq_f32(zero, tmp);
            float32x4_t r = vrsqrteq_f32(tmp);
            r = vmulq_f32(r, vrsqrtsq_f32(vmulq_f32(r, r), tmp));
            float32x4_t sqrt_val = vmulq_f32(tmp, r);
            sqrt_val = vbslq_f32(mask, sqrt_val, zero);
            tmp = vmulq_f32(tmp, sqrt_val);

            mask = vcltq_f32(zero, tmp);
            r = vrsqrteq_f32(tmp);
            r = vmulq_f32(r, vrsqrtsq_f32(vmulq_f32(r, r), tmp));
            sqrt_val = vmulq_f32(tmp, r);
            tmp = vbslq_f32(mask, sqrt_val, zero);
#endif
            tmp = vaddq_f32(tmp, magic);

            int32x4_t q = vcvtq_s32_f32(tmp);
            vst1q_s32(xi + cnt, q);
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
void quantize_sfb_neon(int end, int gsize, faac_real sfacfix, const faac_real *xr, int *xi) {}
#endif
