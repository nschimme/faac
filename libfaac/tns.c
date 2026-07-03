/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2024 Project FAAC
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

/**
 * Temporal Noise Shaping (TNS) implementation for MPEG-4 AAC-LC.
 *
 * This implementation is designed to be highly efficient and distinct in
 * expression to ensure LGPL compliance while providing optimal performance
 * on modern CPU architectures.
 *
 * Senior Signal Engineer's Note: TNS operates by whitening the spectrum in
 * the frequency domain using linear predictive coding (LPC). Because the
 * frequency and time domains are duals, a whitening filter in frequency
 * corresponds to an envelope-shaping effect in time. By matching the
 * quantization noise envelope to the signal's temporal envelope, we
 * effectively mask noise that would otherwise be heard as pre-echo before
 * sharp transients.
 *
 * This version uses float-precision arithmetic for the TNS analysis stages,
 * which provides a performance boost through better autovectorization while
 * remaining more than accurate enough for a whitening filter application.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "frame.h"
#include "coder.h"
#include "tns.h"
#include "util.h"

/* TNS Scalefactor Band Limits (ISO/IEC 14496-3 Table 4.4.48)
   These limits ensure TNS is only applied above ~2kHz where it is most effective. */
static const struct {
    unsigned char min;
    unsigned char max;
} tns_sfb_range[12] = {
    {11, 31}, {12, 31}, {15, 34}, {16, 40}, {17, 42}, {20, 51},
    {25, 46}, {26, 46}, {24, 42}, {28, 42}, {30, 42}, {31, 39}
};

#define TNS_LPC_ORDER       8       /* Standard order for AAC-LC long windows */
#define TNS_GAIN_LIMIT      1.4f    /* Minimum prediction gain for TNS usage */
#define TNS_ATTACK_RATIO    1.4f    /* Min peak/mean sub-block energy to fire TNS */
#define TNS_MAX_BITRATE     80000   /* High-bitrate threshold for TNS bypass */
#define TNS_MIN_ENERGY      1e-9f   /* Energy floor for spectral analysis */

/**
 * Estimate autocorrelation for a segment of spectral data.
 * Optimized with local float buffer to encourage compiler autovectorization.
 */
static void calc_autocorr(int order, int length,
                          const faac_real * restrict spec,
                          float * restrict r)
{
    float work[BLOCK_LEN_LONG];
    int lag, i;

    /* Local copy to float for faster math and better vectorization potential */
    for (i = 0; i < length; i++) work[i] = (float)spec[i];

    for (lag = 0; lag <= order; lag++) {
        float acc = 0.0f;
        const float * p1 = work;
        const float * p2 = work + lag;
        int n = length - lag;

        /* Dot product pattern: easily recognized by modern compilers */
        for (i = 0; i < n; i++) {
            acc += p1[i] * p2[i];
        }
        r[lag] = acc;
    }
}

/**
 * Solve the Yule-Walker equations using Levinson-Durbin recursion.
 * Returns the prediction gain (original energy / residual energy).
 */
static float compute_lpc(int order, const float * restrict r,
                         float * restrict k)
{
    float a[TNS_MAX_ORDER + 1];
    float err;
    int i, j;

    if (r[0] <= 0.0f) {
        for (i = 1; i <= order; i++) k[i] = 0.0f;
        return 1.0f;
    }

    err = r[0];
    a[0] = 1.0f;

    for (i = 1; i <= order; i++) {
        float lambda = r[i];
        for (j = 1; j < i; j++) {
            lambda += a[j] * r[i - j];
        }

        if (err <= 0.0f) {
            for (; i <= order; i++) k[i] = 0.0f;
            break;
        }

        float rc = -lambda / err;
        /* Constrain reflection coefficient for filter stability */
        if (rc > 0.999f) rc = 0.999f;
        else if (rc < -0.999f) rc = -0.999f;
        k[i] = rc;

        /* Update LPC coefficients using symmetric property */
        int half = (i + 1) / 2;
        for (j = 1; j < half; j++) {
            float t1 = a[j];
            float t2 = a[i - j];
            a[j]     = t1 + rc * t2;
            a[i - j] = t2 + rc * t1;
        }
        if (i % 2 == 0) {
            a[i / 2] += rc * a[i / 2];
        }
        a[i] = rc;

        err *= (1.0f - rc * rc);
        if (err <= 0.0f) break;
    }

    if (err <= 1e-15f) return 100.0f; /* Perfect prediction case */

    return r[0] / err;
}

