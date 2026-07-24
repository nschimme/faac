/*
 * FAAC - Freeware Advanced Audio Coder
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

#include <assert.h>
#include <math.h>
#include <stdlib.h>

#include "fft.h"
#include "util.h"

/* Radix-4 twiddle factor tables are pre-calculated once in double precision
 * to eliminate cumulative recurrence errors, then stored as single-precision
 * floats to optimize cache-line utilization in wide-vector registers.
 */
static int build_tables_radix4(FFT_Tables *fft_tables, int logm)
{
    if (fft_tables->costbl[logm] == NULL)
    {
        int size = 1 << logm;
        int i;
        fft_tables->costbl[logm] = AllocMemory(size * sizeof(*(fft_tables->costbl[0])));
        fft_tables->negsintbl[logm] = AllocMemory(size * sizeof(*(fft_tables->negsintbl[0])));

        if (!fft_tables->costbl[logm] || !fft_tables->negsintbl[logm])
        {
            if (fft_tables->costbl[logm]) FreeMemory(fft_tables->costbl[logm]);
            if (fft_tables->negsintbl[logm]) FreeMemory(fft_tables->negsintbl[logm]);
            fft_tables->costbl[logm] = fft_tables->negsintbl[logm] = NULL;
            return 0;
        }

        for (i = 0; i < size; i++)
        {
            double theta = 2.0 * M_PI_DOUBLE * (double)i / (double)size;
            fft_tables->costbl[logm][i] = (fftfloat)cos(theta);
            fft_tables->negsintbl[logm][i] = (fftfloat)-sin(theta);
        }
    }
    return 1;
}

/* Bit-reversal tables are pre-calculated to map scrambled DIF FFT outputs
 * back to natural order. This transforms the O(N) random-access swapping pass
 * into an structured lookup, saving memory cycles in the hot path.
 */
static int build_reorder_table(FFT_Tables *fft_tables, int logm)
{
    if (fft_tables->reordertbl[logm] == NULL)
    {
        int size = 1 << logm;
        int i;
        fft_tables->reordertbl[logm] = AllocMemory(size * sizeof(*(fft_tables->reordertbl[0])));
        if (!fft_tables->reordertbl[logm]) return 0;

        for (i = 0; i < size; i++)
        {
            int reversed = 0;
            int b;
            int tmp = i;
            for (b = 0; b < logm; b++)
            {
                reversed = (reversed << 1) | (tmp & 1);
                tmp >>= 1;
            }
            fft_tables->reordertbl[logm][i] = (unsigned short)reversed;
        }
    }
    return 1;
}

int fft_initialize(FFT_Tables *fft_tables)
{
    int i;
    fft_tables->costbl = AllocMemory((FFT_MAXLOGM + 1) * sizeof(fft_tables->costbl[0]));
    fft_tables->negsintbl = AllocMemory((FFT_MAXLOGM + 1) * sizeof(fft_tables->negsintbl[0]));
    fft_tables->reordertbl = AllocMemory((FFT_MAXLOGM + 1) * sizeof(fft_tables->reordertbl[0]));

    if (!fft_tables->costbl || !fft_tables->negsintbl || !fft_tables->reordertbl)
    {
        if (fft_tables->costbl) FreeMemory(fft_tables->costbl);
        if (fft_tables->negsintbl) FreeMemory(fft_tables->negsintbl);
        if (fft_tables->reordertbl) FreeMemory(fft_tables->reordertbl);
        fft_tables->costbl = NULL;
        fft_tables->negsintbl = NULL;
        fft_tables->reordertbl = NULL;
        return 0;
    }

    for (i = 0; i < FFT_MAXLOGM + 1; i++)
    {
        fft_tables->costbl[i] = NULL;
        fft_tables->negsintbl[i] = NULL;
        fft_tables->reordertbl[i] = NULL;
    }

    for (i = 0; i < FFT_MAXLOGM + 1; i++)
    {
        fft_tables->mdct_cos[i] = NULL;
        fft_tables->mdct_sin[i] = NULL;
    }

    /* Precompute MDCT pre/post-twiddles for both block sizes now, so the
       per-frame twiddle loop is a table lookup instead of a cos/sin recurrence. */
    {
        static const int logms[2] = { LOGM_SHORT, LOGM_LONG };
        int t;
        for (t = 0; t < 2; t++)
        {
            int logm = logms[t];
            int size = 1 << logm;
            double freq = 2.0 * M_PI_DOUBLE / (double)(4 << logm);
            fftfloat *c = AllocMemory(size * sizeof(fftfloat));
            fftfloat *s = AllocMemory(size * sizeof(fftfloat));

            if (!c || !s)
            {
                if (c) FreeMemory(c);
                if (s) FreeMemory(s);
                fft_terminate(fft_tables);
                return 0;
            }

            for (i = 0; i < size; i++)
            {
                double theta = freq * ((double)i + 0.125);
                c[i] = (fftfloat)cos(theta);
                s[i] = (fftfloat)sin(theta);
            }
            fft_tables->mdct_cos[logm] = c;
            fft_tables->mdct_sin[logm] = s;
        }
    }

    /* Eagerly initialize Radix-4 tables and reorder tables for both block sizes.
     * This avoids expensive branch checks and lazy-init safety concerns during hot path.
     */
    if (!build_tables_radix4(fft_tables, LOGM_LONG) ||
        !build_tables_radix4(fft_tables, LOGM_SHORT) ||
        !build_reorder_table(fft_tables, LOGM_LONG) ||
        !build_reorder_table(fft_tables, LOGM_SHORT))
    {
        fft_terminate(fft_tables);
        return 0;
    }

    return 1;
}

