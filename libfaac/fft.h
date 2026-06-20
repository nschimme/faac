/*
 * FAAC - Freeware Advanced Audio Coder
 * $Id: fft.h,v 1.6 2005/02/02 07:50:35 sur Exp $
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

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef _FFT_H_
#define _FFT_H_

#include "faac_real.h"
#include "cpu_compute.h"

/* Twiddle factor precision is fixed to single-precision float.
 * This ensures 4-wide SSE2 SIMD compatibility and maximizes throughput
 * even when the encoder's core precision (faac_real) is set to double.
 */
typedef float fftfloat;

typedef struct
{
    fftfloat **costbl;
    fftfloat **negsintbl;
    unsigned short **reordertbl;
    FFTProcFunc fft_proc;
} FFT_Tables;

void fft_initialize		( FFT_Tables *fft_tables, FFTProcFunc fft_proc );
void fft_terminate	( FFT_Tables *fft_tables );

void rfft			( FFT_Tables *fft_tables, faac_real *x, int logm );
void fft			( FFT_Tables *fft_tables, faac_real *xr, faac_real *xi, int logm );

void fft_proc_scalar(
		faac_real * __restrict xr,
		faac_real * __restrict xi,
		const fftfloat * __restrict refac,
		const fftfloat * __restrict imfac,
		int size);

#if defined(HAVE_SSE2)
void fft_proc_sse2(
		faac_real * __restrict xr,
		faac_real * __restrict xi,
		const fftfloat * __restrict refac,
		const fftfloat * __restrict imfac,
		int size);
#endif

#if defined(HAVE_AVX)
void fft_proc_avx(
		faac_real * __restrict xr,
		faac_real * __restrict xi,
		const fftfloat * __restrict refac,
		const fftfloat * __restrict imfac,
		int size);
#endif

#endif
