/*
 * FAAR - Freeware Advanced Audio Resampler
 * Copyright (C) 2002 Krzysztof Nikiel
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

/* Generalized from FAAC 17fbbf8a. */
#ifndef FAAR_FFT_TEMPLATE
#define FAAR_FFT_TEMPLATE
#include "fft.h"
#include "libfaar_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define JOIN_(a, b) a##b
#define JOIN(a, b) JOIN_(a, b)
#define FN(name) JOIN(PREFIX, name)
#define REAL float
#define PLAN faar_fftf_plan
#define PREFIX faar_fftf_
#include "fft.c"
#undef REAL
#undef PLAN
#undef PREFIX
/* Double transforms serve the native-double High kernels. */
#ifdef FAAR_WIDE_HIGH
#define REAL double
#define PLAN faar_fftd_plan
#define PREFIX faar_fftd_
#include "fft.c"
#undef REAL
#undef PLAN
#undef PREFIX
#endif
#else
#define BUTTERFLY_SUMS \
    REAL t1 = r1 + r3, t2 = i1 + i3; \
    REAL t3 = r2 + r4, t4 = i2 + i4; \
    REAL t5 = r1 - r3, t6 = i1 - i3; \
    REAL t7 = r2 - r4, t8 = i2 - i4

/* Padding separates butterfly streams that otherwise alias the same L1 sets.
 * Early stages use 256-element blocks; later stages pad each B-element run. */
#define PAD_BLOCK ((sizeof(REAL) == sizeof(float) ? 1024 : 2048) / (int)sizeof(REAL))
#define PAD_COUNT ((sizeof(REAL) == sizeof(float) ? 128 : 64) / (int)sizeof(REAL))
#define PAD_INDEX(x) ((x) + ((x) / PAD_BLOCK) * PAD_COUNT)
#define IDX(sk, x) ((sk) ? PAD_INDEX(x) : (x))

/* Stage 0: a single sub-transform, so the twiddle changes every butterfly
 * and the four outputs of butterfly j are adjacent at 4j. Its twiddles are
 * stored as six planes below 8192 complex points, for unit-stride loads.
 * Larger transforms use interleaved records to reduce twiddle set aliasing.
 * The identity twiddle is not special-cased: one butterfly in n/4 does not
 * justify another loop body. */
static void FN(radix4_first_stage)(
    const REAL * restrict sr,
    const REAL * restrict si,
    REAL * restrict dr,
    REAL * restrict di,
    int n4,
    const REAL * restrict tw,
    int skout)
{
    const int tp = n4;
    int j;

    for (j = 0; j < n4; j++) {
        const REAL c1 = tw[j],           s1 = tw[tp + j];
        const REAL c2 = tw[2 * tp + j], s2 = tw[3 * tp + j];
        const REAL c3 = tw[4 * tp + j], s3 = tw[5 * tp + j];
        REAL * restrict o = dr + IDX(skout, 4 * j);
        REAL * restrict p = di + IDX(skout, 4 * j);

        REAL r1 = sr[j],          i1 = si[j];
        REAL r2 = sr[j + n4],     i2 = si[j + n4];
        REAL r3 = sr[j + 2 * n4], i3 = si[j + 2 * n4];
        REAL r4 = sr[j + 3 * n4], i4 = si[j + 3 * n4];
        BUTTERFLY_SUMS;

        o[0] = t1 + t3;
        p[0] = t2 + t4;

        r1 = t1 - t3; i1 = t2 - t4;
        r2 = t5 + t8; i2 = t6 - t7;
        r3 = t5 - t8; i3 = t6 + t7;

        o[1] = r2 * c1 - i2 * s1;
        p[1] = r2 * s1 + i2 * c1;
        o[2] = r1 * c2 - i1 * s2;
        p[2] = r1 * s2 + i1 * c2;
        o[3] = r3 * c3 - i3 * s3;
        p[3] = r3 * s3 + i3 * c3;
    }
}

