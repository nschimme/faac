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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

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

	if (!fft_tables->costbl || !fft_tables->negsintbl || !fft_tables->reordertbl)
	{
		if (fft_tables->costbl) FreeMemory(fft_tables->costbl);
		if (fft_tables->negsintbl) FreeMemory(fft_tables->negsintbl);
		if (fft_tables->reordertbl) FreeMemory(fft_tables->reordertbl);
		fft_tables->costbl = NULL;
		fft_tables->negsintbl = NULL;
		fft_tables->reordertbl = NULL;
		return;
	}
	
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

#ifndef USE_RADIX4_FFT

static void reorder2( FFT_Tables *fft_tables, faac_real *xr, faac_real *xi, int logm)
{
	int i;
	int size = 1 << logm;
	const unsigned short *r;


	if ( fft_tables->reordertbl[logm] == NULL ) // create bit reversing table
	{
		fft_tables->reordertbl[logm] = AllocMemory(size * sizeof(*(fft_tables->reordertbl[0])));
		if (!fft_tables->reordertbl[logm]) return;

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
		faac_real * __restrict xr,
		faac_real * __restrict xi,
		const fftfloat * __restrict refac,
		const fftfloat * __restrict imfac,
		int size)
{
	int step, shift, pos;
	int exp, estep;

	estep = size >> 1;
	/* First stage: step = 1
	   Twiddle factor W_N^0 is always (1, 0).
	   Eliminate all multiplications and table lookups.
	*/
	for (pos = 0; pos < size; pos += 2)
	{
		faac_real v2r, v2i;
		int x1 = pos;
		int x2 = pos + 1;

		v2r = xr[x2];
		v2i = xi[x2];

		xr[x2] = xr[x1] - v2r;
		xr[x1] += v2r;

		xi[x2] = xi[x1] - v2i;
		xi[x1] += v2i;
	}

	/* Second stage: step = 2
	   shift = 0: Twiddle is (1, 0).
	   shift = 1: Twiddle is (0, -1).
	   Eliminate multiplications and avoid trig/table calls entirely.
	*/
	if (size >= 4) {
		for (pos = 0; pos < size; pos += 4)
		{
			faac_real v2r, v2i;
			int x1 = pos;
			int x2 = pos + 2;

			/* shift = 0: Rotation by 0 degrees */
			v2r = xr[x2];
			v2i = xi[x2];

			xr[x2] = xr[x1] - v2r;
			xr[x1] += v2r;

			xi[x2] = xi[x1] - v2i;
			xi[x1] += v2i;

			/* shift = 1: Rotation by -90 degrees */
			x1++;
			x2++;

			v2r = xi[x2];
			v2i = -xr[x2];

			xr[x2] = xr[x1] - v2r;
			xr[x1] += v2r;

			xi[x2] = xi[x1] - v2i;
			xi[x1] += v2i;
		}
	}

	/* Resume standard Radix-2 loop from stage 3 (step = 4) */
	estep = 0;
	for (step = 4; step < size; step *= 2)
	{
		for (pos = 0; pos < size; pos += (2 * step))
		{
			int x1 = pos;
			int x2 = pos + step;
			exp = estep;
			for (shift = 0; shift < step; shift++)
			{
				faac_real v2r, v2i;

				v2r = xr[x2] * refac[exp] - xi[x2] * imfac[exp];
				v2i = xr[x2] * imfac[exp] + xi[x2] * refac[exp];

				xr[x2] = xr[x1] - v2r;
				xr[x1] += v2r;

				xi[x2] = xi[x1] - v2i;
				xi[x1] += v2i;

				exp++;
				x1++;
				x2++;
			}
		}
		estep += step;
	}
}

static void check_tables( FFT_Tables *fft_tables, int logm)
{
	if( fft_tables->costbl[logm] == NULL )
	{
		int step;
		int size = 1 << logm;
		int offset = 0;

		if( fft_tables->negsintbl[logm] != NULL )
			FreeMemory( fft_tables->negsintbl[logm] );

		/* Total elements: 4 + 8 + ... + size/2 = size - 4.
		   We allocate exactly the required size for stage-contiguous twiddles.
		   Guard against size < 4 (logm < 2) which would result in negative allocation.
		*/
		int alloc_size = (size < 4) ? 0 : (size - 4);
		fft_tables->costbl[logm]	= AllocMemory(alloc_size * sizeof(*(fft_tables->costbl[0])));
		fft_tables->negsintbl[logm]	= AllocMemory(alloc_size * sizeof(*(fft_tables->negsintbl[0])));

		if (!fft_tables->costbl[logm] || !fft_tables->negsintbl[logm])
		{
			if (fft_tables->costbl[logm]) FreeMemory(fft_tables->costbl[logm]);
			if (fft_tables->negsintbl[logm]) FreeMemory(fft_tables->negsintbl[logm]);
			fft_tables->costbl[logm] = fft_tables->negsintbl[logm] = NULL;
			return;
		}

		for (step = 4; step < size; step *= 2)
		{
			int shift;
			for (shift = 0; shift < step; shift++)
			{
				faac_real theta = M_PI * (faac_real)shift / (faac_real)step;
				fft_tables->costbl[logm][offset] = (fftfloat)FAAC_COS(theta);
				fft_tables->negsintbl[logm][offset] = (fftfloat)-FAAC_SIN(theta);
				offset++;
			}
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

#else /* USE_RADIX4_FFT */

/* Radix-4 Decimation-in-Frequency FFT
 * This implementation uses a butterfly that produces bit-reversed output
 * by swapping the 2nd and 3rd output branches. This allows using a standard
 * bit-reversal at the end instead of a more complex digit-reversal.
 * It handles logm=6 (64) and logm=9 (512). Since 512 is not a power of 4,
 * a final Radix-2 stage is used.
 */

static void check_tables_radix4(FFT_Tables *fft_tables, int logm)
{
	if (fft_tables->costbl[logm] == NULL)
	{
		int size = 1 << logm;
		int i;
		/* For Radix-4 DIF, we need twiddle factors W_N^k, W_N^{2k}, W_N^{3k} */
		fft_tables->costbl[logm] = AllocMemory(size * sizeof(*(fft_tables->costbl[0])));
		fft_tables->negsintbl[logm] = AllocMemory(size * sizeof(*(fft_tables->negsintbl[0])));

		if (!fft_tables->costbl[logm] || !fft_tables->negsintbl[logm])
		{
			if (fft_tables->costbl[logm]) FreeMemory(fft_tables->costbl[logm]);
			if (fft_tables->negsintbl[logm]) FreeMemory(fft_tables->negsintbl[logm]);
			fft_tables->costbl[logm] = fft_tables->negsintbl[logm] = NULL;
			return;
		}

		for (i = 0; i < size; i++)
		{
			faac_real theta = 2.0 * M_PI * (faac_real)i / (faac_real)size;
			fft_tables->costbl[logm][i] = (fftfloat)FAAC_COS(theta);
			fft_tables->negsintbl[logm][i] = (fftfloat)-FAAC_SIN(theta);
		}
	}
}

static void radix4_dif_proc(
    faac_real * restrict xr,
    faac_real * restrict xi,
    int logm,
    const fftfloat * restrict costbl,
    const fftfloat * restrict sintbl)
{
	int n = 1 << logm;
	int n2 = n;
	int n1;
	int i, j, k;

	/* Radix-4 stages */
	for (k = 0; k < (logm >> 1); k++)
	{
		n1 = n2;
		n2 >>= 2;
		for (i = 0; i < n; i += n1)
		{
			faac_real * restrict r1p = xr + i;
			faac_real * restrict r2p = xr + i + n2;
			faac_real * restrict r3p = xr + i + 2*n2;
			faac_real * restrict r4p = xr + i + 3*n2;
			faac_real * restrict i1p = xi + i;
			faac_real * restrict i2p = xi + i + n2;
			faac_real * restrict i3p = xi + i + 2*n2;
			faac_real * restrict i4p = xi + i + 3*n2;

			/* j = 0 case: twiddles are all (1, 0) */
			{
				faac_real r1 = *r1p, i1 = *i1p;
				faac_real r2 = *r2p, i2 = *i2p;
				faac_real r3 = *r3p, i3 = *i3p;
				faac_real r4 = *r4p, i4 = *i4p;

				faac_real t1 = r1 + r3, t2 = i1 + i3;
				faac_real t3 = r2 + r4, t4 = i2 + i4;
				faac_real t5 = r1 - r3, t6 = i1 - i3;
				faac_real t7 = r2 - r4, t8 = i2 - i4;

				*r1p = t1 + t3; *i1p = t2 + t4;
				*r3p = t5 + t8; *i3p = t6 - t7;
				*r2p = t1 - t3; *i2p = t2 - t4;
				*r4p = t5 - t8; *i4p = t6 + t7;

				r1p++; r2p++; r3p++; r4p++;
				i1p++; i2p++; i3p++; i4p++;
			}

			/* Process j=1..n2-1 with twiddle lookups.
			   Pointer-based access with unit stride aids autovectorization.
			*/
			for (j = 1; j < n2; j++)
			{
				int tw_idx = j << (2 * k);
				const faac_real c1 = (faac_real)costbl[tw_idx];
				const faac_real s1 = (faac_real)sintbl[tw_idx];
				const faac_real c2 = (faac_real)costbl[2 * tw_idx];
				const faac_real s2 = (faac_real)sintbl[2 * tw_idx];
				const faac_real c3 = (faac_real)costbl[3 * tw_idx];
				const faac_real s3 = (faac_real)sintbl[3 * tw_idx];

				faac_real r1 = *r1p, i1 = *i1p;
				faac_real r2 = *r2p, i2 = *i2p;
				faac_real r3 = *r3p, i3 = *i3p;
				faac_real r4 = *r4p, i4 = *i4p;

				faac_real t1 = r1 + r3, t2 = i1 + i3;
				faac_real t3 = r2 + r4, t4 = i2 + i4;
				faac_real t5 = r1 - r3, t6 = i1 - i3;
				faac_real t7 = r2 - r4, t8 = i2 - i4;

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

	/* Final Radix-2 stage if logm is odd */
	if (logm & 1)
	{
		faac_real * restrict r1p = xr;
		faac_real * restrict r2p = xr + 1;
		faac_real * restrict i1p = xi;
		faac_real * restrict i2p = xi + 1;
		for (i = 0; i < n; i += 2)
		{
			faac_real r1 = *r1p, i1 = *i1p;
			faac_real r2 = *r2p, i2 = *i2p;
			*r1p = r1 + r2; *i1p = i1 + i2;
			*r2p = r1 - r2; *i2p = i1 - i2;
			r1p += 2; r2p += 2; i1p += 2; i2p += 2;
		}
	}
}

static void bit_reverse(
    faac_real * restrict xr,
    faac_real * restrict xi,
    int logm,
    const unsigned short * restrict r)
{
	int i;
	int size = 1 << logm;

	for (i = 0; i < size; i++)
	{
		int j = (int)r[i];
		if (j > i)
		{
			faac_real tr = xr[i]; xr[i] = xr[j]; xr[j] = tr;
			faac_real ti = xi[i]; xi[i] = xi[j]; xi[j] = ti;
		}
	}
}

void fft(FFT_Tables *fft_tables, faac_real *xr, faac_real *xi, int logm)
{
	if (logm > MAXLOGM) return;
	if (logm < 1) return;

	check_tables_radix4(fft_tables, logm);

	if (fft_tables->reordertbl[logm] == NULL)
	{
		int size = 1 << logm;
		int i;
		fft_tables->reordertbl[logm] = AllocMemory(size * sizeof(*(fft_tables->reordertbl[0])));
		if (!fft_tables->reordertbl[logm]) return;

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

	radix4_dif_proc(xr, xi, logm, fft_tables->costbl[logm], fft_tables->negsintbl[logm]);
	bit_reverse(xr, xi, logm, fft_tables->reordertbl[logm]);
}

#endif /* USE_RADIX4_FFT */

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
