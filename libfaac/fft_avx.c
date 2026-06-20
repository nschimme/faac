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
#include <assert.h>
#include "faac_real.h"
#include "fft.h"

void fft_proc_avx2(
    faac_real * __restrict xr,
    faac_real * __restrict xi,
    const fftfloat * __restrict refac,
    const fftfloat * __restrict imfac,
    int size)
{
    int step, pos, shift;
    int estep;

    /* First stage: step = 1 */
    for (pos = 0; pos < size; pos += 2)
    {
        faac_real v2r, v2i;
        v2r = xr[pos + 1];
        v2i = xi[pos + 1];
        xr[pos + 1] = xr[pos] - v2r;
        xr[pos] += v2r;
        xi[pos + 1] = xi[pos] - v2i;
        xi[pos] += v2i;
    }

    /* Second stage: step = 2 */
    if (size >= 4) {
        for (pos = 0; pos < size; pos += 4)
        {
            faac_real v2r, v2i;
            v2r = xr[pos + 2];
            v2i = xi[pos + 2];
            xr[pos + 2] = xr[pos] - v2r;
            xr[pos] += v2r;
            xi[pos + 2] = xi[pos] - v2i;
            xi[pos] += v2i;

            v2r = xi[pos + 3];
            v2i = -xr[pos + 3];
            xr[pos + 3] = xr[pos + 1] - v2r;
            xr[pos + 1] += v2r;
            xi[pos + 3] = xi[pos + 1] - v2i;
            xi[pos + 1] += v2i;
        }
    }

    /* Third stage: step = 4. Use 4-wide SSE2 logic.
     */
    estep = 0;
    if (size >= 8) {
        step = 4;
        for (pos = 0; pos < size; pos += 8)
        {
            int x1 = pos;
            int x2 = pos + 4;
            __m128 xr1, xi1, xr2, xi2, wr, wi, v2r, v2i, tr, ti;

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
            wr = _mm_loadu_ps(&refac[estep]);
            wi = _mm_loadu_ps(&imfac[estep]);

#if defined(__FMA__) || defined(HAVE_FMA)
            v2r = _mm_fmsub_ps(xr2, wr, _mm_mul_ps(xi2, wi));
            v2i = _mm_fmadd_ps(xr2, wi, _mm_mul_ps(xi2, wr));
#else
            v2r = _mm_sub_ps(_mm_mul_ps(xr2, wr), _mm_mul_ps(xi2, wi));
            v2i = _mm_add_ps(_mm_mul_ps(xr2, wi), _mm_mul_ps(xi2, wr));
#endif

            tr = _mm_sub_ps(xr1, v2r);
            xr1 = _mm_add_ps(xr1, v2r);
            ti = _mm_sub_ps(xi1, v2i);
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
        }
        estep += 4;
        step = 8;
    } else {
        step = 4;
    }

    /* Stages 4 onwards: step >= 8. Use AVX 256-bit (8-wide) and FMA if available. */
    for (; step < size; step *= 2)
    {
        assert((step & 7) == 0);

        for (pos = 0; pos < size; pos += (2 * step))
        {
            int x1 = pos;
            int x2 = pos + step;

            for (shift = 0; shift < step; shift += 8)
            {
                __m256 xr1_256, xi1_256, xr2_256, xi2_256, wr_256, wi_256;
                __m256 v2r_256, v2i_256, tr_256, ti_256;

#ifdef FAAC_PRECISION_SINGLE
                xr1_256 = _mm256_loadu_ps(&xr[x1]);
                xi1_256 = _mm256_loadu_ps(&xi[x1]);
                xr2_256 = _mm256_loadu_ps(&xr[x2]);
                xi2_256 = _mm256_loadu_ps(&xi[x2]);
#else
                __m128 l0, l1, l2, l3;
                l0 = _mm_cvtpd_ps(_mm_loadu_pd(&xr[x1]));
                l1 = _mm_cvtpd_ps(_mm_loadu_pd(&xr[x1+2]));
                l2 = _mm_cvtpd_ps(_mm_loadu_pd(&xr[x1+4]));
                l3 = _mm_cvtpd_ps(_mm_loadu_pd(&xr[x1+6]));
                xr1_256 = _mm256_setzero_ps();
                xr1_256 = _mm256_insertf128_ps(xr1_256, _mm_movelh_ps(l0, l1), 0);
                xr1_256 = _mm256_insertf128_ps(xr1_256, _mm_movelh_ps(l2, l3), 1);

                l0 = _mm_cvtpd_ps(_mm_loadu_pd(&xi[x1]));
                l1 = _mm_cvtpd_ps(_mm_loadu_pd(&xi[x1+2]));
                l2 = _mm_cvtpd_ps(_mm_loadu_pd(&xi[x1+4]));
                l3 = _mm_cvtpd_ps(_mm_loadu_pd(&xi[x1+6]));
                xi1_256 = _mm256_insertf128_ps(_mm256_setzero_ps(), _mm_movelh_ps(l0, l1), 0);
                xi1_256 = _mm256_insertf128_ps(xi1_256, _mm_movelh_ps(l2, l3), 1);

                l0 = _mm_cvtpd_ps(_mm_loadu_pd(&xr[x2]));
                l1 = _mm_cvtpd_ps(_mm_loadu_pd(&xr[x2+2]));
                l2 = _mm_cvtpd_ps(_mm_loadu_pd(&xr[x2+4]));
                l3 = _mm_cvtpd_ps(_mm_loadu_pd(&xr[x2+6]));
                xr2_256 = _mm256_insertf128_ps(_mm256_setzero_ps(), _mm_movelh_ps(l0, l1), 0);
                xr2_256 = _mm256_insertf128_ps(xr2_256, _mm_movelh_ps(l2, l3), 1);

                l0 = _mm_cvtpd_ps(_mm_loadu_pd(&xi[x2]));
                l1 = _mm_cvtpd_ps(_mm_loadu_pd(&xi[x2+2]));
                l2 = _mm_cvtpd_ps(_mm_loadu_pd(&xi[x2+4]));
                l3 = _mm_cvtpd_ps(_mm_loadu_pd(&xi[x2+6]));
                xi2_256 = _mm256_insertf128_ps(_mm256_setzero_ps(), _mm_movelh_ps(l0, l1), 0);
                xi2_256 = _mm256_insertf128_ps(xi2_256, _mm_movelh_ps(l2, l3), 1);
#endif

                wr_256 = _mm256_loadu_ps(&refac[estep + shift]);
                wi_256 = _mm256_loadu_ps(&imfac[estep + shift]);

#if defined(__FMA__) || defined(HAVE_FMA)
                v2r_256 = _mm256_fmsub_ps(xr2_256, wr_256, _mm256_mul_ps(xi2_256, wi_256));
                v2i_256 = _mm256_fmadd_ps(xr2_256, wi_256, _mm256_mul_ps(xi2_256, wr_256));
#else
                v2r_256 = _mm256_sub_ps(_mm256_mul_ps(xr2_256, wr_256), _mm256_mul_ps(xi2_256, wi_256));
                v2i_256 = _mm256_add_ps(_mm256_mul_ps(xr2_256, wi_256), _mm256_mul_ps(xi2_256, wr_256));
#endif

                tr_256 = _mm256_sub_ps(xr1_256, v2r_256);
                xr1_256 = _mm256_add_ps(xr1_256, v2r_256);
                ti_256 = _mm256_sub_ps(xi1_256, v2i_256);
                xi1_256 = _mm256_add_ps(xi1_256, v2i_256);

#ifdef FAAC_PRECISION_SINGLE
                _mm256_storeu_ps(&xr[x1], xr1_256);
                _mm256_storeu_ps(&xi[x1], xi1_256);
                _mm256_storeu_ps(&xr[x2], tr_256);
                _mm256_storeu_ps(&xi[x2], ti_256);
#else
                __m128 out0 = _mm256_extractf128_ps(xr1_256, 0);
                __m128 out1 = _mm256_extractf128_ps(xr1_256, 1);
                _mm_storeu_pd(&xr[x1], _mm_cvtps_pd(out0));
                _mm_storeu_pd(&xr[x1+2], _mm_cvtps_pd(_mm_movehl_ps(out0, out0)));
                _mm_storeu_pd(&xr[x1+4], _mm_cvtps_pd(out1));
                _mm_storeu_pd(&xr[x1+6], _mm_cvtps_pd(_mm_movehl_ps(out1, out1)));

                out0 = _mm256_extractf128_ps(xi1_256, 0);
                out1 = _mm256_extractf128_ps(xi1_256, 1);
                _mm_storeu_pd(&xi[x1], _mm_cvtps_pd(out0));
                _mm_storeu_pd(&xi[x1+2], _mm_cvtps_pd(_mm_movehl_ps(out0, out0)));
                _mm_storeu_pd(&xi[x1+4], _mm_cvtps_pd(out1));
                _mm_storeu_pd(&xi[x1+6], _mm_cvtps_pd(_mm_movehl_ps(out1, out1)));

                out0 = _mm256_extractf128_ps(tr_256, 0);
                out1 = _mm256_extractf128_ps(tr_256, 1);
                _mm_storeu_pd(&xr[x2], _mm_cvtps_pd(out0));
                _mm_storeu_pd(&xr[x2+2], _mm_cvtps_pd(_mm_movehl_ps(out0, out0)));
                _mm_storeu_pd(&xr[x2+4], _mm_cvtps_pd(out1));
                _mm_storeu_pd(&xr[x2+6], _mm_cvtps_pd(_mm_movehl_ps(out1, out1)));

                out0 = _mm256_extractf128_ps(ti_256, 0);
                out1 = _mm256_extractf128_ps(ti_256, 1);
                _mm_storeu_pd(&xi[x2], _mm_cvtps_pd(out0));
                _mm_storeu_pd(&xi[x2+2], _mm_cvtps_pd(_mm_movehl_ps(out0, out0)));
                _mm_storeu_pd(&xi[x2+4], _mm_cvtps_pd(out1));
                _mm_storeu_pd(&xi[x2+6], _mm_cvtps_pd(_mm_movehl_ps(out1, out1)));
#endif
                x1 += 8;
                x2 += 8;
            }
        }
        estep += step;
    }
}
