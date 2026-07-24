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

#ifndef _FFT_H_
#define _FFT_H_


#define FFT_MAXLOGM 9

#define LOGM_SHORT 6      /* logm for the 256-sample short block MDCT */
#define LOGM_LONG  FFT_MAXLOGM /* logm for the 2048-sample long block MDCT */

typedef float fftfloat;

typedef struct
{
    fftfloat **costbl;
    fftfloat **negsintbl;
    unsigned short **reordertbl;
    /* MDCT pre/post-twiddle factors cos/sin(freq*(i+1/8)), one table pair per
     * transform size (indexed by the size's fft logm). Precomputing them
     * breaks the serial cos/sin recurrence that kept the MDCT twiddle loops
     * from vectorizing, and is more accurate than the recurrence. */
    fftfloat *mdct_cos[FFT_MAXLOGM + 1];
    fftfloat *mdct_sin[FFT_MAXLOGM + 1];
} FFT_Tables;

int fft_initialize		( FFT_Tables *fft_tables );
void fft_terminate	( FFT_Tables *fft_tables );

void fft64			( FFT_Tables *fft_tables, float *xr, float *xi );

/* Runs Radix-4 DIF stages k_start..logm/2-1, plus the trailing radix-2
 * stage for odd logm. k_start=0 is a full transform; filtbank.c's MDCT
 * fuses stage 0 with its pre-twiddle fold and passes k_start=1 to resume
 * here, so both callers share one engine instead of duplicating it. */
void radix4_dif_run(
    float * restrict xr,
    float * restrict xi,
    int logm,
    int k_start,
    const fftfloat * restrict costbl,
    const fftfloat * restrict sintbl);

#endif
