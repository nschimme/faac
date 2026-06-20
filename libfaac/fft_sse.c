/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2025 Nils Schimmelmann
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
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <immintrin.h>
#include "faac_real.h"
#include "fft.h"

void fft_proc_sse2(
    faac_real * __restrict xr,
    faac_real * __restrict xi,
    const fftfloat * __restrict refac,
    const fftfloat * __restrict imfac,
    int size)
{
    int step, shift, pos;
    int exp, estep;

    /* First stage: step = 1 */
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

    /* Second stage: step = 2 */
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

    /* Standard Radix-2 loop from stage 3 (step = 4) */
    estep = 0;
    for (step = 4; step < size; step *= 2)
    {
        for (pos = 0; pos < size; pos += (2 * step))
        {
            int x1 = pos;
            int x2 = pos + step;

            for (shift = 0; shift <= step - 4; shift += 4)
            {
                __m128 xr1, xi1, xr2, xi2, wr, wi;
                __m128 v2r, v2i, tr, ti;

#ifdef FAAC_PRECISION_SINGLE
                xr1 = _mm_loadu_ps(&xr[x1]);
                xi1 = _mm_loadu_ps(&xi[x1]);
                xr2 = _mm_loadu_ps(&xr[x2]);
                xi2 = _mm_loadu_ps(&xi[x2]);
#else
                xr1 = _mm_movelh_ps(_mm_cvtpd_ps(_mm_loadu_pd(&xr[x1])), _mm_cvtpd_ps(_mm_loadu_pd(&xr[x1+2])));
                xi1 = _mm_movelh_ps(_mm_cvtpd_ps(_mm_loadu_pd(&xi[x1])), _mm_cvtpd_ps(_mm_loadu_pd(&xi[x1+2])));
                xr2 = _mm_movelh_ps(_mm_cvtpd_ps(_mm_loadu_pd(&xr[x2])), _mm_cvtpd_ps(_mm_loadu_pd(&xr[x2+2])));
                xi2 = _mm_movelh_ps(_mm_cvtpd_ps(_mm_loadu_pd(&xi[x2])), _mm_cvtpd_ps(_mm_loadu_pd(&xi[x2+2])));
#endif
                /* Load twiddle factors from contiguous tables */
                wr = _mm_loadu_ps(&refac[estep + shift]);
                wi = _mm_loadu_ps(&imfac[estep + shift]);

                /* v2r = xr[x2] * wr - xi[x2] * wi */
                v2r = _mm_sub_ps(_mm_mul_ps(xr2, wr), _mm_mul_ps(xi2, wi));
                /* v2i = xr[x2] * wi + xi[x2] * wr */
                v2i = _mm_add_ps(_mm_mul_ps(xr2, wi), _mm_mul_ps(xi2, wr));

                /* xr[x2] = xr[x1] - v2r */
                tr = _mm_sub_ps(xr1, v2r);
                /* xr[x1] += v2r */
                xr1 = _mm_add_ps(xr1, v2r);

                /* xi[x2] = xi[x1] - v2i */
                ti = _mm_sub_ps(xi1, v2i);
                /* xi[x1] += v2i */
                xi1 = _mm_add_ps(xi1, v2i);

#ifdef FAAC_PRECISION_SINGLE
                _mm_storeu_ps(&xr[x1], xr1);
                _mm_storeu_ps(&xi[x1], xi1);
                _mm_storeu_ps(&xr[x2], tr);
                _mm_storeu_ps(&xi[x2], ti);
#else
                _mm_storeu_pd(&xr[x1], _mm_cvtps_pd(xr1));
                _mm_storeu_pd(&xr[x1+2], _mm_cvtps_pd(_mm_movehl_ps(xr1, xr1)));
                _mm_storeu_pd(&xi[x1], _mm_cvtps_pd(xi1));
                _mm_storeu_pd(&xi[x1+2], _mm_cvtps_pd(_mm_movehl_ps(xi1, xi1)));
                _mm_storeu_pd(&xr[x2], _mm_cvtps_pd(tr));
                _mm_storeu_pd(&xr[x2+2], _mm_cvtps_pd(_mm_movehl_ps(tr, tr)));
                _mm_storeu_pd(&xi[x2], _mm_cvtps_pd(ti));
                _mm_storeu_pd(&xi[x2+2], _mm_cvtps_pd(_mm_movehl_ps(ti, ti)));
#endif
                x1 += 4;
                x2 += 4;
            }

            /* Scalar remainder for shift */
            exp = estep + shift;
            for (; shift < step; shift++)
            {
                faac_real v2r, v2i;
                faac_real wr_s = refac[exp];
                faac_real wi_s = imfac[exp];

                v2r = xr[x2] * wr_s - xi[x2] * wi_s;
                v2i = xr[x2] * wi_s + xi[x2] * wr_s;

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
