/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2001 Menno Bakker
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

/*
 * Temporal Noise Shaping (TNS).
 *
 * Clean-room LGPL implementation of the AAC-LC TNS tool (ISO/IEC 14496-3),
 * structured after the LGPL TNS encoder in FFmpeg. TNS runs on long blocks
 * only: a single LPC filter is estimated across the spectral coefficients of
 * the TNS band and, if its prediction gain is worth the coding cost, the
 * spectrum is inverse-filtered in place so that quantization noise follows the
 * signal's temporal envelope.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "frame.h"
#include "coder.h"
#include "tns.h"
#include "util.h"

/* Lowest and highest TNS scalefactor band per sample-rate index (long window).
 * These band limits keep TNS above ~2 kHz, as specified by the standard. */
static const unsigned short tnsMinBandNumberLong[12] =
    { 11, 12, 15, 16, 17, 20, 25, 26, 24, 28, 30, 31 };
static const unsigned short tnsMaxBandsLong[12] =
    { 31, 31, 34, 40, 42, 51, 46, 46, 42, 42, 42, 39 };

#define TNS_MAX_ORDER_LONG  8       /* LPC order for long-window TNS */
#define TNS_GAIN_THRESHOLD  1.4     /* min prediction gain to spend TNS bits */
#define TNS_GATE_CEILING    80000   /* disable TNS at/above this bps per channel */
#define TNS_ENERGY_FLOOR    1e-9    /* skip near-silent TNS bands */

/*
 * Compute autocorrelation r[0..maxOrder] of data[0..dataSize-1].
 */
static void Autocorrelation(int maxOrder, int dataSize,
                            const faac_real * restrict data,
                            faac_real * restrict r)
{
    int lag, i;

    for (lag = 0; lag <= maxOrder; lag++) {
        faac_real acc = 0.0;
        for (i = lag; i < dataSize; i++)
            acc += data[i] * data[i - lag];
        r[lag] = acc;
    }
}

/*
 * Levinson-Durbin recursion. Fills reflection coefficients k[1..order] and
 * returns the LPC prediction gain (input energy / residual energy); 1.0 when
 * the signal is flat or empty.
 */
static faac_real LevinsonDurbin(int order, const faac_real * restrict r,
                                faac_real * restrict k)
{
    faac_real a[TNS_MAX_ORDER + 1];
    faac_real err;
    int i, j;

    for (i = 1; i <= order; i++)
        k[i] = 0.0;

    if (r[0] == 0.0)
        return 1.0;

    a[0] = 1.0;
    err = r[0];

    for (i = 1; i <= order; i++) {
        faac_real acc = r[i];
        for (j = 1; j < i; j++)
            acc += a[j] * r[i - j];

        if (err <= 0.0)
            break;

        faac_real refl = -acc / err;
        if (refl > 1.0)  refl = 1.0;
        if (refl < -1.0) refl = -1.0;
        k[i] = refl;

        a[i] = refl;
        for (j = 1; j <= (i >> 1); j++) {
            faac_real tmp = a[j];
            a[j]     += refl * a[i - j];
            a[i - j] += refl * tmp;
        }

        err *= (1.0 - refl * refl);
    }

    if (err <= 0.0)
        return (faac_real)(TNS_GAIN_THRESHOLD + 1.0);  /* perfect prediction */

    return r[0] / err;
}

/*
 * Quantize reflection coefficients to `res` bits, storing the integer indices
 * and the requantized coefficient values (which StepUp then consumes).
 */
