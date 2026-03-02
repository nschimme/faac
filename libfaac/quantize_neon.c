/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <arm_neon.h>
#include <math.h>
#include "coder.h"

#define MAGIC_NUMBER_REAL ((faac_real)0.4054)

#if !defined(__aarch64__)
/* Helper for ARMv7 to compute sqrt using reciprocal square root sequence */
static inline float32x4_t vsqrtq_f32_v7(float32x4_t x)
{
    float32x4_t r = vrsqrteq_f32(x);
    /* One iteration of Newton-Raphson */
    r = vmulq_f32(r, vrsqrtsq_f32(vmulq_f32(x, r), r));
    /* x * 1/sqrt(x) = sqrt(x) */
    float32x4_t res = vmulq_f32(x, r);
    /* Return 0 for zero or negative input to avoid NaN from 0 * inf */
    return vbslq_f32(vcleq_f32(x, vdupq_n_f32(0.0f)), vdupq_n_f32(0.0f), res);
}
#endif

void quantize_neon(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix)
{
    int cnt = 0;

#ifdef FAAC_PRECISION_DOUBLE
#ifdef __aarch64__
    const float64x2_t sfac = vdupq_n_f64(sfacfix);
    const float64x2_t magic = vdupq_n_f64(MAGIC_NUMBER_REAL);
    const float64x2_t zero = vdupq_n_f64(0.0);

    // 8-wide loop to hide sqrt latency
    if (n >= 8)
    {
        for (; cnt <= n - 8; cnt += 8)
        {
            float64x2_t x0 = vld1q_f64(xr + cnt);
            float64x2_t x1 = vld1q_f64(xr + cnt + 2);
            float64x2_t x2 = vld1q_f64(xr + cnt + 4);
            float64x2_t x3 = vld1q_f64(xr + cnt + 6);

            uint64x2_t s0 = vcltq_f64(x0, zero);
            uint64x2_t s1 = vcltq_f64(x1, zero);
            uint64x2_t s2 = vcltq_f64(x2, zero);
            uint64x2_t s3 = vcltq_f64(x3, zero);

            float64x2_t t0 = vmulq_f64(vabsq_f64(x0), sfac);
            float64x2_t t1 = vmulq_f64(vabsq_f64(x1), sfac);
            float64x2_t t2 = vmulq_f64(vabsq_f64(x2), sfac);
            float64x2_t t3 = vmulq_f64(vabsq_f64(x3), sfac);

            t0 = vsqrtq_f64(vmulq_f64(t0, vsqrtq_f64(t0)));
            t1 = vsqrtq_f64(vmulq_f64(t1, vsqrtq_f64(t1)));
            t2 = vsqrtq_f64(vmulq_f64(t2, vsqrtq_f64(t2)));
            t3 = vsqrtq_f64(vmulq_f64(t3, vsqrtq_f64(t3)));

            int32x2_t q0 = vmovn_s64(vcvtq_s64_f64(vaddq_f64(t0, magic)));
            int32x2_t q1 = vmovn_s64(vcvtq_s64_f64(vaddq_f64(t1, magic)));
            int32x2_t q2 = vmovn_s64(vcvtq_s64_f64(vaddq_f64(t2, magic)));
            int32x2_t q3 = vmovn_s64(vcvtq_s64_f64(vaddq_f64(t3, magic)));

            int32x2_t m0 = vmovn_s64(vreinterpretq_s64_u64(s0));
            int32x2_t m1 = vmovn_s64(vreinterpretq_s64_u64(s1));
            int32x2_t m2 = vmovn_s64(vreinterpretq_s64_u64(s2));
            int32x2_t m3 = vmovn_s64(vreinterpretq_s64_u64(s3));

            vst1_s32(xi + cnt,     vsub_s32(veor_s32(q0, m0), m0));
            vst1_s32(xi + cnt + 2, vsub_s32(veor_s32(q1, m1), m1));
            vst1_s32(xi + cnt + 4, vsub_s32(veor_s32(q2, m2), m2));
            vst1_s32(xi + cnt + 6, vsub_s32(veor_s32(q3, m3), m3));
        }
    }
#endif
#else
    const float32x4_t sfac = vdupq_n_f32((float)sfacfix);
    const float32x4_t magic = vdupq_n_f32((float)MAGIC_NUMBER_REAL);
    const float32x4_t zero = vdupq_n_f32(0.0f);

    // 16-wide loop to hide sqrt latency
    if (n >= 16)
    {
        for (; cnt <= n - 16; cnt += 16)
        {
            float32x4_t x0 = vld1q_f32(xr + cnt);
            float32x4_t x1 = vld1q_f32(xr + cnt + 4);
            float32x4_t x2 = vld1q_f32(xr + cnt + 8);
            float32x4_t x3 = vld1q_f32(xr + cnt + 12);

            uint32x4_t s0 = vcltq_f32(x0, zero);
            uint32x4_t s1 = vcltq_f32(x1, zero);
            uint32x4_t s2 = vcltq_f32(x2, zero);
            uint32x4_t s3 = vcltq_f32(x3, zero);

            float32x4_t t0 = vmulq_f32(vabsq_f32(x0), sfac);
            float32x4_t t1 = vmulq_f32(vabsq_f32(x1), sfac);
            float32x4_t t2 = vmulq_f32(vabsq_f32(x2), sfac);
            float32x4_t t3 = vmulq_f32(vabsq_f32(x3), sfac);

#if defined(__aarch64__)
            t0 = vsqrtq_f32(vmulq_f32(t0, vsqrtq_f32(t0)));
            t1 = vsqrtq_f32(vmulq_f32(t1, vsqrtq_f32(t1)));
            t2 = vsqrtq_f32(vmulq_f32(t2, vsqrtq_f32(t2)));
            t3 = vsqrtq_f32(vmulq_f32(t3, vsqrtq_f32(t3)));
#else
            t0 = vsqrtq_f32_v7(vmulq_f32(t0, vsqrtq_f32_v7(t0)));
            t1 = vsqrtq_f32_v7(vmulq_f32(t1, vsqrtq_f32_v7(t1)));
            t2 = vsqrtq_f32_v7(vmulq_f32(t2, vsqrtq_f32_v7(t2)));
            t3 = vsqrtq_f32_v7(vmulq_f32(t3, vsqrtq_f32_v7(t3)));
#endif

            int32x4_t q0 = vcvtq_s32_f32(vaddq_f32(t0, magic));
            int32x4_t q1 = vcvtq_s32_f32(vaddq_f32(t1, magic));
            int32x4_t q2 = vcvtq_s32_f32(vaddq_f32(t2, magic));
            int32x4_t q3 = vcvtq_s32_f32(vaddq_f32(t3, magic));

            int32x4_t m0 = vreinterpretq_s32_u32(s0);
            int32x4_t m1 = vreinterpretq_s32_u32(s1);
            int32x4_t m2 = vreinterpretq_s32_u32(s2);
            int32x4_t m3 = vreinterpretq_s32_u32(s3);

            vst1q_s32(xi + cnt,      vsubq_s32(veorq_s32(q0, m0), m0));
            vst1q_s32(xi + cnt + 4,  vsubq_s32(veorq_s32(q1, m1), m1));
            vst1q_s32(xi + cnt + 8,  vsubq_s32(veorq_s32(q2, m2), m2));
            vst1q_s32(xi + cnt + 12, vsubq_s32(veorq_s32(q3, m3), m3));
        }
    }
#endif

    // Scalar remainder
    for (; cnt < n; cnt++)
    {
        faac_real val = xr[cnt];
        faac_real tmp = FAAC_FABS(val);
        tmp *= sfacfix;
        tmp = FAAC_SQRT(tmp * FAAC_SQRT(tmp));
        int q = (int)(tmp + MAGIC_NUMBER_REAL);
        xi[cnt] = (val < 0) ? -q : q;
    }
}