static void FN(radix4_first_interleaved)(
    const REAL * restrict sr,
    const REAL * restrict si,
    REAL * restrict dr,
    REAL * restrict di,
    int n4,
    const REAL * restrict tw,
    int skout)
{
    int j;

    for (j = 0; j < n4; j++) {
        const REAL c1 = tw[6 * j], s1 = tw[6 * j + 1];
        const REAL c2 = tw[6 * j + 2], s2 = tw[6 * j + 3];
        const REAL c3 = tw[6 * j + 4], s3 = tw[6 * j + 5];
        REAL * restrict o = dr + IDX(skout, 4 * j);
        REAL * restrict p = di + IDX(skout, 4 * j);

        REAL r1 = sr[j],          i1 = si[j];
        REAL r2 = sr[j + n4],     i2 = si[j + n4];
        REAL r3 = sr[j + 2 * n4], i3 = si[j + 2 * n4];
        REAL r4 = sr[j + 3 * n4], i4 = si[j + 3 * n4];
        BUTTERFLY_SUMS;

        o[0] = t1 + t3;
        p[0] = t2 + t4;

        r1 = t1 - t3; i1 = t2 - t4;
        r2 = t5 + t8; i2 = t6 - t7;
        r3 = t5 - t8; i3 = t6 + t7;

        o[1] = r2 * c1 - i2 * s1;
        p[1] = r2 * s1 + i2 * c1;
        o[2] = r1 * c2 - i1 * s2;
        p[2] = r1 * s2 + i1 * c2;
        o[3] = r3 * c3 - i3 * s3;
        p[3] = r3 * s3 + i3 * c3;
    }
}

/* B butterflies sharing one twiddle: element c of each of the four input
 * quarters to element c of each of the four output runs. A restrict pointer
 * per output run tells the vectorizer the runs can't overlap. */
static inline void FN(radix4_butterflies)(
    const REAL * restrict sr, const REAL * restrict si, int n4,
    REAL * restrict o1r, REAL * restrict o1i,
    REAL * restrict o2r, REAL * restrict o2i,
    REAL * restrict o3r, REAL * restrict o3i,
    REAL * restrict o4r, REAL * restrict o4i,
    int B,
    const REAL * restrict tw)
{
    const REAL c1 = tw[0], s1 = tw[1];
    const REAL c2 = tw[2], s2 = tw[3];
    const REAL c3 = tw[4], s3 = tw[5];
    int c;

    for (c = 0; c < B; c++) {
        REAL r1 = sr[c],          i1 = si[c];
        REAL r2 = sr[c + n4],     i2 = si[c + n4];
        REAL r3 = sr[c + 2 * n4], i3 = si[c + 2 * n4];
        REAL r4 = sr[c + 3 * n4], i4 = si[c + 3 * n4];
        BUTTERFLY_SUMS;

        o1r[c] = t1 + t3;
        o1i[c] = t2 + t4;

        r1 = t1 - t3; i1 = t2 - t4;
        r2 = t5 + t8; i2 = t6 - t7;
        r3 = t5 - t8; i3 = t6 + t7;

        o2r[c] = r2 * c1 - i2 * s1;
        o2i[c] = r2 * s1 + i2 * c1;
        o3r[c] = r1 * c2 - i1 * s2;
        o3i[c] = r1 * s2 + i1 * c2;
        o4r[c] = r3 * c3 - i3 * s3;
        o4i[c] = r3 * s3 + i3 * c3;
    }
}

/* Later stages: B sub-transforms share each twiddle, so the inner loop is
 * unit-stride over c. Early stages use fixed padding blocks; quarters and
 * output runs are whole blocks or fit entirely between padding gaps. */