static void QuantizeReflectionCoeffs(int order, int res,
                                     faac_real * restrict k, int * restrict index)
{
    const faac_real fac_pos = (faac_real)(((1 << (res - 1)) - 0.5) / (M_PI / 2));
    const faac_real fac_neg = (faac_real)(((1 << (res - 1)) + 0.5) / (M_PI / 2));
    const int i_max =  (1 << (res - 1)) - 1;
    const int i_min = -(1 << (res - 1));
    int i;

    for (i = 1; i <= order; i++) {
        faac_real fac = (k[i] >= 0.0) ? fac_pos : fac_neg;
        faac_real bias = (k[i] >= 0.0) ? 0.5 : -0.5;
        int idx = (int)(bias + FAAC_ASIN(k[i]) * fac);

        if (idx > i_max) idx = i_max;
        if (idx < i_min) idx = i_min;
        index[i] = idx;

        fac = (idx >= 0) ? fac_pos : fac_neg;
        k[i] = (faac_real)FAAC_SIN((faac_real)idx / fac);
    }
}

/*
 * Drop trailing (near-zero) reflection coefficients and return the resulting
 * filter order.
 */
static int TruncateOrder(int order, faac_real thresh, const faac_real * restrict k)
{
    while (order >= 1 && FAAC_FABS(k[order]) <= thresh)
        order--;
    return order;
}

/*
 * Convert reflection coefficients k[1..order] into LPC (AR) coefficients
 * a[0..order], a[0] == 1.
 */
static void StepUp(int order, const faac_real * restrict k, faac_real * restrict a)
{
    faac_real tmp[TNS_MAX_ORDER + 1];
    int i, m;

    a[0] = 1.0;
    for (m = 1; m <= order; m++) {
        for (i = 1; i < m; i++)
            tmp[i] = a[i] + k[m] * a[m - i];
        for (i = 1; i < m; i++)
            a[i] = tmp[i];
        a[m] = k[m];
    }
}

/*
 * Encoder-side TNS analysis filter, applied to `spec[0..length-1]` in place:
 * y[n] = x[n] + sum_{j=1..order} a[j] * x[n-j], where x is the original
 * (unfiltered) spectrum. This is the FIR whitening filter A(z); the decoder
 * reconstructs with the complementary all-pole 1/A(z). state[] is a delay line
 * of the last `order` original inputs. Forward direction runs low-to-high
 * index; backward runs high-to-low.
 */
static void TnsInvFilter(int length, int order, int direction,
                         const faac_real * restrict a, faac_real * restrict spec)
{
    faac_real state[TNS_MAX_ORDER + 1];
    int i, j;

    for (i = 0; i <= order; i++)
        state[i] = 0.0;

    if (direction) {
        for (i = length - 1; i >= 0; i--) {
            faac_real x = spec[i];
            faac_real acc = x;
            for (j = 1; j <= order; j++)
                acc += a[j] * state[j];
            for (j = order; j > 1; j--)
                state[j] = state[j - 1];
            state[1] = x;
            spec[i] = acc;
        }
    } else {
        for (i = 0; i < length; i++) {
            faac_real x = spec[i];
            faac_real acc = x;
            for (j = 1; j <= order; j++)
                acc += a[j] * state[j];
            for (j = order; j > 1; j--)
                state[j] = state[j - 1];
            state[1] = x;
            spec[i] = acc;
        }
    }
}

/*
 * Choose filter direction from the frame's time-domain energy envelope: filter
 * backward (high-to-low index) when energy rises across the frame (onset late
 * in the frame), forward otherwise. Falls back to forward when no envelope is
 * available.
 */
static int ChooseDirection(const float *env, int envLen)
{
    if (!env || envLen < 2)
        return 0;
    return (env[envLen - 1] > env[0]) ? 1 : 0;
}

