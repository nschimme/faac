/*
 * FAAC - Freeware Advanced Audio Coder
 * $Id: fft.c,v 1.12 2005/02/02 07:49:55 sur Exp $
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

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "fft.h"
#include "util.h"

#define MAXLOGM 9
#define MAXLOGR 8

void fft_initialize( FFT_Tables *fft_tables )
{
	int i;
	fft_tables->costbl		= AllocMemory( (MAXLOGM+1) * sizeof( fft_tables->costbl[0] ) );
	fft_tables->negsintbl	= AllocMemory( (MAXLOGM+1) * sizeof( fft_tables->negsintbl[0] ) );
	fft_tables->reordertbl	= AllocMemory( (MAXLOGM+1) * sizeof( fft_tables->reordertbl[0] ) );
	
	for( i = 0; i< MAXLOGM+1; i++ )
	{
		fft_tables->costbl[i]		= NULL;
		fft_tables->negsintbl[i]	= NULL;
		fft_tables->reordertbl[i]	= NULL;
	}
}

void fft_terminate( FFT_Tables *fft_tables )
{
	int i;

	for( i = 0; i< MAXLOGM+1; i++ )
	{
		if( fft_tables->costbl[i] != NULL )
			FreeMemory( fft_tables->costbl[i] );
		
		if( fft_tables->negsintbl[i] != NULL )
			FreeMemory( fft_tables->negsintbl[i] );
			
		if( fft_tables->reordertbl[i] != NULL )
			FreeMemory( fft_tables->reordertbl[i] );
	}

	FreeMemory( fft_tables->costbl );
	FreeMemory( fft_tables->negsintbl );
	FreeMemory( fft_tables->reordertbl );

	fft_tables->costbl		= NULL;
	fft_tables->negsintbl	= NULL;
	fft_tables->reordertbl	= NULL;
}

static void reorder2( FFT_Tables *fft_tables, faac_real *xr, faac_real *xi, int logm)
{
	int i;
	int size = 1 << logm;
	const unsigned short *r;


	if ( fft_tables->reordertbl[logm] == NULL ) // create bit reversing table
	{
		fft_tables->reordertbl[logm] = AllocMemory(size * sizeof(*(fft_tables->reordertbl[0])));

		for (i = 0; i < size; i++)
		{
			int reversed = 0;
			int b0;
			int tmp = i;

			for (b0 = 0; b0 < logm; b0++)
			{
				reversed = (reversed << 1) | (tmp & 1);
				tmp >>= 1;
			}
			fft_tables->reordertbl[logm][i] = reversed;
		}
	}

	r = fft_tables->reordertbl[logm];

	for (i = 0; i < size; i++)
	{
		int j = r[i];
		faac_real tmp;

		if (j <= i)
			continue;

		tmp = xr[i];
		xr[i] = xr[j];
		xr[j] = tmp;

		tmp = xi[i];
		xi[i] = xi[j];
		xi[j] = tmp;
	}
}

static void fft_proc(
		faac_real *xr,
		faac_real *xi,
		fftfloat *refac, 
		fftfloat *imfac, 
		int size)	
{
	int step, shift, pos;
	int exp, estep;

	estep = size >> 1;
	/* First stage: step = 1
	   Twiddle factor W_N^0 is always (1, 0).
	   Eliminate all multiplications and table lookups.
	*/
	faac_real * __restrict p_xr = xr;
	faac_real * __restrict p_xi = xi;
	for (pos = 0; pos < size; pos += 2)
	{
		faac_real v2r = p_xr[1];
		faac_real v2i = p_xi[1];
		faac_real v1r = p_xr[0];
		faac_real v1i = p_xi[0];

		p_xr[1] = v1r - v2r;
		p_xr[0] = v1r + v2r;
		p_xi[1] = v1i - v2i;
		p_xi[0] = v1i + v2i;
		p_xr += 2;
		p_xi += 2;
	}

	/* Second stage: step = 2
	   shift = 0: Twiddle is (1, 0).
	   shift = 1: Twiddle is (0, -1).
	   Eliminate multiplications and avoid trig/table calls entirely.
	*/
	if (size >= 4) {
		for (pos = 0; pos < size; pos += 4)
		{
			faac_real * __restrict pxr1 = xr + pos;
			faac_real * __restrict pxi1 = xi + pos;
			faac_real * __restrict pxr2 = xr + pos + 2;
			faac_real * __restrict pxi2 = xi + pos + 2;

			/* shift = 0: Rotation by 0 degrees */
			faac_real v2r = *pxr2;
			faac_real v2i = *pxi2;
			faac_real v1r = *pxr1;
			faac_real v1i = *pxi1;

			*pxr2++ = v1r - v2r;
			*pxr1++ = v1r + v2r;
			*pxi2++ = v1i - v2i;
			*pxi1++ = v1i + v2i;

			/* shift = 1: Rotation by -90 degrees (W_4^1 = -j) */
			v2r = *pxi2;
			v2i = -*pxr2;
			v1r = *pxr1;
			v1i = *pxi1;

			*pxr2 = v1r - v2r;
			*pxr1 = v1r + v2r;
			*pxi2 = v1i - v2i;
			*pxi1 = v1i + v2i;
		}
	}

	/* Resume standard Radix-2 loop from stage 3 (step = 4) */
	estep = size >> 2;
	for (step = 4; step < size; step *= 2)
	{
		estep >>= 1;
		for (pos = 0; pos < size; pos += (2 * step))
		{
			faac_real * __restrict pxr1 = xr + pos;
			faac_real * __restrict pxi1 = xi + pos;
			faac_real * __restrict pxr2 = xr + pos + step;
			faac_real * __restrict pxi2 = xi + pos + step;
			exp = 0;
			for (shift = 0; shift < step; shift++)
			{
				faac_real x2r = *pxr2;
				faac_real x2i = *pxi2;
				faac_real r_f = refac[exp];
				faac_real i_f = imfac[exp];

				faac_real v2r = x2r * r_f - x2i * i_f;
				faac_real v2i = x2r * i_f + x2i * r_f;

				faac_real x1r = *pxr1;
				faac_real x1i = *pxi1;

				*pxr2++ = x1r - v2r;
				*pxr1++ = x1r + v2r;

				*pxi2++ = x1i - v2i;
				*pxi1++ = x1i + v2i;

				exp += estep;
			}
		}
	}
}