/**
 * Quantize reflection coefficients for bitstream transmission.
 * MPEG-4 AAC uses arcsine-domain quantization to better match human
 * perception of filter sensitivity.
 */
static void quantize_coeffs(int order, int res, float * restrict k, int * restrict idx)
{
    const float s_p = (float)(((1 << (res - 1)) - 0.5f) / (M_PI / 2));
    const float s_n = (float)(((1 << (res - 1)) + 0.5f) / (M_PI / 2));
    const int i_max =  (1 << (res - 1)) - 1;
    const int i_min = -(1 << (res - 1));

    for (int i = 1; i <= order; i++) {
        float val = k[i];
        float s = (val >= 0.0f) ? s_p : s_n;
        int q = (int)(asinf(val) * s + ((val >= 0.0f) ? 0.5f : -0.5f));

        if (q > i_max) q = i_max;
        else if (q < i_min) q = i_min;
        idx[i] = q;

        /* Requantize for local encoder filtering to match decoder exactly */
        s = (q >= 0) ? s_p : s_n;
        k[i] = sinf((float)q / s);
    }
}

/**
 * Convert reflection coefficients back to FIR predictor coefficients.
 * Uses a symmetric update to efficiently compute higher-order filters.
 */
static void finalize_filter(int order, const float * restrict k, faac_real * restrict a)
{
    int i, m;
    a[0] = 1.0;
    for (m = 1; m <= order; m++) {
        faac_real km = (faac_real)k[m];
        int half = (m + 1) / 2;
        for (i = 1; i < half; i++) {
            faac_real t1 = a[i];
            faac_real t2 = a[m - i];
            a[i]     = t1 + km * t2;
            a[m - i] = t2 + km * t1;
        }
        if (m % 2 == 0) {
            a[m / 2] += km * a[m / 2];
        }
        a[m] = km;
    }
}

/**
 * Apply the TNS analysis filter (FIR) to the spectral data.
 * The filter can be applied in forward or backward direction across frequency.
 */
static void filter_spec(int length, int order, int direction,
                        const faac_real * restrict a, faac_real * restrict spec)
{
    /* Snapshot the original inputs so the FIR can index the delay line directly
     * (no per-sample state shifting). length is bounded by the long block. */
    faac_real hist[BLOCK_LEN_LONG];
    int i, j;

    memcpy(hist, spec, length * sizeof(faac_real));

    if (direction) { /* Backward filtering (high to low frequency) */
        for (i = length - 1; i >= 0; i--) {
            faac_real acc = hist[i];
            int jmax = min(order, length - 1 - i);
            for (j = 1; j <= jmax; j++)
                acc += a[j] * hist[i + j];
            spec[i] = acc;
        }
    } else { /* Forward filtering (low to high frequency) */
        for (i = 0; i < length; i++) {
            faac_real acc = hist[i];
            int jmax = min(order, i);
            for (j = 1; j <= jmax; j++)
                acc += a[j] * hist[i - j];
            spec[i] = acc;
        }
    }
}

void TnsInit(faacEncStruct* hEncoder)
{
    unsigned int ch;
    int fs = hEncoder->sampleRateIdx;
    unsigned long br = hEncoder->config.bitRate;

    /* Bitrate estimation if quality mode is used */
    if (br == 0) br = hEncoder->config.quantqual * 640;

    int gated = (br >= TNS_MAX_BITRATE) || !hEncoder->config.useTns;
    hEncoder->config.useTns = !gated;

    for (ch = 0; ch < hEncoder->numChannels; ch++) {
        TnsInfo *info = &hEncoder->coderInfo[ch].tnsInfo;
        info->tnsDisabled = gated;
        info->tnsMaxBandsLong = tns_sfb_range[fs].max;
        info->tnsMinBandNumberLong = tns_sfb_range[fs].min;
        info->tnsMaxOrderLong = TNS_LPC_ORDER;
        info->gainThreshLong = (faac_real)TNS_GAIN_LIMIT;
    }
}

