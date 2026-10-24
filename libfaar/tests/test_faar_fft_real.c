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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 */
/* Independent long-double DFT verification for the FAAC-derived real FFT. */
#include "fft_real.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static void reference(const double *x, long double *ref, int n)
{
    int k, j;
    const long double tau = 6.283185307179586476925286766559005768L;

    /* Direct DFT, with a long-double oscillator to avoid O(N^2) trig calls. */
    for (k = 0; k <= n / 2; k++) {
        long double c = cosl(tau * k / n), s = sinl(tau * k / n);
        long double wr = 1, wi = 0, re = 0, im = 0;
        for (j = 0; j < n; j++) {
            long double next = wr * c - wi * s;
            re += x[j] * wr;
            im += x[j] * wi;
            wi = wr * s + wi * c;
            wr = next;
        }
        if (!k) ref[0] = re;
        else if (k == n / 2) ref[1] = re;
        else {
            ref[2 * k] = re;
            ref[2 * k + 1] = im;
        }
    }
}

#define CHECK_TYPE(T, P, INIT, EXEC, CLEAR, LABEL, TOL) do { \
    P plan = {0}; \
    T *a = malloc(n * sizeof(T)), *b = malloc(n * sizeof(T)); \
    T *sum = malloc(n * sizeof(T)); \
    long double peak = 0, energy = 0, error = 0, worst = 0; \
    double roundtrip = 0, linear = 0, scale = 0; \
    if (!a || !b || !sum || INIT(&plan, n)) exit(2); \
    for (j = 0; j < n; j++) a[j] = (T)x[j]; \
    EXEC(&plan, a, 0); \
    if (n <= 8192) { \
        for (j = 0; j < n; j++) { \
            long double e = fabsl((long double)a[j] - ref[j]); \
            if (!isfinite(a[j])) failed = 1; \
            if (e > worst) worst = e; \
            if (fabsl(ref[j]) > peak) peak = fabsl(ref[j]); \
            energy += ref[j] * ref[j]; error += e * e; \
        } \
        printf("%s N=%d max_rel=%.9Le rms_rel=%.9Le", LABEL, n, \
               worst / peak, sqrtl(error / energy)); \
        if (worst / peak > TOL || sqrtl(error / energy) > TOL) failed = 1; \
    } else { \
        printf("%s N=%d direct DFT skipped;", LABEL, n); \
        for (j = 0; j < n; j++) if (!isfinite(a[j])) failed = 1; \
    } \
    EXEC(&plan, a, 1); \
    for (j = 0; j < n; j++) { \
        double e = fabs(a[j] - x[j]); \
        if (!isfinite(a[j])) failed = 1; \
        if (e > roundtrip) roundtrip = e; \
        a[j] = (T)x[j]; b[j] = (T)x[(j * 13 + 7) % n]; \
        sum[j] = a[j] + b[j]; \
    } \
    EXEC(&plan, a, 0); EXEC(&plan, b, 0); EXEC(&plan, sum, 0); \
    for (j = 0; j < n; j++) { \
        double e = fabs((double)sum[j] - ((double)a[j] + b[j])); \
        if (!isfinite(sum[j])) failed = 1; \
        if (e > linear) linear = e; \
        if (fabs(sum[j]) > scale) scale = fabs(sum[j]); \
    } \
    printf(" roundtrip=%.9g linear_rel=%.9g\n", roundtrip, linear / scale); \
    if (roundtrip > 8 * TOL || linear / scale > 8 * TOL) failed = 1; \
    /* DC, Nyquist, and shifted impulse cover real-spectrum endpoints. */ \
    for (int kind = 0; kind < 3; kind++) { \
        for (j = 0; j < n; j++) \
            a[j] = kind == 0 ? 1 : kind == 1 ? (j & 1 ? -1 : 1) : j == 7; \
        EXEC(&plan, a, 0); \
        if (kind < 2) { \
            for (j = 0; j < n; j++) \
                if (fabs(a[j] - (j == kind ? n : 0)) > n * TOL) failed = 1; \
        } \
        EXEC(&plan, a, 1); \
        for (j = 0; j < n; j++) { \
            double expected = kind == 0 ? 1 : kind == 1 ? (j & 1 ? -1 : 1) : j == 7; \
            if (!isfinite(a[j]) || fabs(a[j] - expected) > 8 * TOL) failed = 1; \
        } \
    } \
    CLEAR(&plan); CLEAR(&plan); free(a); free(b); free(sum); \
} while (0)

/* Complex transform against a direct DFT, plus the conjugation round trip
 * the resampler uses as its inverse. */
