/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Nils Schimmelmann
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
 */

#include <math.h>
#include <string.h>
#include "frame.h"
#include "coder.h"
#include "tns.h"
#include "util.h"

/* Per-sample-rate scalefactor-band range TNS is allowed to filter over, from
 * ISO/IEC 13818-7/14496-3's TNS tool tables (indexed by sampleRateIdx). Not
 * an original heuristic: this is the spec's fixed table. */
static const struct {
    uint8_t min;
    uint8_t max;
} tns_sfb_range[12] = {
    {11, 31}, {12, 31}, {15, 34}, {16, 40}, {17, 42}, {20, 51},
    {25, 46}, {26, 46}, {24, 42}, {28, 42}, {30, 42}, {31, 39}
};

#define TNS_MIN_ENERGY      1e-9f
#define TNS_PNS_SFM_SKIP    0.85f

static inline int TnsMaxOrder(unsigned long bitRate)
{
    if (bitRate > 0 && bitRate <= 32000) return 6;
    if (bitRate > 0 && bitRate <= 64000) return 8;
    return 12;
}

static inline float TnsGainLimit(unsigned long bitRate)
{
    if (bitRate > 0 && bitRate <= 32000) return 1.50f;
    if (bitRate > 0 && bitRate <= 64000) return 1.40f;
    return 1.35f;
}

static void calc_autocorr_f(int order, int length, const float * work, float * r)
{
    int lag, i;

    for (lag = 0; lag <= order; lag++) {
        float acc = 0.0f;
        const float * p1 = work;
        const float * p2 = work + lag;
        int n = length - lag;

        for (i = 0; i < n; i++)
            acc += p1[i] * p2[i];
        r[lag] = acc;
    }
}

/* Reflection coefficients are clamped to +-0.999, short of the unstable
 * +-1 limit, so the resulting filter is guaranteed stable regardless of
 * input. The returned prediction gain (r[0]/final residual error) lets
 * TnsEncode reject a bad fit before spending any bits on coefficients --
 * free to report here, since the recursion already has both numbers. */
static float compute_lpc(int order, const float * r, float * k)
{
    float a[TNS_MAX_ORDER + 1];
    float err;
    int i, j;

    if (r[0] <= 0.0f) {
        for (i = 1; i <= order; i++)
            k[i] = 0.0f;
        return 1.0f;
    }

    err = r[0];
    a[0] = 1.0f;
    for (i = 1; i <= order; i++) {
        float lambda = r[i];
        float rc;
        int half;

        for (j = 1; j < i; j++)
            lambda += a[j] * r[i - j];

        if (err <= 0.0f) {
            for (; i <= order; i++)
                k[i] = 0.0f;
            break;
        }

        rc = -lambda / err;
        if (rc > 0.999f) rc = 0.999f;
        else if (rc < -0.999f) rc = -0.999f;
        k[i] = rc;

        half = (i + 1) / 2;
        for (j = 1; j < half; j++) {
            float t1 = a[j];
            float t2 = a[i - j];
            a[j] = t1 + rc * t2;
            a[i - j] = t2 + rc * t1;
        }
        if (i % 2 == 0)
            a[i / 2] += rc * a[i / 2];
        a[i] = rc;

        err *= (1.0f - rc * rc);
        if (err <= 0.0f)
            break;
    }

    /* err collapsing to ~0 means a (near-)perfect fit, which for real audio
     * means a degenerate input rather than a genuinely great filter. Report
     * infinite gain so the caller's isfinite() check rejects it, instead of
     * dividing by ~0. */
    if (err <= TNS_MIN_ENERGY)
        return INFINITY;
    return r[0] / err;
}

/* Reflection coefficients live in (-1, 1); arcsine-warp them before
 * quantizing so equal code steps land closer to equal perceptual steps
 * near the +-1 ends, per the tns_data() coefficient law in the spec. */
static void quantize_coeffs(int order, int res, float * k, int * idx)
{
    const float s_p = (float)(((1 << (res - 1)) - 0.5f) / (M_PI / 2));
    const float s_n = (float)(((1 << (res - 1)) + 0.5f) / (M_PI / 2));
    const int i_max = (1 << (res - 1)) - 1;
    const int i_min = -(1 << (res - 1));
    int i;

    for (i = 1; i <= order; i++) {
        float val = k[i];
        float s = (val >= 0.0f) ? s_p : s_n;
        int q = (int)(asinf(val) * s + ((val >= 0.0f) ? 0.5f : -0.5f));

        if (q > i_max) q = i_max;
        else if (q < i_min) q = i_min;
        idx[i] = q;

        /* Re-derive k[] from the quantized index (not the original float)
         * so finalize_filter below builds the same filter the decoder will,
         * from the same lossy coefficients that actually get transmitted. */
        s = (q >= 0) ? s_p : s_n;
        k[i] = sinf((float)q / s);
    }
}

/* filter_spec's recursion needs direct-form polynomial coefficients, not
 * reflection coefficients, so this runs the standard lattice-to-direct
 * step-up. Called after quantize_coeffs so the polynomial matches what's
 * actually transmitted, not the pre-quantization float values. */
static void finalize_filter(int order, const float * k, float * a)
{
    int i, m;

    a[0] = 1.0f;
    for (m = 1; m <= order; m++) {
        float km = k[m];
        int half = (m + 1) / 2;

        for (i = 1; i < half; i++) {
            float t1 = a[i];
            float t2 = a[m - i];
            a[i] = t1 + km * t2;
            a[m - i] = t2 + km * t1;
        }
        if (m % 2 == 0)
            a[m / 2] += km * a[m / 2];
        a[m] = km;
    }
}

