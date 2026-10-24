/*
 * FAAR - Freeware Advanced Audio Resampler
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
 */
#ifndef FAAR_FFT_FLOAT2_H
#define FAAR_FFT_FLOAT2_H

#include <stddef.h>
#include <stdint.h>
#include <math.h>

/* A normalized high/low float expansion. Arithmetic uses float and fmaf;
 * filter and twiddle setup retain the existing double design calculations.
 * TwoSum and TwoProductFMA: Ogita, Rump, Oishi, SISC 26(6), 2005,
 * doi:10.1137/030601818. Requires ordinary IEEE rounding and no fast-math
 * reassociation. Inputs follow the same finite-audio range as the float FFT.
 */
typedef struct { float hi, lo; } faar_float2;
typedef struct {
    size_t size, twiddle_size;
    int twiddle_owned;
    faar_float2 *work, *twiddle;
} faar_float2_plan;

static inline faar_float2 ff2(float a) { return (faar_float2){a, 0.0f}; }
static inline faar_float2 ff2_setup(double a) {
    float hi = (float)a;
    return (faar_float2){hi, (float)(a - (double)hi)};
}
static inline float ff2_round(faar_float2 a) { return a.hi + a.lo; }
static inline faar_float2 ff2_neg(faar_float2 a) {
    return (faar_float2){-a.hi, -a.lo};
}
static inline faar_float2 ff2_add(faar_float2 a, faar_float2 b) {
    float s = a.hi + b.hi;
    float v = s - a.hi;
    float e = (a.hi - (s - v)) + (b.hi - v);
    e = (e + a.lo) + b.lo;
    float hi = s + e;
    return (faar_float2){hi, e - (hi - s)};
}
static inline faar_float2 ff2_sub(faar_float2 a, faar_float2 b) {
    return ff2_add(a, ff2_neg(b));
}
static inline faar_float2 ff2_mul(faar_float2 a, faar_float2 b) {
    float p = a.hi * b.hi;
    float e = fmaf(a.hi, b.hi, -p);
    e = fmaf(a.hi, b.lo, e);
    e = fmaf(a.lo, b.hi, e);
    float hi = p + e;
    return (faar_float2){hi, e - (hi - p)};
}
static inline faar_float2 ff2_mulf(faar_float2 a, float b) {
    float p = a.hi * b;
    float e = fmaf(a.hi, b, -p);
    e = fmaf(a.lo, b, e);
    float hi = p + e;
    return (faar_float2){hi, e - (hi - p)};
}
static inline faar_float2 ff2_div(faar_float2 a, faar_float2 b) {
    float q = a.hi / b.hi;
    faar_float2 residual = ff2_sub(a, ff2_mulf(b, q));
    return ff2_add(ff2(q), ff2(ff2_round(residual) / b.hi));
}
static inline faar_float2 ff2_uint32(uint32_t n) {
    return ff2_add(ff2((float)(n >> 16) * 65536.0f), ff2((float)(n & 65535u)));
}

/* In-place complex forward FFT: planar real/imaginary, exp(-j), unnormalized.
 * A plan owns its work buffer; init_shared borrows immutable same-size or larger twiddles
 * from an owner that must outlive it. Clear accepts a zero-initialized plan.
 */
int faar_float2_init(faar_float2_plan *p, size_t n);
int faar_float2_init_shared(faar_float2_plan *p, size_t n,
                           const faar_float2_plan *owner);
void faar_float2_clear(faar_float2_plan *p);
void faar_float2_prepare(faar_float2_plan *p);
void faar_float2_forward(const faar_float2_plan *p);

#endif
