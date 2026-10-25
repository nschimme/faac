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
#include "fft_float2.h"
#include "libfaar_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void faar_float2_clear(faar_float2_plan *p) {
    if (!p) return;
    FreeMemory(p->work);
    if (p->twiddle_owned) FreeMemory(p->twiddle);
    memset(p, 0, sizeof(*p));
}

int faar_float2_init_shared(faar_float2_plan *p, size_t n,
                           const faar_float2_plan *owner) {
    if (!p || p == owner || n < 16 || n > (1u << 22) || (n & (n - 1))
            || n > SIZE_MAX / (2 * sizeof(faar_float2))
            || (owner && (owner->twiddle_size < n || owner->twiddle_size % n || !owner->twiddle))) return -1;
    memset(p, 0, sizeof(*p));
    p->size = n;
    p->twiddle_size = owner ? owner->twiddle_size : n;
    p->work = AllocMemory(2 * n * sizeof(*p->work));
    p->twiddle_owned = owner == NULL;
    p->twiddle = owner ? owner->twiddle : AllocMemory(n * sizeof(*p->twiddle));
    if (!p->work || !p->twiddle) { faar_float2_clear(p); return -1; }
    if (!owner) {
        for (size_t k = 0; k < n / 2; ++k) {
            double a = -6.283185307179586476925286766559 * (double)k / (double)n;
            p->twiddle[k] = ff2_setup(cos(a));
            p->twiddle[n / 2 + k] = ff2_setup(sin(a));
        }
    }
    return 0;
}

int faar_float2_init(faar_float2_plan *p, size_t n) {
    return faar_float2_init_shared(p, n, NULL);
}

void faar_float2_prepare(faar_float2_plan *p) {
    if (p && p->work) memset(p->work, 0, 2 * p->size * sizeof(*p->work));
}

/* In-place radix-2 DIF; planar complex input, unnormalized exp(-j). */
void faar_float2_forward(const faar_float2_plan *p) {
    size_t n = p->size;
    faar_float2 *re = p->work, *im = re + n;
    for (size_t len = n; len >= 2; len /= 2) {
        size_t half = len / 2, stride = n / len;
        for (size_t base = 0; base < n; base += len) {
            for (size_t j = 0; j < half; ++j) {
                size_t a = base + j, b = a + half;
                faar_float2 ar = re[a], ai = im[a], br = re[b], bi = im[b];
                faar_float2 dr = ff2_sub(ar, br), di = ff2_sub(ai, bi);
                faar_float2 c = p->twiddle[j * stride * (p->twiddle_size / n)];
                faar_float2 s = p->twiddle[p->twiddle_size / 2 + j * stride * (p->twiddle_size / n)];
                re[a] = ff2_add(ar, br); im[a] = ff2_add(ai, bi);
                re[b] = ff2_sub(ff2_mul(dr, c), ff2_mul(di, s));
                im[b] = ff2_add(ff2_mul(dr, s), ff2_mul(di, c));
            }
        }
    }
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            faar_float2 t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
}