static void FN(radix4_stage)(
    const REAL * restrict sr,
    const REAL * restrict si,
    REAL * restrict dr,
    REAL * restrict di,
    int n4,
    int B,
    const REAL * restrict tw,
    int skin,
    int skout)
{
    const int L = (skin || skout) && B > PAD_BLOCK ? PAD_BLOCK : B;
    const int qs = IDX(skin, n4);
    const int n2 = n4 / B;
    int j, c0;

    for (j = 0; j < n2; j++, tw += 6) {
        for (c0 = 0; c0 < B; c0 += L) {
            const int a = IDX(skin, j * B + c0);
            REAL *o1r = dr + IDX(skout, 4 * B * j + c0);
            REAL *o1i = di + IDX(skout, 4 * B * j + c0);
            REAL *o2r = dr + IDX(skout, 4 * B * j + B + c0);
            REAL *o2i = di + IDX(skout, 4 * B * j + B + c0);
            REAL *o3r = dr + IDX(skout, 4 * B * j + 2 * B + c0);
            REAL *o3i = di + IDX(skout, 4 * B * j + 2 * B + c0);
            REAL *o4r = dr + IDX(skout, 4 * B * j + 3 * B + c0);
            REAL *o4i = di + IDX(skout, 4 * B * j + 3 * B + c0);
            FN(radix4_butterflies)(sr + a, si + a, qs, o1r, o1i, o2r, o2i,
                               o3r, o3i, o4r, o4i, L, tw);
        }
    }
}

/* Read the preceding stage's padded runs without crossing a gap. The final
 * stage writes directly to the caller-visible, unpadded buffer. */
static void FN(padded_stage)(const REAL * restrict sr, const REAL * restrict si,
    REAL * restrict dr, REAL * restrict di, int n4, int B,
    const REAL * restrict tw, int last)
{
    const int L = B / 4 < PAD_BLOCK ? PAD_BLOCK : B / 4;
    const int pieces = B / L;
    const int qs = n4 + (n4 / L) * PAD_COUNT;
    const int ds = last ? B : B + PAD_COUNT;
    int j, c;

    for (j = 0; j < n4 / B; j++, tw += 6) {
        for (c = 0; c < pieces; c++) {
            int a = j * (B + pieces * PAD_COUNT) + c * (L + PAD_COUNT);
            int o = j * 4 * ds + c * L;
            FN(radix4_butterflies)(sr + a, si + a, qs,
                dr + o, di + o, dr + o + ds, di + o + ds,
                dr + o + 2 * ds, di + o + 2 * ds, dr + o + 3 * ds, di + o + 3 * ds, L, tw);
        }
    }
}

/* odd logm: 4^k can't fill it, one radix-2 stage mops up the remainder */
static void FN(radix2_stage)(
    const REAL * restrict sr,
    const REAL * restrict si,
    REAL * restrict dr,
    REAL * restrict di,
    int n2,
    int skin)
{
    const int L = skin && n2 > PAD_BLOCK ? PAD_BLOCK : n2;
    int c, c0;

    for (c0 = 0; c0 < n2; c0 += L) {
        const REAL * restrict r1p = sr + IDX(skin, c0);
        const REAL * restrict i1p = si + IDX(skin, c0);
        const REAL * restrict r2p = sr + IDX(skin, n2 + c0);
        const REAL * restrict i2p = si + IDX(skin, n2 + c0);
        REAL * restrict o1r = dr + c0, * restrict o1i = di + c0;
        REAL * restrict o2r = dr + n2 + c0, * restrict o2i = di + n2 + c0;

        for (c = 0; c < L; c++) {
            REAL r1 = r1p[c], i1 = i1p[c];
            REAL r2 = r2p[c], i2 = i2p[c];
            o1r[c] = r1 + r2; o1i[c] = i1 + i2;
            o2r[c] = r1 - r2; o2i[c] = i1 - i2;
        }
    }
}

/* Separate the real and imaginary planes to avoid L1 set aliasing. */
#define PAD_IM(n) (PAD_INDEX(n) + 3 * PAD_COUNT)

/* Small transforms ping-pong in the public buffers. Larger transforms use
 * padded scratch and return to the input buffer in natural order. */
