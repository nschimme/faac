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
 * Temporal Noise Shaping (TNS) for MPEG-4 AAC-LC.
 *
 * TNS whitens the spectrum in the frequency domain with a linear-predictive
 * (LPC) filter. Because frequency and time are duals, a whitening filter in
 * frequency shapes the quantization-noise envelope in time; matching that
 * envelope to the signal's temporal envelope hides noise that would otherwise
 * be audible as pre-echo ahead of sharp transients.
 *
 * The analysis stages use float arithmetic, which is accurate enough for a
 * whitening filter and vectorizes well.
 */

#include <math.h>
#include <stdlib.h>

#include <string.h>
#include "frame.h"
#include "coder.h"
#include "bitstream.h"
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

#define TNS_ATTACK_RATIO    1.4f    /* Min peak/mean sub-block energy to fire TNS */
#define TNS_LPC_ORDER       8       /* Standard order for AAC-LC long windows */
#define TNS_GAIN_LIMIT      1.4f    /* Minimum prediction gain for TNS usage */
#define TNS_GAIN_CLAMP      6.0f    /* Reject above: poles near the unit circle
                                       make the synthesis filter ring (noise blowup) */
#define TNS_MEASURED_GAIN   1.4f    /* Outcome gate: the quantized filter must
                                       reduce band energy by at least this factor */
#define TNS_MIN_ENERGY      1e-9f   /* Energy floor for spectral analysis */

/* Autocorrelation over the TNS band, lags 0..order. The float scratch copy
   keeps the inner dot product in single precision so it vectorizes cleanly. */
static void calc_autocorr_f(int order, int length,
                            const float * work,
                            float * r)
{
    int lag, i;

    for (lag = 0; lag <= order; lag++) {
        float acc = 0.0f;
        const float * p1 = work;
        const float * p2 = work + lag;
        int n = length - lag;

        for (i = 0; i < n; i++) {
            acc += p1[i] * p2[i];
        }
        r[lag] = acc;
    }
}

/* Levinson-Durbin recursion: reflection coeffs k[] from autocorrelation r[].
   Returns the prediction gain (r[0]/residual), the profit signal for the gate. */
static float compute_lpc(int order, const float * r,
                         float * k)
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
        /* Clamp inside the unit circle so the synthesis filter stays stable. */
        if (rc > 0.999f) rc = 0.999f;
        else if (rc < -0.999f) rc = -0.999f;
        k[i] = rc;

        /* Symmetric in-place update touches each a[] pair once. */
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

    if (err <= 1e-15f) return 100.0f; /* residual underflow: treat as huge gain */

    return r[0] / err;
}

/* Quantize the reflection coeffs to transmission indices. AAC quantizes in the
   arcsine domain, where coeffs near +/-1 (the perceptually sensitive ones) get
   finer steps. k[] is overwritten with the requantized values so the encoder
   filters with exactly what the decoder will reconstruct. */
static void quantize_coeffs(int order, int res, float * k, int * idx)
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

        s = (q >= 0) ? s_p : s_n;
        k[i] = sinf((float)q / s);
    }
}

/* Reflection coeffs -> FIR predictor taps, same symmetric step as the LPC
   recursion but run once on the final (quantized) k[]. */
static void finalize_filter(int order, const float * k, faac_real * a)
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

/* In-place TNS analysis FIR across the band. Snapshot the input first so the
   delay line reads original samples directly instead of shifting state each
   step; direction picks which end of the band the prediction leans on. */