void fft_terminate(FFT_Tables *fft_tables)
{
    int i;

    for (i = 0; i < FFT_MAXLOGM + 1; i++)
    {
        if (fft_tables->costbl[i] != NULL)
            FreeMemory(fft_tables->costbl[i]);

        if (fft_tables->negsintbl[i] != NULL)
            FreeMemory(fft_tables->negsintbl[i]);

        if (fft_tables->reordertbl[i] != NULL)
            FreeMemory(fft_tables->reordertbl[i]);
    }

    for (i = 0; i < FFT_MAXLOGM + 1; i++)
    {
        if (fft_tables->mdct_cos[i] != NULL)
            FreeMemory(fft_tables->mdct_cos[i]);
        if (fft_tables->mdct_sin[i] != NULL)
            FreeMemory(fft_tables->mdct_sin[i]);
        fft_tables->mdct_cos[i] = NULL;
        fft_tables->mdct_sin[i] = NULL;
    }

    FreeMemory(fft_tables->costbl);
    FreeMemory(fft_tables->negsintbl);
    FreeMemory(fft_tables->reordertbl);

    fft_tables->costbl = NULL;
    fft_tables->negsintbl = NULL;
    fft_tables->reordertbl = NULL;
}


/* Radix-4 DIF. Swapping the 2nd/3rd butterfly outputs yields plain
 * bit-reversed order at the end, avoiding a digit-reversal permutation.
 * logm=9 (512) isn't a power of 4, so it ends with one radix-2 stage.
 */

static inline void radix4_dif_stage(
    float * restrict xr,
    float * restrict xi,
    int n,
    int n1,
    int n2,
    int k,
    const fftfloat * restrict costbl,
    const fftfloat * restrict sintbl)
{
    int i, j;

    for (i = 0; i < n; i += n1)
    {
        float * restrict r1p = xr + i;
        float * restrict r2p = xr + i + n2;
        float * restrict r3p = xr + i + 2*n2;
        float * restrict r4p = xr + i + 3*n2;
        float * restrict i1p = xi + i;
        float * restrict i2p = xi + i + n2;
        float * restrict i3p = xi + i + 2*n2;
        float * restrict i4p = xi + i + 3*n2;

        /* j=0 unrolled: skip the twiddle multiply, it's the identity here */
        {
            float r1 = *r1p, i1 = *i1p;
            float r2 = *r2p, i2 = *i2p;
            float r3 = *r3p, i3 = *i3p;
            float r4 = *r4p, i4 = *i4p;

            float t1 = r1 + r3, t2 = i1 + i3;
            float t3 = r2 + r4, t4 = i2 + i4;
            float t5 = r1 - r3, t6 = i1 - i3;
            float t7 = r2 - r4, t8 = i2 - i4;

            *r1p = t1 + t3; *i1p = t2 + t4;
            *r3p = t5 + t8; *i3p = t6 - t7;
            *r2p = t1 - t3; *i2p = t2 - t4;
            *r4p = t5 - t8; *i4p = t6 + t7;

            r1p++; r2p++; r3p++; r4p++;
            i1p++; i2p++; i3p++; i4p++;
        }

        /* unit-stride pointers, not xr[i+j+...], so the compiler can vectorize this */
        for (j = 1; j < n2; j++)
        {
            int tw_idx = j << (2 * k);
            const float c1 = (float)costbl[tw_idx];
            const float s1 = (float)sintbl[tw_idx];
            const float c2 = (float)costbl[2 * tw_idx];
            const float s2 = (float)sintbl[2 * tw_idx];
            const float c3 = (float)costbl[3 * tw_idx];
            const float s3 = (float)sintbl[3 * tw_idx];

            float r1 = *r1p, i1 = *i1p;
            float r2 = *r2p, i2 = *i2p;
            float r3 = *r3p, i3 = *i3p;
            float r4 = *r4p, i4 = *i4p;

            float t1 = r1 + r3, t2 = i1 + i3;
            float t3 = r2 + r4, t4 = i2 + i4;
            float t5 = r1 - r3, t6 = i1 - i3;
            float t7 = r2 - r4, t8 = i2 - i4;

            *r1p = t1 + t3;
            *i1p = t2 + t4;

            r1 = t1 - t3; i1 = t2 - t4;
            r2 = t5 + t8; i2 = t6 - t7;
            r3 = t5 - t8; i3 = t6 + t7;

            *r3p = r2 * c1 - i2 * s1;
            *i3p = r2 * s1 + i2 * c1;
            *r2p = r1 * c2 - i1 * s2;
            *i2p = r1 * s2 + i1 * c2;
            *r4p = r3 * c3 - i3 * s3;
            *i4p = r3 * s3 + i3 * c3;

            r1p++; r2p++; r3p++; r4p++;
            i1p++; i2p++; i3p++; i4p++;
        }
    }
}

