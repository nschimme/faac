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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "frame.h"
#include "coder.h"
#include "bitstream.h"
#include "tns.h"
#include "util.h"

static const struct {
    unsigned char min;
    unsigned char max;
} tns_sfb_range[12] = {
    {11, 31}, {12, 31}, {15, 34}, {16, 40}, {17, 42}, {20, 51},
    {25, 46}, {26, 46}, {24, 42}, {28, 42}, {30, 42}, {31, 39}
};

#define TNS_ATTACK_RATIO    1.4f
#define TNS_LPC_ORDER       8
#define TNS_GAIN_LIMIT      1.4f
#define TNS_GAIN_CLAMP      6.0f
#define TNS_MEASURED_GAIN   1.4f
#define TNS_MIN_ENERGY      1e-9f
/* Spectral flatness measure (geomean/arithmean of per-band RMS across the TNS
 * SFB range) of ~1.0 means the band is noise-like -- PNS (quantize.c) is
 * about to substitute it with a random-noise codebook entry anyway, so the
 * LPC whitening filter TNS would spend bits and CPU computing here buys
 * nothing. Skip the (expensive) autocorr/LPC/quantize path in that case.
 * Bands below this are tonal/peaky, where TNS still has real work to do. */
#define TNS_PNS_SFM_SKIP    0.85f

static void calc_autocorr_f(int order, int length, const float * work, float * r) {
    int lag, i;
    for (lag = 0; lag <= order; lag++) {
        float acc = 0.0f;
        const float * p1 = work;
        const float * p2 = work + lag;
        int n = length - lag;
        for (i = 0; i < n; i++) acc += p1[i] * p2[i];
        r[lag] = acc;
    }
}

static float compute_lpc(int order, const float * r, float * k) {
    float a[TNS_MAX_ORDER + 1]; float err; int i, j;
    if (r[0] <= 0.0f) { for (i = 1; i <= order; i++) k[i] = 0.0f; return 1.0f; }
    err = r[0]; a[0] = 1.0f;
    for (i = 1; i <= order; i++) {
        float lambda = r[i];
        for (j = 1; j < i; j++) lambda += a[j] * r[i - j];
        if (err <= 0.0f) { for (; i <= order; i++) k[i] = 0.0f; break; }
        float rc = -lambda / err;
        if (rc > 0.999f) rc = 0.999f; else if (rc < -0.999f) rc = -0.999f;
        k[i] = rc;
        int half = (i + 1) / 2;
        for (j = 1; j < half; j++) {
            float t1 = a[j]; float t2 = a[i - j];
            a[j] = t1 + rc * t2; a[i - j] = t2 + rc * t1;
        }
        if (i % 2 == 0) a[i / 2] += rc * a[i / 2];
        a[i] = rc;
        err *= (1.0f - rc * rc);
        if (err <= 0.0f) break;
    }
    return (err <= 1e-15f) ? 100.0f : r[0] / err;
}

static void quantize_coeffs(int order, int res, float * k, int * idx) {
    const float s_p = (float)(((1 << (res - 1)) - 0.5f) / (M_PI / 2));
    const float s_n = (float)(((1 << (res - 1)) + 0.5f) / (M_PI / 2));
    const int i_max = (1 << (res - 1)) - 1;
    const int i_min = -(1 << (res - 1));
    for (int i = 1; i <= order; i++) {
        float val = k[i]; float s = (val >= 0.0f) ? s_p : s_n;
        int q = (int)(asinf(val) * s + ((val >= 0.0f) ? 0.5f : -0.5f));
        if (q > i_max) q = i_max; else if (q < i_min) q = i_min;
        idx[i] = q; s = (q >= 0) ? s_p : s_n; k[i] = sinf((float)q / s);
    }
}

static void finalize_filter(int order, const float * k, faac_real * a) {
    int i, m; a[0] = 1.0;
    for (m = 1; m <= order; m++) {
        faac_real km = (faac_real)k[m]; int half = (m + 1) / 2;
        for (i = 1; i < half; i++) {
            faac_real t1 = a[i]; faac_real t2 = a[m - i];
            a[i] = t1 + km * t2; a[m - i] = t2 + km * t1;
        }
        if (m % 2 == 0) a[m / 2] += km * a[m / 2];
        a[m] = km;
    }
}

static void filter_spec(int length, int order, int direction, const faac_real * a, faac_real * spec) {
    faac_real hist[BLOCK_LEN_LONG]; int i, j;
    memcpy(hist, spec, length * sizeof(faac_real));
    if (direction) {
        for (i = length - 1; i >= 0; i--) {
            faac_real acc = hist[i]; int jmax = min(order, length - 1 - i);
            for (j = 1; j <= jmax; j++) acc += a[j] * hist[i + j];
            spec[i] = acc;
        }
    } else {
        for (i = 0; i < length; i++) {
            faac_real acc = hist[i]; int jmax = min(order, i);
            for (j = 1; j <= jmax; j++) acc += a[j] * hist[i - j];
            spec[i] = acc;
        }
    }
}

