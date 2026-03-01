#include "quantize_internal.h"

#if defined(__aarch64__) || defined(__arm__)
#include <arm_neon.h>

#define MAGIC_NUMBER 0.4054f

void quantize_lines_neon(int end, faac_real sfacfix, const faac_real *xr, int *xi)
{
    int cnt = 0;
#ifdef FAAC_PRECISION_SINGLE
    float32x4_t sfac = vdupq_n_f32((float)sfacfix);
    float32x4_t magic = vdupq_n_f32(MAGIC_NUMBER);

    for (cnt = 0; cnt <= end - 4; cnt += 4)
    {
        float32x4_t x = vld1q_f32(xr + cnt);
        float32x4_t x_abs = vabsq_f32(x);
        float32x4_t tmp = vmulq_f32(x_abs, sfac);

        float32x4_t sqrt_tmp = vsqrtq_f32(tmp);
        tmp = vmulq_f32(tmp, sqrt_tmp);
        tmp = vsqrtq_f32(tmp);

        tmp = vaddq_f32(tmp, magic);

        int32x4_t q = vcvtq_s32_f32(tmp);

        uint32x4_t sign_mask = vcltzq_f32(x);
        q = vbslq_s32(sign_mask, vnegq_s32(q), q);

        vst1q_s32(xi + cnt, q);
    }
#else
    /* Double precision NEON (AArch64) */
#ifdef __aarch64__
    float64x2_t sfac = vdupq_n_f64(sfacfix);
    float64x2_t magic = vdupq_n_f64((double)MAGIC_NUMBER);

    for (cnt = 0; cnt <= end - 2; cnt += 2)
    {
        float64x2_t x = vld1q_f64(xr + cnt);
        float64x2_t x_abs = vabsq_f64(x);
        float64x2_t tmp = vmulq_f64(x_abs, sfac);

        float64x2_t sqrt_tmp = vsqrtq_f64(tmp);
        tmp = vmulq_f64(tmp, sqrt_tmp);
        tmp = vsqrtq_f64(tmp);

        tmp = vaddq_f64(tmp, magic);

        uint64x2_t sign_mask = vcltzq_f64(x);
        int64x2_t q64 = vcvtq_s64_f64(tmp);
        int64x2_t q64_signed = vbslq_s64(sign_mask, vnegq_s64(q64), q64);
        int32x2_t q32 = vqmovn_s64(q64_signed);

        vst1_s32(xi + cnt, q32);
    }
#endif
#endif

    for (; cnt < end; cnt++)
    {
        faac_real tmp = FAAC_FABS(xr[cnt]);
        tmp *= sfacfix;
        tmp = FAAC_SQRT(tmp * FAAC_SQRT(tmp));
        xi[cnt] = (int)(tmp + (faac_real)MAGIC_NUMBER);
        if (xr[cnt] < 0)
            xi[cnt] = -xi[cnt];
    }
}
#else
void quantize_lines_neon(int end, faac_real sfacfix, const faac_real *xr, int *xi) {}
#endif
