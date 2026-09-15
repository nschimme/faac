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

typedef float fftfloat;

#ifdef FAAC_FIXED_POINT
#include <stdint.h>
typedef int32_t fftfix32;

#define FIX_Q31(x) ((int32_t)((x) * 2147483647.0))
#define FIX_MUL_Q31(a, b) ((int32_t)(((int64_t)(a) * (b)) >> 31))
#endif

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
#ifdef FAAC_FIXED_POINT
    fftfix32 **costbl_fx;
    fftfix32 **negsintbl_fx;
#endif
} FFT_Tables;

void fft_initialize		( FFT_Tables *fft_tables );
void fft_terminate	( FFT_Tables *fft_tables );

void fft			( FFT_Tables *fft_tables, float *xr, float *xi, int logm );

#endif
