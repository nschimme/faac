#include "quantize.h"

#if defined(__aarch64__) || defined(__arm__)
#include <arm_neon.h>

#define MAGIC_NUMBER 0.4054f

void quantize_sfb_neon(int end, int gsize, faac_real sfacfix, const faac_real *xr, int *xi)
{
    int win, cnt;
    float32x4_t sfac = vdupq_n_f32((float)sfacfix);
    float32x4_t magic = vdupq_n_f32(MAGIC_NUMBER);

    for (win = 0; win < gsize; win++)
    {
        for (cnt = 0; cnt <= end - 16; cnt += 16)
        {
#ifdef FAAC_PRECISION_SINGLE
            float32x4_t x0 = vld1q_f32(xr + cnt);
            float32x4_t x1 = vld1q_f32(xr + cnt + 4);
            float32x4_t x2 = vld1q_f32(xr + cnt + 8);
            float32x4_t x3 = vld1q_f32(xr + cnt + 12);
#else
            float32x4_t x0 = (float32x4_t){(float)xr[cnt], (float)xr[cnt+1], (float)xr[cnt+2], (float)xr[cnt+3]};
            float32x4_t x1 = (float32x4_t){(float)xr[cnt+4], (float)xr[cnt+5], (float)xr[cnt+6], (float)xr[cnt+7]};
            float32x4_t x2 = (float32x4_t){(float)xr[cnt+8], (float)xr[cnt+9], (float)xr[cnt+10], (float)xr[cnt+11]};
            float32x4_t x3 = (float32x4_t){(float)xr[cnt+12], (float)xr[cnt+13], (float)xr[cnt+14], (float)xr[cnt+15]};
#endif

            float32x4_t x0_abs = vabsq_f32(x0);
            float32x4_t x1_abs = vabsq_f32(x1);
            float32x4_t x2_abs = vabsq_f32(x2);
            float32x4_t x3_abs = vabsq_f32(x3);

            float32x4_t tmp0 = vmulq_f32(x0_abs, sfac);
            float32x4_t tmp1 = vmulq_f32(x1_abs, sfac);
            float32x4_t tmp2 = vmulq_f32(x2_abs, sfac);
            float32x4_t tmp3 = vmulq_f32(x3_abs, sfac);

#if defined(__aarch64__)
            float32x4_t sqrt0 = vsqrtq_f32(tmp0);
            float32x4_t sqrt1 = vsqrtq_f32(tmp1);
            float32x4_t sqrt2 = vsqrtq_f32(tmp2);
            float32x4_t sqrt3 = vsqrtq_f32(tmp3);
#else
            /* Sequence of reciprocal square root steps for ARMv7 */
            float32x4_t r0 = vrsqrteq_f32(tmp0);
            r0 = vmulq_f32(r0, vrsqrtsq_f32(vmulq_f32(r0, r0), tmp0));
            float32x4_t sqrt0 = vmulq_f32(tmp0, r0);

            float32x4_t r1 = vrsqrteq_f32(tmp1);
            r1 = vmulq_f32(r1, vrsqrtsq_f32(vmulq_f32(r1, r1), tmp1));
            float32x4_t sqrt1 = vmulq_f32(tmp1, r1);

            float32x4_t r2 = vrsqrteq_f32(tmp2);
            r2 = vmulq_f32(r2, vrsqrtsq_f32(vmulq_f32(r2, r2), tmp2));
            float32x4_t sqrt2 = vmulq_f32(tmp2, r2);

            float32x4_t r3 = vrsqrteq_f32(tmp3);
            r3 = vmulq_f32(r3, vrsqrtsq_f32(vmulq_f32(r3, r3), tmp3));
            float32x4_t sqrt3 = vmulq_f32(tmp3, r3);
#endif

            tmp0 = vmulq_f32(tmp0, sqrt0);
            tmp1 = vmulq_f32(tmp1, sqrt1);
            tmp2 = vmulq_f32(tmp2, sqrt2);
            tmp3 = vmulq_f32(tmp3, sqrt3);

#if defined(__aarch64__)
            tmp0 = vsqrtq_f32(tmp0);
            tmp1 = vsqrtq_f32(tmp1);
            tmp2 = vsqrtq_f32(tmp2);
            tmp3 = vsqrtq_f32(tmp3);
#else
            r0 = vrsqrteq_f32(tmp0);
            r0 = vmulq_f32(r0, vrsqrtsq_f32(vmulq_f32(r0, r0), tmp0));
            tmp0 = vmulq_f32(tmp0, r0);

            r1 = vrsqrteq_f32(tmp1);
            r1 = vmulq_f32(r1, vrsqrtsq_f32(vmulq_f32(r1, r1), tmp1));
            tmp1 = vmulq_f32(tmp1, r1);

            r2 = vrsqrteq_f32(tmp2);
            r2 = vmulq_f32(r2, vrsqrtsq_f32(vmulq_f32(r2, r2), tmp2));
            tmp2 = vmulq_f32(tmp2, r2);

            r3 = vrsqrteq_f32(tmp3);
            r3 = vmulq_f32(r3, vrsqrtsq_f32(vmulq_f32(r3, r3), tmp3));
            tmp3 = vmulq_f32(tmp3, r3);
#endif

            tmp0 = vaddq_f32(tmp0, magic);
            tmp1 = vaddq_f32(tmp1, magic);
            tmp2 = vaddq_f32(tmp2, magic);
            tmp3 = vaddq_f32(tmp3, magic);

            int32x4_t q0 = vcvtq_s32_f32(tmp0);
            int32x4_t q1 = vcvtq_s32_f32(tmp1);
            int32x4_t q2 = vcvtq_s32_f32(tmp2);
            int32x4_t q3 = vcvtq_s32_f32(tmp3);

            int32x4_t s0 = vreinterpretq_s32_u32(vcltzq_f32(x0));
            int32x4_t s1 = vreinterpretq_s32_u32(vcltzq_f32(x1));
            int32x4_t s2 = vreinterpretq_s32_u32(vcltzq_f32(x2));
            int32x4_t s3 = vreinterpretq_s32_u32(vcltzq_f32(x3));

            q0 = vsubq_s32(veorq_s32(q0, s0), s0);
            q1 = vsubq_s32(veorq_s32(q1, s1), s1);
            q2 = vsubq_s32(veorq_s32(q2, s2), s2);
            q3 = vsubq_s32(veorq_s32(q3, s3), s3);

            vst1q_s32(xi + cnt, q0);
            vst1q_s32(xi + cnt + 4, q1);
            vst1q_s32(xi + cnt + 8, q2);
            vst1q_s32(xi + cnt + 12, q3);
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
