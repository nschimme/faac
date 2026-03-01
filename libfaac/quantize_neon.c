#include "quantize.h"

#if defined(__aarch64__) || defined(__arm__)
#include <arm_neon.h>

#define MAGIC_NUMBER 0.4054f

void quantize_sfb_neon(int end, int gsize, faac_real sfacfix, const faac_real *xr, int *xi)
{
    int win, cnt;
    float32x4_t vzero32 = vdupq_n_f32(0.0f);

    for (win = 0; win < gsize; win++)
    {
#ifdef FAAC_PRECISION_SINGLE
        float32x4_t vsfac = vdupq_n_f32((float)sfacfix);
        float32x4_t vmagic = vdupq_n_f32(MAGIC_NUMBER);
        if (end >= 4)
        {
            for (cnt = 0; cnt <= end - 4; cnt += 4)
            {
                float32x4_t x = vld1q_f32(xr + cnt);
                float32x4_t t = vmulq_f32(vabsq_f32(x), vsfac);

#if defined(__aarch64__)
                t = vaddq_f32(vsqrtq_f32(vmulq_f32(t, vsqrtq_f32(t))), vmagic);
#else
                /* Newton-Raphson approximation sequence for ARMv7 */
                float32x4_t r = vrsqrteq_f32(t);
                r = vmulq_f32(r, vrsqrtsq_f32(vmulq_f32(r, r), t));
                float32x4_t sqrt_val = vbslq_f32(vcltq_f32(vzero32, t), vmulq_f32(t, r), vzero32);
                t = vmulq_f32(t, sqrt_val);

                r = vrsqrteq_f32(t);
                r = vmulq_f32(r, vrsqrtsq_f32(vmulq_f32(r, r), t));
                t = vaddq_f32(vbslq_f32(vcltq_f32(vzero32, t), vmulq_f32(t, r), vzero32), vmagic);
#endif
                int32x4_t q = vcvtq_s32_f32(t);
                int32x4_t g = vreinterpretq_s32_u32(vcltq_f32(x, vzero32));
                vst1q_s32(xi + cnt, vsubq_s32(veorq_s32(q, g), g));
            }
        }
        else cnt = 0;
#else
#if defined(__aarch64__)
        float64x2_t vdsfac = vdupq_n_f64(sfacfix);
        float64x2_t vdmagic = vdupq_n_f64((double)MAGIC_NUMBER);
        float64x2_t vzero64 = vdupq_n_f64(0.0);

        if (end >= 2)
        {
            for (cnt = 0; cnt <= end - 2; cnt += 2)
            {
                float64x2_t x = vld1q_f64(xr + cnt);
                float64x2_t t = vmulq_f64(vabsq_f64(x), vdsfac);
                t = vaddq_f64(vsqrtq_f64(vmulq_f64(t, vsqrtq_f64(t))), vdmagic);

                int32x2_t q = vqmovn_s64(vcvtq_s64_f64(t));
                int64x2_t g64 = vreinterpretq_s64_u64(vcltq_f64(x, vzero64));
                int32x2_t g = vqmovn_s64(g64);

                vst1_s32(xi + cnt, vsubq_s32(veorq_s32(q, g), g));
            }
        }
        else cnt = 0;
#else
        cnt = 0;
#endif
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
void quantize_sfb_neon(int end, int gsize, faac_real sfacfix, const faac_real *xr, int *xi) {}
#endif