/* direction picks which end of the band the recursion runs from: TNS wants
 * the prediction to run towards the transient (so quantization noise piles
 * up where it'll be masked), and the transient can sit at either edge of
 * the analysis window. */
static void filter_spec(int length, int order, int direction, const float * a, float * spec)
{
    float hist[BLOCK_LEN_LONG];
    int i, j;

    memcpy(hist, spec, length * sizeof(float));
    if (direction) {
        for (i = length - 1; i >= 0; i--) {
            float acc = hist[i];
            int jmax = min(order, length - 1 - i);

            for (j = 1; j <= jmax; j++)
                acc += a[j] * hist[i + j];
            spec[i] = acc;
        }
    } else {
        for (i = 0; i < length; i++) {
            float acc = hist[i];
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

    for (ch = 0; ch < hEncoder->numChannels; ch++) {
        TnsInfo *info = &hEncoder->coderInfo[ch].tnsInfo;

        info->tnsMaxBandsLong = tns_sfb_range[fs].max;
        info->tnsNumSwbLong = hEncoder->srInfo->num_cb_long;
        info->tnsMinBandNumberLong = tns_sfb_range[fs].min;
    }
}

/* Fits one TNS filter over scalefactor bands [b_start, b_stop) and, if it
 * earns its place, whitens that range of `spec` in place and fills *filter.
 * Returns 1 when a filter was written, 0 otherwise. */
static int tns_fit_range(int b_start, int b_stop, int *sfbOffsetTable,
                         float *spec, TnsFilterData *filter, unsigned long bitRate,
                         float *workBuff)
{
    int i_start = sfbOffsetTable[b_start];
    int length = sfbOffsetTable[b_stop] - i_start;
    float r[TNS_MAX_ORDER + 1] = {0};
    float k[TNS_MAX_ORDER + 1] = {0};
    float gain, energy;
    int max_order = TnsMaxOrder(bitRate);
    float gain_limit = TnsGainLimit(bitRate);
    int order, limit, i;

    if (length <= max_order)
        return 0;

    energy = 0.0f;
    for (i = 0; i < length; i++)
        energy += spec[i_start + i] * spec[i_start + i];
    if (energy < TNS_MIN_ENERGY)
        return 0;

    calc_autocorr_f(max_order, length, spec + i_start, r);
    gain = compute_lpc(max_order, r, k);
    if (gain < gain_limit || !isfinite(gain))
        return 0;

    quantize_coeffs(max_order, DEF_TNS_COEFF_RES, k, filter->index);

    order = max_order;
    while (order > 0 && fabsf(k[order]) < (float)DEF_TNS_COEFF_THRESH)
        order--;
    if (order == 0)
        return 0;

    filter->order = order;
    filter->direction = 0;

    filter->coefCompress = 1;
    limit = 1 << (DEF_TNS_COEFF_RES - 2);
    for (i = 1; i <= order; i++) {
        if (filter->index[i] < -limit || filter->index[i] >= limit) {
            filter->coefCompress = 0;
            break;
        }
    }

    finalize_filter(order, k, filter->aCoeffs);

    {
        float *trial = workBuff ? workBuff : spec + i_start;
        float orig_e = 0.0f, filt_e = 0.0f;
        float *band = spec + i_start;

        if (workBuff)
            memcpy(trial, band, length * sizeof(float));

        filter_spec(length, order, filter->direction, filter->aCoeffs, trial);

        for (i = 0; i < length; i++) {
            orig_e += band[i] * band[i];
            filt_e += trial[i] * trial[i];
        }

        if (filt_e < TNS_MIN_ENERGY)
            filt_e = TNS_MIN_ENERGY;
        if (orig_e < gain_limit * filt_e)
            return 0;

        if (workBuff)
            memcpy(band, trial, length * sizeof(float));
    }

    return 1;
}

void TnsEncodeElement(TnsInfo** tnsInfos, float** specs, int nch,
                      int numBands, enum WINDOW_TYPE blockType,
                      int* sfbOffsetTable, unsigned long bitRate,
                      float* workBuff)
{
    int b_start, b_stop, ch;

    for (ch = 0; ch < nch; ch++) {
        tnsInfos[ch]->tnsDataPresent = 0;
        tnsInfos[ch]->windowData.numFilters = 0;
    }

    if (blockType == ONLY_SHORT_WINDOW)
        return;

    b_start = min(tnsInfos[0]->tnsMinBandNumberLong, numBands);
    b_stop = min(tnsInfos[0]->tnsMaxBandsLong, numBands);
    if (b_stop <= b_start)
        return;

    for (ch = 0; ch < nch; ch++) {
        TnsInfo *info = tnsInfos[ch];

        if (!tns_fit_range(b_start, b_stop, sfbOffsetTable, specs[ch],
                           &info->windowData.tnsFilter[0], bitRate, workBuff))
            continue;

#ifdef FAAC_STATS
        g_faacStats.longBlocksTNS++;
#endif

        info->windowData.tnsFilter[0].length = info->tnsNumSwbLong - b_start;
        info->windowData.numFilters = 1;
        info->windowData.coefResolution = DEF_TNS_COEFF_RES;
        info->tnsDataPresent = 1;
    }
}
