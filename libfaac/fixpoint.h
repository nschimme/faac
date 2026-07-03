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

#ifndef FIXPOINT_H
#define FIXPOINT_H

#include <stdint.h>

/* Use Q1.31 for high precision twiddle factors and intermediate results */
typedef int32_t int32_q31;
#define Q31_ONE 2147483647
#define Q31_BIT 31

/* Standard multiplication for Q1.31 */
static inline int32_q31 fix_mul_q31(int32_q31 a, int32_q31 b) {
    return (int32_q31)(((int64_t)a * b) >> Q31_BIT);
}

#define FIX_SIN_LUT_SIZE 1024
extern const int32_q31 fix_sin_lut[FIX_SIN_LUT_SIZE + 1];

/**
 * @brief Fixed-point sine function using LUT and linear interpolation.
 * @param theta Angle in units of (PI/2), scaled by 2^30.
 * @return sin(theta) in Q1.31.
 */
static inline int32_q31 fix_sin(int32_t theta) {
    int32_t idx;
    int32_t frac;
    int32_q31 v0, v1;
    if (theta > (1 << 30)) { theta = (1 << 31) - theta; }
    if (theta < 0) return 0;
    if (theta >= (1 << 30)) return fix_sin_lut[FIX_SIN_LUT_SIZE];
    idx = theta >> (30 - 10);
    frac = theta & ((1 << (30 - 10)) - 1);
    v0 = fix_sin_lut[idx];
    v1 = fix_sin_lut[idx + 1];
    return v0 + (int32_q31)(((int64_t)(v1 - v0) * frac) >> (30 - 10));
}

static inline int32_q31 fix_cos(int32_t theta) {
    return fix_sin((1 << 30) - theta);
}

#endif /* FIXPOINT_H */
