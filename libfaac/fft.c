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

static void radix4_dif_proc(faac_real *xr, faac_real *xi, int logm, const fftfloat *costbl, const fftfloat *sintbl)
{
	int n = 1 << logm;
	int n2 = n;
	int n1;
	int i, j, k;
	faac_real r1, r2, r3, r4, i1, i2, i3, i4;
	faac_real t1, t2, t3, t4, t5, t6, t7, t8;
	faac_real c1, s1, c2, s2, c3, s3;

	/* Radix-4 stages */
	for (k = 0; k < (logm >> 1); k++)
	{
		n1 = n2;
		n2 >>= 2;
		for (i = 0; i < n; i += n1)
		{
			for (j = 0; j < n2; j++)
			{
				int idx1 = i + j;
				int idx2 = idx1 + n2;
				int idx3 = idx2 + n2;
				int idx4 = idx3 + n2;

				r1 = xr[idx1]; i1 = xi[idx1];
				r2 = xr[idx2]; i2 = xi[idx2];
				r3 = xr[idx3]; i3 = xi[idx3];
				r4 = xr[idx4]; i4 = xi[idx4];

				/* Radix-4 Butterfly */
				t1 = r1 + r3; t2 = i1 + i3;
				t3 = r2 + r4; t4 = i2 + i4;
				t5 = r1 - r3; t6 = i1 - i3;
				t7 = r2 - r4; t8 = i2 - i4;

				xr[idx1] = t1 + t3;
				xi[idx1] = t2 + t4;

				r1 = t1 - t3; // X2
				i1 = t2 - t4; // X2
				r2 = t5 + t8; // X1
				i2 = t6 - t7; // X1
				r3 = t5 - t8; // X3
				i3 = t6 + t7; // X3

				if (j == 0)
				{
					/* To produce bit-reversed output, X1 and X2 are swapped */
					xr[idx3] = r2; xi[idx3] = i2; // X1 -> Block 2
					xr[idx2] = r1; xi[idx2] = i1; // X2 -> Block 1
					xr[idx4] = r3; xi[idx4] = i3; // X3 -> Block 3
				}
				else
				{
					int tw_idx = j << (2 * k);
					c1 = costbl[tw_idx];
					s1 = sintbl[tw_idx];
					c2 = costbl[2 * tw_idx];
					s2 = sintbl[2 * tw_idx];
					c3 = costbl[3 * tw_idx];
					s3 = sintbl[3 * tw_idx];

					/* Twiddle multiplication and bit-reversed assignment */
					xr[idx3] = r2 * c1 - i2 * s1; // X1 * W^k -> Block 2
					xi[idx3] = r2 * s1 + i2 * c1;
					xr[idx2] = r1 * c2 - i1 * s2; // X2 * W^2k -> Block 1
					xi[idx2] = r1 * s2 + i1 * c2;
					xr[idx4] = r3 * c3 - i3 * s3; // X3 * W^3k -> Block 3
					xi[idx4] = r3 * s3 + i3 * c3;
				}
			}
		}
	}

	/* Final Radix-2 stage if logm is odd */
	if (logm & 1)
	{
		for (i = 0; i < n; i += 2)
		{
			r1 = xr[i]; i1 = xi[i];
			r2 = xr[i + 1]; i2 = xi[i + 1];
			xr[i] = r1 + r2;
			xi[i] = i1 + i2;
			xr[i + 1] = r1 - r2;
			xi[i + 1] = i1 - i2;
		}
	}
}

static void bit_reverse(FFT_Tables *fft_tables, faac_real *xr, faac_real *xi, int logm)
{
	int i;
	int size = 1 << logm;
	const unsigned short *r;

	if (fft_tables->reordertbl[logm] == NULL)
	{
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

	r = fft_tables->reordertbl[logm];
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
	radix4_dif_proc(xr, xi, logm, fft_tables->costbl[logm], fft_tables->negsintbl[logm]);
	bit_reverse(fft_tables, xr, xi, logm);
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