void TnsInit(faacEncStruct* hEncoder) {
    unsigned int ch; int fs = hEncoder->sampleRateIdx;
    for (ch = 0; ch < hEncoder->numChannels; ch++) {
        TnsInfo *info = &hEncoder->coderInfo[ch].tnsInfo;
        info->tnsDisabled = !hEncoder->config.useTns;
        info->tnsMaxBandsLong = tns_sfb_range[fs].max;
        info->tnsNumSwbLong = hEncoder->srInfo->num_cb_long;
        info->tnsMinBandNumberLong = tns_sfb_range[fs].min;
        info->tnsMaxOrderLong = TNS_LPC_ORDER;
        info->gainThreshLong = (faac_real)TNS_GAIN_LIMIT;
    }
}

void TnsEncode(TnsInfo* tnsInfo, int numBands, enum WINDOW_TYPE blockType, int* sfbOffsetTable, faac_real* spec, const float* tdEnvelope, int tdEnvelopeLen) {
    tnsInfo->tnsDataPresent = 0; tnsInfo->windowData[0].numFilters = 0;
    if (tnsInfo->tnsDisabled || blockType == ONLY_SHORT_WINDOW) return;
    if (tdEnvelope && tdEnvelopeLen > 0) {
        float peak = 0.0f, mean = 0.0f; int w;
        for (w = 0; w < tdEnvelopeLen; w++) {
            float e = tdEnvelope[w]; mean += e; if (e > peak) peak = e;
        }
        mean /= (float)tdEnvelopeLen; if (peak < TNS_ATTACK_RATIO * mean) return;
    }
    int b_start = min(tnsInfo->tnsMinBandNumberLong, numBands);
    int b_stop = min(tnsInfo->tnsMaxBandsLong, numBands);
    if (b_stop <= b_start) return;
    int i_start = sfbOffsetTable[b_start]; int length = sfbOffsetTable[b_stop] - i_start;
    if (length <= TNS_LPC_ORDER) return;
    faac_real *band = spec + i_start; faac_real energy = 0.0;
    for (int i = 0; i < length; i++) energy += band[i] * band[i];
    if (energy < (faac_real)TNS_MIN_ENERGY) return;
    float wspec[BLOCK_LEN_LONG]; {
        float maxrms = 0.0f, floorrms; int b;
        float sum_rms = 0.0f, sum_log_rms = 0.0f;
        int nbands = b_stop - b_start;
        for (b = b_start; b < b_stop; b++) {
            int s0 = sfbOffsetTable[b], s1 = sfbOffsetTable[b + 1]; float e = 0.0f;
            for (int i = s0; i < s1; i++) e += (float)(spec[i] * spec[i]);
            float rms = sqrtf(e / (float)(s1 - s0)); if (rms > maxrms) maxrms = rms;
            float rms_fl = rms > 1e-9f ? rms : 1e-9f;
            sum_rms += rms_fl; sum_log_rms += logf(rms_fl);
        }
        {
            float sfm = expf(sum_log_rms / (float)nbands) / (sum_rms / (float)nbands);
#ifdef FAAC_TNS_STATS
            static int stats_env = -1;
            if (stats_env < 0) stats_env = getenv("FAAC_TNS_STATS") ? 1 : 0;
            if (stats_env) fprintf(stderr, "tns_sfm %f skip %d\n", sfm, sfm > TNS_PNS_SFM_SKIP);
#endif
            if (sfm > TNS_PNS_SFM_SKIP) return;
        }
        floorrms = maxrms * 0.01f; if (floorrms < 1e-9f) floorrms = 1e-9f;
        for (b = b_start; b < b_stop; b++) {
            int s0 = sfbOffsetTable[b], s1 = sfbOffsetTable[b + 1]; float e = 0.0f;
            for (int i = s0; i < s1; i++) e += (float)(spec[i] * spec[i]);
            float rms = sqrtf(e / (float)(s1 - s0));
            float wgt = 1.0f / (rms > floorrms ? rms : floorrms);
            for (int i = s0; i < s1; i++) wspec[i - i_start] = (float)spec[i] * wgt;
        }
    }
    float r[TNS_MAX_ORDER + 1] = {0}; float k[TNS_MAX_ORDER + 1] = {0};
    calc_autocorr_f(TNS_LPC_ORDER, length, wspec, r);
    float gain = compute_lpc(TNS_LPC_ORDER, r, k);
    if (gain < TNS_GAIN_LIMIT || gain > TNS_GAIN_CLAMP) return;
    TnsFilterData *filter = &tnsInfo->windowData[0].tnsFilter[0];
    quantize_coeffs(TNS_LPC_ORDER, DEF_TNS_COEFF_RES, k, filter->index);
    int order = TNS_LPC_ORDER;
    while (order > 0 && fabsf(k[order]) < (float)DEF_TNS_COEFF_THRESH) order--;
    if (order == 0) return;
    filter->order = order; filter->length = tnsInfo->tnsNumSwbLong - b_start;
    if (tdEnvelope && tdEnvelopeLen >= 2) {
        float e_early = 0.0f, e_late = 0.0f; int w, half = tdEnvelopeLen / 2;
        for (w = 0; w < half; w++) e_early += tdEnvelope[w];
        for (; w < tdEnvelopeLen; w++) e_late += tdEnvelope[w];
        filter->direction = (e_early > e_late) ? 1 : 0;
    } else filter->direction = 0;
    filter->coefCompress = 1; int limit = 1 << (DEF_TNS_COEFF_RES - 2);
    for (int i = 1; i <= order; i++) {
        if (filter->index[i] < -limit || filter->index[i] >= limit) { filter->coefCompress = 0; break; }
    }
    finalize_filter(order, k, filter->aCoeffs);
    {
        faac_real trial[BLOCK_LEN_LONG] = {0}; faac_real orig_e = 0.0, filt_e = 0.0; int i;
        for (i = 0; i < length; i++) trial[i] = (faac_real)wspec[i];
        filter_spec(length, order, filter->direction, filter->aCoeffs, trial);
        for (i = 0; i < length; i++) { orig_e += (faac_real)wspec[i] * (faac_real)wspec[i]; filt_e += trial[i] * trial[i]; }
        if (filt_e < (faac_real)TNS_MIN_ENERGY) filt_e = (faac_real)TNS_MIN_ENERGY;
        if (orig_e < (faac_real)TNS_MEASURED_GAIN * filt_e) return;
    }
    filter_spec(length, order, filter->direction, filter->aCoeffs, band);
    tnsInfo->windowData[0].numFilters = 1; tnsInfo->windowData[0].coefResolution = DEF_TNS_COEFF_RES;
    tnsInfo->tnsDataPresent = 1;
}