static void check_tables( FFT_Tables *fft_tables, int logm)
{
	if( fft_tables->costbl[logm] == NULL )
	{
		int i;
		int size = 1 << logm;

		if( fft_tables->negsintbl[logm] != NULL )
			FreeMemory( fft_tables->negsintbl[logm] );

		fft_tables->costbl[logm]	= AllocMemory((size / 2) * sizeof(*(fft_tables->costbl[0])));
		fft_tables->negsintbl[logm]	= AllocMemory((size / 2) * sizeof(*(fft_tables->negsintbl[0])));

		for (i = 0; i < (size >> 1); i++)
		{
			faac_real theta = 2.0 * M_PI * ((faac_real) i) / (faac_real) size;
			fft_tables->costbl[logm][i]		= FAAC_COS(theta);
			fft_tables->negsintbl[logm][i]	= -FAAC_SIN(theta);
		}
	}
}

void fft( FFT_Tables *fft_tables, faac_real *xr, faac_real *xi, int logm)
{
	if (logm > MAXLOGM)
	{
		fprintf(stderr, "%s:%d: fft size too big (%d)\n",
		        __FILE__, __LINE__, logm);
		return;
	}

	if (logm < 1)
	{
		//printf("logm < 1\n");
		return;
	}

	check_tables( fft_tables, logm);

	reorder2( fft_tables, xr, xi, logm);

	fft_proc( xr, xi, fft_tables->costbl[logm], fft_tables->negsintbl[logm], 1 << logm );
}

void rfft( FFT_Tables *fft_tables, faac_real *x, int logm)
{
	faac_real xi[1 << MAXLOGR];

	if (logm > MAXLOGR)
	{
		fprintf(stderr, "%s:%d: rfft size too big (%d)\n",
		        __FILE__, __LINE__, logm);
		return;
	}

	memset(xi, 0, (1 << logm) * sizeof(xi[0]));

	fft( fft_tables, x, xi, logm);

	memcpy(x + (1 << (logm - 1)), xi, (1 << (logm - 1)) * sizeof(*x));
}