void TnsInit(faacEncStruct* hEncoder)
{
    unsigned int channel;
    int fsIndex = hEncoder->sampleRateIdx;
    /* config.bitRate is already normalized to bps per channel by the frontend. */
    unsigned long bitratePerCh = hEncoder->config.bitRate;
    unsigned long effectiveBitratePerCh;

    if (bitratePerCh > 0)
        effectiveBitratePerCh = bitratePerCh;
    else
        /* Estimate bitrate from quality (quality 100 ~= 64 kbps/ch). */
        effectiveBitratePerCh = hEncoder->config.quantqual * 640;

    /* TNS is only enabled when requested and below the bitrate ceiling. */
    int tnsGated = (effectiveBitratePerCh >= TNS_GATE_CEILING);
    hEncoder->config.useTns = (hEncoder->config.useTns != 0) && !tnsGated;

    for (channel = 0; channel < hEncoder->numChannels; channel++) {
        TnsInfo *tnsInfo = &hEncoder->coderInfo[channel].tnsInfo;

        tnsInfo->tnsMaxBandsLong      = tnsMaxBandsLong[fsIndex];
        tnsInfo->tnsMinBandNumberLong = tnsMinBandNumberLong[fsIndex];
        tnsInfo->tnsMaxOrderLong      = TNS_MAX_ORDER_LONG;
        tnsInfo->gainThreshLong       = (faac_real)TNS_GAIN_THRESHOLD;
        tnsInfo->tnsDisabled          = !hEncoder->config.useTns;
    }
}

void TnsEncode(TnsInfo* tnsInfo, int numBands,
               enum WINDOW_TYPE blockType, int* sfbOffsetTable,
               faac_real* spec,
               const float* tdEnvelope, int tdEnvelopeLen)
{
    tnsInfo->tnsDataPresent = 0;
    tnsInfo->windowData[0].numFilters = 0;
    tnsInfo->windowData[0].coefResolution = DEF_TNS_COEFF_RES;

    /* Long windows only: short-window TNS is not beneficial here. */
    if (tnsInfo->tnsDisabled || blockType == ONLY_SHORT_WINDOW)
        return;

    int startBand = min(tnsInfo->tnsMinBandNumberLong, tnsInfo->tnsMaxBandsLong);
    int stopBand  = min(numBands, tnsInfo->tnsMaxBandsLong);
    startBand = max(min(startBand, numBands), 0);
    stopBand  = max(min(stopBand,  numBands), 0);
    if (stopBand - startBand < 1)
        return;

    int startIndex = sfbOffsetTable[startBand];
    int length     = sfbOffsetTable[stopBand] - startIndex;
    if (length < 2)
        return;

    const faac_real * restrict band = &spec[startIndex];

    faac_real energy = 0.0;
    for (int i = 0; i < length; i++)
        energy += band[i] * band[i];
    if (energy < (faac_real)TNS_ENERGY_FLOOR)
        return;

    /* LPC analysis over the TNS band. */
    int order = min(tnsInfo->tnsMaxOrderLong, length - 1);
    faac_real r[TNS_MAX_ORDER + 1];
    faac_real k[TNS_MAX_ORDER + 1];

    Autocorrelation(order, length, band, r);
    faac_real gain = LevinsonDurbin(order, r, k);
    if (gain < tnsInfo->gainThreshLong)
        return;

    TnsFilterData *filter = &tnsInfo->windowData[0].tnsFilter[0];

    QuantizeReflectionCoeffs(order, DEF_TNS_COEFF_RES, k, filter->index);
    order = TruncateOrder(order, (faac_real)DEF_TNS_COEFF_THRESH, k);
    if (order == 0)
        return;

    /* coefCompress: 1 when every index fits in (res-1) bits. */
    int compress = 1;
    int limit = 1 << (DEF_TNS_COEFF_RES - 2);
    for (int i = 1; i <= order; i++) {
        if (filter->index[i] < -limit || filter->index[i] >= limit) {
            compress = 0;
            break;
        }
    }

    StepUp(order, k, filter->aCoeffs);

    filter->order        = order;
    filter->length       = stopBand - startBand;
    filter->startBand    = startBand;
    filter->stopBand     = stopBand;
    filter->coefCompress = compress;
    filter->direction    = ChooseDirection(tdEnvelope, tdEnvelopeLen);

    TnsInvFilter(length, order, filter->direction, filter->aCoeffs, &spec[startIndex]);

    tnsInfo->windowData[0].numFilters = 1;
    tnsInfo->tnsDataPresent = 1;
}