#define CHECK_COMPLEX(T, P, INIT, SINGLE, PREPARE, BUF, FWD, CLEAR, LABEL, TOL) do { \
    P plan = {0}; \
    long double err = 0, energy = 0; \
    double trip = 0; \
    T *w0, *w1, *out; \
    if (INIT(&plan, n)) exit(2); \
    w0 = BUF(&plan, 0); w1 = BUF(&plan, 1); \
    for (j = 0; j < n; j++) { w0[j] = (T)xr[j]; w0[n + j] = (T)xi[j]; } \
    out = FWD(&plan, w0); \
    for (j = 0; j < n; j++) { \
        long double dr = out[j] - rr[j], di = out[n + j] - ri[j]; \
        err += dr * dr + di * di; energy += rr[j] * rr[j] + ri[j] * ri[j]; \
    } \
    { P owner = {0}, single = {0}; \
      if (INIT(&owner, 2 * n) || SINGLE(&single, n, &owner)) exit(2); \
      PREPARE(&single, 1); \
      T *input = BUF(&single, 0); \
      for (j = 0; j < n; j++) { input[j] = (T)xr[j]; input[n + j] = (T)xi[j]; } \
      T *actual = FWD(&single, input); \
      if (memcmp(actual, out, 2 * (size_t)n * sizeof(T))) failed = 1; \
      CLEAR(&single); CLEAR(&owner); } \
    /* Inverse: conjugate, forward into the other buffer, conjugate, scale. */ \
    { T *dst = out == w0 ? w1 : w0; \
      for (j = 0; j < n; j++) { dst[j] = out[j]; dst[n + j] = -out[n + j]; } \
      out = FWD(&plan, dst); \
      for (j = 0; j < n; j++) { \
          double a = fabs(out[j] / n - xr[j]), b = fabs(-out[n + j] / n - xi[j]); \
          if (a > trip) trip = a; \
          if (b > trip) trip = b; \
      } } \
    printf("%s complex N=%d rms_rel=%.9Le roundtrip=%.9g\n", LABEL, n, \
           sqrtl(err / energy), trip); \
    if (!(sqrtl(err / energy) < TOL) || !(trip < 8 * TOL)) failed = 1; \
    CLEAR(&plan); \
} while (0)

static int check_complex(int selected)
{
    const int sizes[] = {16, 64, 512, 2048, 4096, 8192};
    int failed = 0, t, j, k;
    uint32_t state = 7;
    const long double tau = 6.283185307179586476925286766559005768L;
    faar_rfftf_plan invalid = {0};

    if (!faar_rfftf_cinit(&invalid, 8) || !faar_rfftf_cinit(&invalid, 48)
            || !faar_rfftf_cinit(&invalid, 1 << 23)) return 1;
    for (t = 0; t < 6; t++) {
        if (selected >= 0 && t != selected) continue;
        int n = sizes[t];
        double *xr = malloc(n * sizeof(double)), *xi = malloc(n * sizeof(double));
        long double *rr = malloc(n * sizeof(long double));
        long double *ri = malloc(n * sizeof(long double));
        if (!xr || !xi || !rr || !ri) exit(2);
        for (j = 0; j < n; j++) {
            state = state * 1664525u + 1013904223u;
            xr[j] = ((int)(state >> 16) - 32768) / 32768.0;
            state = state * 1664525u + 1013904223u;
            xi[j] = ((int)(state >> 16) - 32768) / 32768.0;
        }
        for (k = 0; k < n; k++) {
            long double c = cosl(tau * k / n), s = -sinl(tau * k / n);
            long double wr = 1, wi = 0, re = 0, im = 0;
            for (j = 0; j < n; j++) {
                long double next = wr * c - wi * s;
                re += xr[j] * wr - xi[j] * wi;
                im += xr[j] * wi + xi[j] * wr;
                wi = wr * s + wi * c;
                wr = next;
            }
            rr[k] = re; ri[k] = im;
        }
        CHECK_COMPLEX(float, faar_rfftf_plan, faar_rfftf_cinit,
                      faar_rfftf_cinit_shared_single, faar_rfftf_prepare,
                      faar_rfftf_cbuffer, faar_rfftf_cforward,
                      faar_rfftf_clear, "float", 2e-6);
        CHECK_COMPLEX(double, faar_rfftd_plan, faar_rfftd_cinit,
                      faar_rfftd_cinit_shared_single, faar_rfftd_prepare,
                      faar_rfftd_cbuffer, faar_rfftd_cforward,
                      faar_rfftd_clear, "double", 2e-12);
        free(xr); free(xi); free(rr); free(ri);
    }
    return failed;
}

int main(int argc, char **argv)
{
    const int sizes[] = {256, 1024, 8192, 65536};
    int t, j, failed = 0;
    uint32_t state = 1;
    faar_rfftf_plan invalid = {0};

    if (argc != 3 || (strcmp(argv[1], "real") && strcmp(argv[1], "complex")))
        return 2;
    t = atoi(argv[2]);
    if (strcmp(argv[1], "complex") == 0) {
        const int complex_sizes[] = {16, 64, 512, 2048, 4096, 8192};
        int i;
        for (i = 0; i < 6 && complex_sizes[i] != t; i++) {
            /* Find the selected transform size. */
        }
        if (i == 6) return 2;
        return check_complex(i);
    }
    if (t != 256 && t != 1024 && t != 8192 && t != 65536) return 2;
    if (!faar_rfftf_init(&invalid, 0) || !faar_rfftf_init(&invalid, 257)
            || !faar_rfftf_init(&invalid, 131072)) return 1;
    for (t = 0; t < 4; t++) {
        if (sizes[t] != atoi(argv[2])) continue;
        int n = sizes[t];
        double *x = malloc(n * sizeof(double));
        long double *ref = malloc(n * sizeof(long double));
        if (!x || !ref) return 2;
        for (j = 0; j < n; j++) {
            state = state * 1664525u + 1013904223u;
            /* Dyadic inputs are identical in both precisions. */
            x[j] = ((int)(state >> 16) - 32768) / 32768.0;
        }
        if (n <= 8192) reference(x, ref, n);
        CHECK_TYPE(float, faar_rfftf_plan, faar_rfftf_init, faar_rfftf_execute,
                   faar_rfftf_clear, "float", 2e-6);
        CHECK_TYPE(double, faar_rfftd_plan, faar_rfftd_init, faar_rfftd_execute,
                   faar_rfftd_clear, "double", 2e-12);
        free(x); free(ref);
    }
    return failed;
}
