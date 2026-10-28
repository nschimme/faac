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
/* Independent long-double DFT verification for the complex FFT. */
#include "fft.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

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

/* Large transforms: a direct DFT of the whole spectrum is out of reach, so
 * compare the sampled bins only and keep the shared-plan and conjugation
 * checks. */
#define CHECK_COMPLEX_BINS(T, P, INIT, SINGLE, PREPARE, BUF, FWD, CLEAR, LABEL, TOL) do { \
    P plan = {0}; \
    long double err = 0, energy = 0; \
    double trip = 0; \
    T *w0, *w1, *out; \
    if (INIT(&plan, n)) exit(2); \
    w0 = BUF(&plan, 0); w1 = BUF(&plan, 1); \
    for (j = 0; j < n; j++) { w0[j] = (T)xr[j]; w0[n + j] = (T)xi[j]; } \
    out = FWD(&plan, w0); \
    for (k = 0; k < 64; k++) { \
        const int b = bins[k]; \
        long double re = 0, im = 0; \
        for (j = 0; j < n; j++) { \
            /* Reduce the angle exactly; stepping a twiddle by recurrence \
             * loses too much precision over these lengths. */ \
            long double a = tau * (long double)((long long)b * j % n) / n; \
            long double c = cosl(a), s = -sinl(a); \
            re += xr[j] * c - xi[j] * s; \
            im += xr[j] * s + xi[j] * c; \
        } \
        { long double dr = out[b] - re, di = out[n + b] - im; \
          err += dr * dr + di * di; energy += re * re + im * im; } \
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
    printf("%s complex N=%d bins=64 rms_rel=%.9Le roundtrip=%.9g\n", LABEL, n, \
           sqrtl(err / energy), trip); \
    if (!(sqrtl(err / energy) < TOL) || !(trip < 8 * TOL)) failed = 1; \
    CLEAR(&plan); \
} while (0)

static int check_complex(int selected)
{
    const int sizes[] = {16, 64, 512, 2048, 4096, 8192, 16384, 32768, 65536};
    int failed = 0, t, j, k;
    uint32_t state = 7;
    const long double tau = 6.283185307179586476925286766559005768L;
    faar_fftf_plan invalid = {0};

    if (!faar_fftf_cinit(&invalid, 8) || !faar_fftf_cinit(&invalid, 48)
            || !faar_fftf_cinit(&invalid, 1 << 23)) return 1;
    for (t = 0; t < 9; t++) {
        if (selected >= 0 && t != selected) continue;
        int n = sizes[t];
        int bins[64];
        double *xr = malloc(n * sizeof(double)), *xi = malloc(n * sizeof(double));
        long double *rr = NULL, *ri = NULL;
        if (!xr || !xi) exit(2);
        for (j = 0; j < n; j++) {
            state = state * 1664525u + 1013904223u;
            xr[j] = ((int)(state >> 16) - 32768) / 32768.0;
            state = state * 1664525u + 1013904223u;
            xi[j] = ((int)(state >> 16) - 32768) / 32768.0;
        }
        /* The O(n^2) long double reference is software binary128 on several
         * targets, so larger sizes check sampled bins instead. */
        if (t < 4) {
            rr = malloc(n * sizeof(long double));
            ri = malloc(n * sizeof(long double));
            if (!rr || !ri) exit(2);
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
            CHECK_COMPLEX(float, faar_fftf_plan, faar_fftf_cinit,
                          faar_fftf_cinit_shared_single, faar_fftf_prepare,
                          faar_fftf_cbuffer, faar_fftf_cforward,
                          faar_fftf_clear, "float", 2e-6);
            CHECK_COMPLEX(double, faar_fftd_plan, faar_fftd_cinit,
                          faar_fftd_cinit_shared_single, faar_fftd_prepare,
                          faar_fftd_cbuffer, faar_fftd_cforward,
                          faar_fftd_clear, "double", 2e-12);
        } else {
            /* Fixed extremes plus bins drawn from the same generator. */
            bins[0] = 0; bins[1] = 1; bins[2] = n / 2; bins[3] = n - 1;
            for (k = 4; k < 64; k++) {
                state = state * 1664525u + 1013904223u;
                bins[k] = (int)(state >> 16) % n;
            }
            CHECK_COMPLEX_BINS(float, faar_fftf_plan, faar_fftf_cinit,
                               faar_fftf_cinit_shared_single, faar_fftf_prepare,
                               faar_fftf_cbuffer, faar_fftf_cforward,
                               faar_fftf_clear, "float", 2e-6);
            CHECK_COMPLEX_BINS(double, faar_fftd_plan, faar_fftd_cinit,
                               faar_fftd_cinit_shared_single, faar_fftd_prepare,
                               faar_fftd_cbuffer, faar_fftd_cforward,
                               faar_fftd_clear, "double", 2e-12);
        }
        free(xr); free(xi); free(rr); free(ri);
    }
    return failed;
}

int main(int argc, char **argv)
{
    const int sizes[] = {16, 64, 512, 2048, 4096, 8192, 16384, 32768, 65536};
    int i, n;

    if (argc != 3 || strcmp(argv[1], "complex")) return 2;
    n = atoi(argv[2]);
    for (i = 0; i < 9 && sizes[i] != n; i++) {
        /* Find the selected transform size. */
    }
    if (i == 9) return 2;
    return check_complex(i);
}