static void filter_spec(int length, int order, int direction,
                        const faac_real * a, faac_real * spec)
{
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

    int gated = !hEncoder->config.useTns;
    hEncoder->config.useTns = !gated;

    for (ch = 0; ch < hEncoder->numChannels; ch++) {
        TnsInfo *info = &hEncoder->coderInfo[ch].tnsInfo;
        info->tnsDisabled = gated;
        info->tnsMaxBandsLong = tns_sfb_range[fs].max;
        info->tnsNumSwbLong = hEncoder->srInfo->num_cb_long;
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

    /* TNS is currently limited to long windows to prioritize efficiency and
       because short-block transients are naturally handled by the filterbank's
       higher temporal resolution. */
    if (tnsInfo->tnsDisabled || blockType == ONLY_SHORT_WINDOW) return;

    /* Tonal but temporally flat frames pass the gain gate too; without a real
       transient to hide behind, whitening just smears noise as reverb. This
       cheap early-out also skips the LPC work on the common path. */
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

    /* Fit the LPC on an energy-normalized copy of the band: each sfb scaled to
       unit RMS (floored so near-silent bands can't blow up a bin). Without
       this the fit is dominated by the loudest harmonics and the filter
       whitens tonality instead of modeling the temporal envelope. */
    float wspec[BLOCK_LEN_LONG];
    {
        float maxrms = 0.0f, floorrms;
        int b;
        for (b = b_start; b < b_stop; b++) {
            int s0 = sfbOffsetTable[b], s1 = sfbOffsetTable[b + 1];
            float e = 0.0f;
            for (int i = s0; i < s1; i++) e += (float)(spec[i] * spec[i]);
            float rms = sqrtf(e / (float)(s1 - s0));
            if (rms > maxrms) maxrms = rms;
        }
        floorrms = maxrms * 0.01f;
        if (floorrms < 1e-9f) floorrms = 1e-9f;
        for (b = b_start; b < b_stop; b++) {
            int s0 = sfbOffsetTable[b], s1 = sfbOffsetTable[b + 1];
            float e = 0.0f;
            for (int i = s0; i < s1; i++) e += (float)(spec[i] * spec[i]);
            float rms = sqrtf(e / (float)(s1 - s0));
            float wgt = 1.0f / (rms > floorrms ? rms : floorrms);
            for (int i = s0; i < s1; i++) wspec[i - i_start] = (float)spec[i] * wgt;
        }
    }

    float r[TNS_MAX_ORDER + 1] = {0};
    float k[TNS_MAX_ORDER + 1] = {0};

    calc_autocorr_f(TNS_LPC_ORDER, length, wspec, r);
    float gain = compute_lpc(TNS_LPC_ORDER, r, k);

    if (gain < TNS_GAIN_LIMIT || gain > TNS_GAIN_CLAMP) return;

    TnsFilterData *filter = &tnsInfo->windowData[0].tnsFilter[0];
    quantize_coeffs(TNS_LPC_ORDER, DEF_TNS_COEFF_RES, k, filter->index);

    /* Trim filter order based on coefficient significance */
    int order = TNS_LPC_ORDER;
    while (order > 0 && fabsf(k[order]) < (float)DEF_TNS_COEFF_THRESH) order--;
    if (order == 0) return;

    filter->order = order;
    /* The decoder anchors the filter region at the TOP — its num_swb, the FULL
       swb table count for the sample rate, not max_sfb — and walks DOWN by
       `length`, clamping both ends to min(tns_max_band, max_sfb). For its
       region to land exactly on [b_start, b_stop], length must span from that
       table top down to b_start — not just the bands we filtered. */
    filter->length = tnsInfo->tnsNumSwbLong - b_start;

    /* Direction heuristic: run the filter in the direction of the signal's
       temporal energy so the shaped quantization noise lands in the loud,
       masked part of the frame instead of spilling into the quiet part.
       Backward filtering when the early half of the frame carries the energy. */
    if (tdEnvelope && tdEnvelopeLen >= 2) {
        float e_early = 0.0f, e_late = 0.0f;
        int w, half = tdEnvelopeLen / 2;
        for (w = 0; w < half; w++) e_early += tdEnvelope[w];
        for (; w < tdEnvelopeLen; w++) e_late += tdEnvelope[w];
        filter->direction = (e_early > e_late) ? 1 : 0;
    } else {
        filter->direction = 0;
    }
    /* Narrower encoding saves a bit per coefficient when it's lossless. */
    filter->coefCompress = 1;
    int limit = 1 << (DEF_TNS_COEFF_RES - 2);
    for (int i = 1; i <= order; i++) {
        if (filter->index[i] < -limit || filter->index[i] >= limit) {
            filter->coefCompress = 0;
            break;
        }
    }

    finalize_filter(order, k, filter->aCoeffs);

    /* Outcome gate, measured in the same energy-normalized domain the filter
       was fitted in: filter a scratch copy with the QUANTIZED coefficients in
       the chosen direction and keep TNS only if it actually reduced weighted
       energy by TNS_MEASURED_GAIN. The pre-quantization Levinson estimate can
       overpromise; a filter that fails this bar just smears noise. */
    {
        faac_real trial[BLOCK_LEN_LONG] = {0};
        faac_real orig_e = 0.0, filt_e = 0.0;
        int i;
        for (i = 0; i < length; i++) trial[i] = (faac_real)wspec[i];

        filter_spec(length, order, filter->direction, filter->aCoeffs, trial);
        for (i = 0; i < length; i++) {
            orig_e += (faac_real)wspec[i] * (faac_real)wspec[i];
            filt_e += trial[i] * trial[i];
        }
        if (filt_e < (faac_real)TNS_MIN_ENERGY) filt_e = (faac_real)TNS_MIN_ENERGY;
        if (orig_e < (faac_real)TNS_MEASURED_GAIN * filt_e) return;
    }

    filter_spec(length, order, filter->direction, filter->aCoeffs, band);

    tnsInfo->windowData[0].numFilters = 1;
    tnsInfo->windowData[0].coefResolution = DEF_TNS_COEFF_RES;
    tnsInfo->tnsDataPresent = 1;


}

int TnsWriteBitstream(CoderInfo* coderInfo, BitStream* bitStream, int writeFlag)
{
    TnsInfo *tns = &coderInfo->tnsInfo;

    /* Field widths differ between the eight short windows and the single long
       window; select the short-block set only for an actual short block. */
    const int shortBlock = (coderInfo->block_type == ONLY_SHORT_WINDOW);
    const int numWindows = shortBlock ? MAX_SHORT_WINDOWS : 1;
    const int nfiltBits  = shortBlock ? LEN_TNS_NFILTS  : LEN_TNS_NFILTL;
    const int lenBits    = shortBlock ? LEN_TNS_LENGTHS : LEN_TNS_LENGTHL;
    const int ordBits    = shortBlock ? LEN_TNS_ORDERS  : LEN_TNS_ORDERL;

    int bits = LEN_TNS_PRES;
    int w;

    if (writeFlag)
        PutBit(bitStream, tns->tnsDataPresent, LEN_TNS_PRES);

    if (!tns->tnsDataPresent)
        return bits;

    for (w = 0; w < numWindows; w++) {
        const TnsWindowData *win = &tns->windowData[w];
        const int nfilt = win->numFilters;
        int f;

        bits += nfiltBits;
        if (writeFlag)
            PutBit(bitStream, nfilt, nfiltBits);

        if (!nfilt)
            continue;

        /* coef_res rides the wire biased by the 3-bit floor the syntax assumes. */
        bits += LEN_TNS_COEFF_RES;
        if (writeFlag)
            PutBit(bitStream, win->coefResolution - DEF_TNS_RES_OFFSET, LEN_TNS_COEFF_RES);

        for (f = 0; f < nfilt; f++) {
            const TnsFilterData *filt = &win->tnsFilter[f];
            const int order = filt->order;

            bits += lenBits + ordBits;
            if (writeFlag) {
                PutBit(bitStream, filt->length, lenBits);
                PutBit(bitStream, order, ordBits);
            }

            if (!order)
                continue;

            /* coefCompress drops the redundant MSB when every index fits the
               narrower range; the decoder sign-extends back to coef_res width. */
            const int coefBits = win->coefResolution - filt->coefCompress;
            const unsigned long mask = ~(~0UL << coefBits);
            int i;

            bits += LEN_TNS_DIRECTION + LEN_TNS_COMPRESS + order * coefBits;
            if (writeFlag) {
                PutBit(bitStream, filt->direction, LEN_TNS_DIRECTION);
                PutBit(bitStream, filt->coefCompress, LEN_TNS_COMPRESS);
                for (i = 1; i <= order; i++)
                    PutBit(bitStream, (unsigned long)filt->index[i] & mask, coefBits);
            }
        }
    }

    return bits;
}