static REAL *FN(core)(const PLAN *plan, REAL *in)
{
    int n = plan->size / 2, n4 = n / 4, B = 4;
    const int padded = n >= (sizeof(REAL) == sizeof(float) ? 4096 : 2048);
    const int im = PAD_IM(n);
    REAL *sa = plan->scratch;
    REAL *sb = sa + 2 * im;
    REAL *src, *dst;
    const REAL *tw = plan->twiddle;

    if (padded) {
        if (n4 >= 2048)
            FN(radix4_first_interleaved)(in, in + n, sa, sa + im, n4, tw, 1);
        else
            FN(radix4_first_stage)(in, in + n, sa, sa + im, n4, tw, 1);
        src = sa; dst = sb;
    } else {
        src = in == plan->work ? in + 2 * n : plan->work;
        dst = in;
        FN(radix4_first_stage)(dst, dst + n, src, src + n, n4, tw, 0);
    }
    tw += 6 * n4;
    while (B <= n4) {
        REAL *tmp;
        if (padded && B * 4 == n) {
            FN(padded_stage)(src, src + im, in, in + n, n4, B, tw, 1);
            return in;
        }
        if (!padded)
            FN(radix4_stage)(src, src + n, dst, dst + n, n4, B, tw, 0, 0);
        else if (B < PAD_BLOCK)
            FN(radix4_stage)(src, src + im, dst, dst + im, n4, B, tw, 1, 1);
        else
            FN(padded_stage)(src, src + im, dst, dst + im, n4, B, tw, 0);
        tw += 6 * (n4 / B);
        B *= 4;
        tmp = src; src = dst; dst = tmp;
    }
    if (B < n) {
        if (padded) {
            int ib = B / 4 > PAD_BLOCK ? B / 4 : PAD_BLOCK;
            int q = n / 2 + (n / 2 / ib) * PAD_COUNT;
            int c;
            for (c = 0; c < n / 2; c += ib) {
                int a = c + (c / ib) * PAD_COUNT;
                int k;
                const REAL * restrict r1 = src + a, * restrict i1 = src + im + a;
                const REAL * restrict r2 = r1 + q, * restrict i2 = i1 + q;
                REAL * restrict o1 = in + c, * restrict p1 = in + n + c;
                REAL * restrict o2 = o1 + n / 2, * restrict p2 = p1 + n / 2;
                for (k = 0; k < ib; k++) {
                    o1[k] = r1[k] + r2[k]; p1[k] = i1[k] + i2[k];
                    o2[k] = r1[k] - r2[k]; p2[k] = i1[k] - i2[k];
                }
            }
            return in;
        }
        FN(radix2_stage)(src, src + n, dst, dst + n, n / 2, 0);
        src = dst;
    }
    return src;
}

/* Each plan owns its twiddles; plans that share a filter borrow the owner's. */
static void FN(build_twiddle)(REAL *tw, int m)
{
    int n2, j, b;
    for (n2 = m / 4; n2 >= 1; n2 >>= 2) {
        for (j = 0; j < n2; j++) {
            for (b = 1; b <= 3; b++) {
                double a = 6.283185307179586476925286766559 * b * j / (4 * n2);
                REAL *t = n2 == m / 4 && n2 < 2048 ? tw + 2 * (b - 1) * n2 + j : tw + 6 * j + 2 * (b - 1);
                t[0] = (REAL)cos(a);
                t[n2 == m / 4 && n2 < 2048 ? n2 : 1] = (REAL)-sin(a);
            }
        }
        tw += 6 * n2;
    }
}

void FN(clear)(PLAN *plan)
{
    if (!plan) return;
    if (plan->twiddle_owned) FreeMemory(plan->twiddle);
    FreeMemory(plan->work);
    memset(plan, 0, sizeof(*plan));
}

void FN(prepare)(PLAN *plan, int both_buffers)
{
    if (!plan || !plan->work) return;
    int m = plan->size / 2;
    int padded = m >= (sizeof(REAL) == sizeof(float) ? 4096 : 2048);
    /* Padded transforms return in their input buffer; a separate inverse
     * plan never needs buffer 1. Small transforms ping-pong through both. */
    size_t count = (both_buffers || !padded ? 4 : 2) * (size_t)m;
    size_t work_count = plan->work_buffers * (size_t)plan->size;
    if (count > work_count) count = work_count;
    memset(plan->work, 0, count * sizeof(REAL));
    if (padded && plan->scratch == plan->work + work_count)
        memset(plan->scratch, 0, 4 * (size_t)PAD_IM(m) * sizeof(REAL));
}