/* odd logm: 4^k can't fill it, one radix-2 stage mops up the remainder */
static inline void radix4_dif_radix2_tail(
    float * restrict xr,
    float * restrict xi,
    int n)
{
    int i;
    float * restrict r1p = xr;
    float * restrict r2p = xr + 1;
    float * restrict i1p = xi;
    float * restrict i2p = xi + 1;
    for (i = 0; i < n; i += 2)
    {
        float r1 = *r1p, i1 = *i1p;
        float r2 = *r2p, i2 = *i2p;
        *r1p = r1 + r2; *i1p = i1 + i2;
        *r2p = r1 - r2; *i2p = i1 - i2;
        r1p += 2; r2p += 2; i1p += 2; i2p += 2;
    }
}

/* Specialized inline helper to allow the compiler to fully constant-fold
 * loop counters, strides, and twiddle shift counts for known FFT sizes.
 */
static inline void radix4_dif_run_specialized(
    float * restrict xr,
    float * restrict xi,
    int logm,
    int k_start,
    const fftfloat * restrict costbl,
    const fftfloat * restrict sintbl)
{
    assert(xr != NULL);
    assert(xi != NULL);
    assert(costbl != NULL);
    assert(sintbl != NULL);

    int n = 1 << logm;
    int k;

    for (k = k_start; k < (logm >> 1); k++)
        radix4_dif_stage(xr, xi, n, n >> (2 * k), n >> (2 * k + 2), k, costbl, sintbl);

    if (logm & 1)
        radix4_dif_radix2_tail(xr, xi, n);
}

void radix4_dif_run(
    float * restrict xr,
    float * restrict xi,
    int logm,
    int k_start,
    const fftfloat * restrict costbl,
    const fftfloat * restrict sintbl)
{
    if (logm == LOGM_SHORT) {
        if (k_start == 1) {
            radix4_dif_run_specialized(xr, xi, LOGM_SHORT, 1, costbl, sintbl);
        } else {
            radix4_dif_run_specialized(xr, xi, LOGM_SHORT, 0, costbl, sintbl);
        }
    } else {
        if (k_start == 1) {
            radix4_dif_run_specialized(xr, xi, LOGM_LONG, 1, costbl, sintbl);
        } else {
            radix4_dif_run_specialized(xr, xi, LOGM_LONG, 0, costbl, sintbl);
        }
    }
}

/* Only called by SBR's QMF analysis, always at the fixed 64-point size, so
 * logm is hardcoded to LOGM_SHORT rather than taken as a parameter: this
 * drops the bounds checks and table-array indexing from the hot path and
 * lets the compiler constant-fold the transform size.
 *
 * Leaves the output in bit-reversed order (no physical permute): callers
 * read natural bin i via reordertbl[i], the same pattern MDCT uses to fuse
 * its post-twiddle unfold with reordering instead of running a separate
 * swap pass.
 */
void fft64(FFT_Tables *fft_tables, float *xr, float *xi)
{
    assert(fft_tables != NULL);
    assert(fft_tables->costbl != NULL);
    assert(fft_tables->negsintbl != NULL);
    assert(fft_tables->costbl[LOGM_SHORT] != NULL);
    assert(fft_tables->negsintbl[LOGM_SHORT] != NULL);

    /* Tables are pre-initialized during FilterBankInit, avoiding hot-path
     * branching and checks. */
    radix4_dif_run_specialized(xr, xi, LOGM_SHORT, 0, fft_tables->costbl[LOGM_SHORT], fft_tables->negsintbl[LOGM_SHORT]);
}