int TnsWriteBitstream(CoderInfo* coderInfo, BitStream* bitStream, int writeFlag) {
    TnsInfo *tns = &coderInfo->tnsInfo;
    const int shortBlock = (coderInfo->block_type == ONLY_SHORT_WINDOW);
    const int numWindows = shortBlock ? MAX_SHORT_WINDOWS : 1;
    const int nfiltBits = shortBlock ? LEN_TNS_NFILTS : LEN_TNS_NFILTL;
    const int lenBits = shortBlock ? LEN_TNS_LENGTHS : LEN_TNS_LENGTHL;
    const int ordBits = shortBlock ? LEN_TNS_ORDERS : LEN_TNS_ORDERL;
    int bits = LEN_TNS_PRES; int w;
    if (writeFlag) PutBit(bitStream, tns->tnsDataPresent, LEN_TNS_PRES);
    if (!tns->tnsDataPresent) return bits;
    for (w = 0; w < numWindows; w++) {
        const TnsWindowData *win = &tns->windowData[w]; const int nfilt = win->numFilters; int f;
        bits += nfiltBits; if (writeFlag) PutBit(bitStream, nfilt, nfiltBits);
        if (!nfilt) continue;
        bits += LEN_TNS_COEFF_RES;
        if (writeFlag) PutBit(bitStream, win->coefResolution - DEF_TNS_RES_OFFSET, LEN_TNS_COEFF_RES);
        for (f = 0; f < nfilt; f++) {
            const TnsFilterData *filt = &win->tnsFilter[f]; const int order = filt->order;
            bits += lenBits + ordBits;
            if (writeFlag) { PutBit(bitStream, filt->length, lenBits); PutBit(bitStream, order, ordBits); }
            if (!order) continue;
            const int coefBits = win->coefResolution - filt->coefCompress;
            const unsigned long mask = ~(~0UL << coefBits); int i;
            bits += LEN_TNS_DIRECTION + LEN_TNS_COMPRESS + order * coefBits;
            if (writeFlag) {
                PutBit(bitStream, filt->direction, LEN_TNS_DIRECTION);
                PutBit(bitStream, filt->coefCompress, LEN_TNS_COMPRESS);
                for (i = 1; i <= order; i++) PutBit(bitStream, (unsigned long)filt->index[i] & mask, coefBits);
            }
        }
    }
    return bits;
}