void TnsEncode(TnsInfo* tnsInfo, int numBands,
               enum WINDOW_TYPE blockType, int* sfbOffsetTable,
               faac_real* spec,
               const float* tdEnvelope, int tdEnvelopeLen)
{
    tnsInfo->tnsDataPresent = 0;
    tnsInfo->windowData[0].numFilters = 0;

    /* TNS is currently restricted to long windows to prioritize efficiency and
       because short-block transients are naturally handled by the filterbank's
       higher temporal resolution. */
    if (tnsInfo->tnsDisabled || blockType == ONLY_SHORT_WINDOW) return;

    /* Temporal-flatness gate: TNS only pays off when the frame contains a real
       temporal event (attack/decay edge) behind which the shaped quantization
       noise can hide. On temporally flat frames — steady tones, smooth decay
       tails — tonal spectra still yield high spectral prediction gain, so the
       gain gate alone fires; the whitened spectrum then spreads quantization
       noise across the whole block and is heard as reverb/smearing. Require a
       clear peak in the sub-block energy envelope before enabling the tool. */
    if (tdEnvelope && tdEnvelopeLen > 0) {
        float peak = 0.0f, mean = 0.0f;
        int w;
        for (w = 0; w < tdEnvelopeLen; w++) {
            float e = tdEnvelope[w];
            mean += e;
            if (e > peak) peak = e;
        }
        mean /= (float)tdEnvelopeLen;
        if (peak < TNS_ATTACK_RATIO * mean) return;
    }

    int b_start = min(tnsInfo->tnsMinBandNumberLong, numBands);
    int b_stop  = min(tnsInfo->tnsMaxBandsLong, numBands);
    if (b_stop <= b_start) return;

    int i_start = sfbOffsetTable[b_start];
    int length = sfbOffsetTable[b_stop] - i_start;
    if (length <= TNS_LPC_ORDER) return;

    faac_real *band = spec + i_start;
    faac_real energy = 0.0;
    for (int i = 0; i < length; i++) energy += band[i] * band[i];
    if (energy < (faac_real)TNS_MIN_ENERGY) return;

    float r[TNS_MAX_ORDER + 1] = {0};
    float k[TNS_MAX_ORDER + 1] = {0};

    calc_autocorr(TNS_LPC_ORDER, length, band, r);
    float gain = compute_lpc(TNS_LPC_ORDER, r, k);

    if (gain < TNS_GAIN_LIMIT) return;

    TnsFilterData *filter = &tnsInfo->windowData[0].tnsFilter[0];
    quantize_coeffs(TNS_LPC_ORDER, DEF_TNS_COEFF_RES, k, filter->index);

    /* Trim filter order based on coefficient significance */
    int order = TNS_LPC_ORDER;
    while (order > 0 && fabsf(k[order]) < (float)DEF_TNS_COEFF_THRESH) order--;
    if (order == 0) return;

    filter->order = order;
    filter->startBand = b_start;
    filter->stopBand = b_stop;
    filter->length = b_stop - b_start;

    /* Direction Heuristic: Backward filtering is used if energy rises across
       the frame, shaping noise towards the temporal masking region of the transient. */
    filter->direction = (tdEnvelope && tdEnvelopeLen >= 2 && tdEnvelope[tdEnvelopeLen - 1] > tdEnvelope[0]) ? 1 : 0;

    /* Check for coefficient compression possibility */
    filter->coefCompress = 1;
    int limit = 1 << (DEF_TNS_COEFF_RES - 2);
    for (int i = 1; i <= order; i++) {
        if (filter->index[i] < -limit || filter->index[i] >= limit) {
            filter->coefCompress = 0;
            break;
        }
    }

    finalize_filter(order, k, filter->aCoeffs);
    filter_spec(length, order, filter->direction, filter->aCoeffs, band);

    tnsInfo->windowData[0].numFilters = 1;
    tnsInfo->windowData[0].coefResolution = DEF_TNS_COEFF_RES;
    tnsInfo->tnsDataPresent = 1;
}
