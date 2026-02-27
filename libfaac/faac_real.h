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

#ifndef FAAC_REAL_H
#define FAAC_REAL_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <stdint.h>

#ifdef FAAC_PRECISION_SINGLE
typedef float faac_real;
#define FAAC_SIN sinf
#define FAAC_COS cosf
#define FAAC_SQRT sqrtf
#define FAAC_FABS fabsf
#define FAAC_LOG10 log10f
#define FAAC_POW powf
#define FAAC_ASIN asinf
#define FAAC_LRINT lrintf
#define FAAC_FLOOR floorf
#else
typedef double faac_real;
#define FAAC_SIN sin
#define FAAC_COS cos
#define FAAC_SQRT sqrt
#define FAAC_FABS fabs
#define FAAC_LOG10 log10
#define FAAC_POW pow
#define FAAC_ASIN asin
#define FAAC_LRINT lrint
#define FAAC_FLOOR floor
#endif

#ifdef USE_FAST_MATH
#undef FAAC_LOG10
#undef FAAC_POW

static inline float fast_log2(float x)
{
    union { float f; uint32_t i; } vx = { x };
    float y = (float)vx.i;
    y *= 1.1920928955078125e-7f;
    return y - 126.94269504f;
}

static inline float fast_log10(float x)
{
    return fast_log2(x) * 0.3010299956639812f;
}

static inline float fast_pow2(float x)
{
    float offset = (x < 0) ? 1.0f : 0.0f;
    union { uint32_t i; float f; } v = { (uint32_t)((x + 121.2740575f + offset) * 8388608.0f) };
    return v.f;
}

static inline float fast_pow(float x, float y)
{
    return fast_pow2(y * fast_log2(x));
}

#define FAAC_LOG10 fast_log10
#define FAAC_POW fast_pow
#endif

#endif /* FAAC_REAL_H */