/* m complex points. */
static int FN(setup)(PLAN *plan, int m, REAL *scratch, int single,
                     const PLAN *filter_owner)
{
    int j;

    plan->size = 2 * m;
    for (j = m; j > 1; j >>= 1) plan->logm++;
    plan->twiddle = filter_owner ? filter_owner->twiddle : NULL;
    plan->twiddle_owned = !filter_owner;
    if (plan->twiddle_owned) {
        plan->twiddle = AllocMemory(2 * m * sizeof(REAL));
        if (plan->twiddle) FN(build_twiddle)(plan->twiddle, m);
    }
    int padded = m >= (sizeof(REAL) == sizeof(float) ? 4096 : 2048);
    plan->work_buffers = single && padded ? 1 : 2;
    size_t work_count = plan->work_buffers * (size_t)plan->size;
    /* Small transforms do not use scratch. Tiny plans retain it to keep
     * the core's second scratch pointer within the allocation. */
    int separate_scratch = padded || 2 * (size_t)PAD_IM(m) > work_count;
    size_t scratch_count = separate_scratch && !scratch ? 4 * (size_t)PAD_IM(m) : 0;
    plan->work = AllocMemory((work_count + scratch_count) * sizeof(REAL));
    plan->scratch = scratch ? scratch : plan->work
        ? plan->work + (separate_scratch ? work_count : 0) : NULL;
    if (!plan->twiddle || !plan->work) {
        FN(clear)(plan);
        return -1;
    }
    return 0;
}

int FN(cinit)(PLAN *plan, int n)
{
    if (!plan) return -1;
    memset(plan, 0, sizeof(*plan));
    if (n < 16 || n > (1 << 22) || (n & (n - 1))) return -1;
    return FN(setup)(plan, n, NULL, 0, NULL);
}

/* The owner must outlive this plan; their executions must be serialized. */
int FN(cinit_shared)(PLAN *plan, int n, const PLAN *owner)
{
    if (!plan || !owner || n < 16 || n > owner->size / 2
            || (n & (n - 1))) return -1;
    memset(plan, 0, sizeof(*plan));
    return FN(setup)(plan, n, owner->scratch, 0, NULL);
}

/* Different-sized FIR plans consume the forward spectrum in a separate plan,
 * so a padded borrower never needs a second complex buffer. */
int FN(cinit_shared_single)(PLAN *plan, int n, const PLAN *owner)
{
    if (!plan || !owner || n < 16 || n >= owner->size / 2
            || (n & (n - 1))) return -1;
    memset(plan, 0, sizeof(*plan));
    return FN(setup)(plan, n, owner->scratch, 1, NULL);
}

/* Borrow an immutable same-size filter plan's twiddles, with private work.
 * Only workspace_owner belongs to this stream; filter_owner may be a shared
 * prototype with its mutable workspace already released. */
int FN(cinit_filter_shared)(PLAN *plan, int n, const PLAN *filter_owner,
                           const PLAN *workspace_owner, int single)
{
    if (!plan || !filter_owner || plan == filter_owner || plan == workspace_owner
            || n < 16 || n > (1 << 22) || (n & (n - 1))
            || filter_owner->size / 2 != n || !filter_owner->twiddle
            || (workspace_owner && (n > workspace_owner->size / 2
                                     || !workspace_owner->scratch))
            || (single && (!workspace_owner || n >= workspace_owner->size / 2)))
        return -1;
    memset(plan, 0, sizeof(*plan));
    return FN(setup)(plan, n,
            workspace_owner ? workspace_owner->scratch : NULL, single, filter_owner);
}

REAL *FN(cbuffer)(const PLAN *plan, int which)
{
    return plan->work + (which ? plan->size : 0);
}

REAL *FN(cforward)(const PLAN *plan, REAL *in)
{
    return FN(core)(plan, in);
}

#undef PAD_BLOCK
#undef PAD_COUNT
#undef PAD_INDEX
#undef PAD_IM
#undef IDX
#undef BUTTERFLY_SUMS
#endif
