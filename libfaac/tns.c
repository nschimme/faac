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
#include <stdio.h>
#include "frame.h"
#include "coder.h"
#include "tns.h"
#include "util.h"

/* Per-sample-rate scalefactor-band range TNS is allowed to filter over, from
 * ISO/IEC 13818-7/14496-3's TNS tool tables (indexed by sampleRateIdx). Not
 * an original heuristic: this is the spec's fixed table. */
static const struct {
    unsigned char min;
    unsigned char max;
} tns_sfb_range[12] = {
    {11, 31}, {12, 31}, {15, 34}, {16, 40}, {17, 42}, {20, 51},
    {25, 46}, {26, 46}, {24, 42}, {28, 42}, {30, 42}, {31, 39}
};

#define TNS_LPC_ORDER       8     /* fixed filter order; spec allows up to TNS_MAX_ORDER but higher orders rarely paid for themselves here */
#define TNS_GAIN_LIMIT      1.4f  /* Levinson-Durbin prediction gain below this isn't worth the filter's bit cost */
#define TNS_GAIN_CLAMP      6.0f  /* gain above this means a near-singular fit (e.g. a single strong tone); reject rather than risk an unstable filter */
#define TNS_MEASURED_GAIN   1.4f  /* post-quantization re-check: same bar as TNS_GAIN_LIMIT, applied to the filter actually being transmitted */

/* Below this, a band's spectral energy is indistinguishable from float
 * rounding noise, so there's nothing real for TNS to whiten. Also reused
 * below as the floor for RMS-normalization and the LPC residual check,
 * rather than inventing a separate near-zero constant for each. */
#define TNS_MIN_ENERGY      1e-5f
/* SFM (geomean/arithmean of per-band RMS) near 1.0 means the band is
 * noise-like, which PNS (quantize.c) is about to replace anyway -- skip
 * TNS's LPC work there; it only pays off on tonal/peaky bands. */
#define TNS_PNS_SFM_SKIP    0.85f

/* A frame with no sub-block energy spike has no attack for TNS's noise
 * buildup to hide behind, so the LPC work isn't worth its bit cost. Peak
 * needs to clear this multiple of the mean sub-block energy to count as a
 * real transient rather than run-of-the-mill fluctuation. */
#define TNS_TD_PEAK_GATE    2.0f

static void calc_autocorr_f(int order, int length, const float * __restrict work, float * __restrict r)
{
    int lag, i;

    for (lag = 0; lag <= order; lag++) {
        float acc = 0.0f;
        const float * __restrict p1 = work;
        const float * __restrict p2 = work + lag;
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
     * means a degenerate input rather than a genuinely great filter. Return
     * a gain past TNS_GAIN_CLAMP so the caller's sanity check rejects it,
     * instead of dividing by ~0. */
    if (err <= TNS_MIN_ENERGY)
        return TNS_GAIN_CLAMP + 1.0f;
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
static void filter_spec(int length, int order, int direction, const float * __restrict a, float * __restrict spec)
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
    unsigned long br = hEncoder->config.bitRate;
    int max_order = 8;

    if (br > 0) {
        if (br >= 64000) {
            max_order = 8;
        } else if (br >= 32000) {
            max_order = 4;
        } else {
            max_order = 0; /* Disable TNS below 32 kbps per channel */
        }
    } else {
        /* VBR mode: use 8th-order filters by default */
        max_order = 8;
    }

    for (ch = 0; ch < hEncoder->numChannels; ch++) {
        TnsInfo *info = &hEncoder->coderInfo[ch].tnsInfo;

        info->tnsMaxBandsLong = tns_sfb_range[fs].max;
        info->tnsNumSwbLong = hEncoder->srInfo->num_cb_long;
        info->tnsMinBandNumberLong = tns_sfb_range[fs].min;
        info->tns_max_order = max_order;
    }
}

void TnsEncode(TnsInfo* tnsInfo, int numBands, enum WINDOW_TYPE blockType, int* sfbOffsetTable,
               float* spec, PsyInfo* psyInfo)
{
    int b_start, b_stop, i_start, length;
    float *band, energy;
    float wspec[BLOCK_LEN_LONG];
    float r[TNS_MAX_ORDER + 1] = {0};
    float k[TNS_MAX_ORDER + 1] = {0};
    float gain;
    TnsFilterData *filter;
    int order, limit, i, win;
    int lpc_order = tnsInfo->tns_max_order;

    tnsInfo->tnsDataPresent = 0;
    tnsInfo->windowData.numFilters = 0;

    if (lpc_order <= 0)
        return;

    /* Short windows already have the temporal resolution to not need TNS. */
    if (blockType == ONLY_SHORT_WINDOW)
        return;

    b_start = min(tnsInfo->tnsMinBandNumberLong, numBands);
    b_stop = min(tnsInfo->tnsMaxBandsLong, numBands);
    if (b_stop <= b_start)
        return;

    i_start = sfbOffsetTable[b_start];
    length = sfbOffsetTable[b_stop] - i_start;
    if (length <= lpc_order)
        return;

    band = spec + i_start;
    energy = 0.0f;
    for (i = 0; i < length; i++)
        energy += band[i] * band[i];
    if (energy < TNS_MIN_ENERGY)
        return;

    /* No real attack in this 2048-sample TNS window (previous + current frame, 16 subblocks)
     * means no transient for TNS's noise buildup to hide behind -- skip the LPC work. */
    if (psyInfo && psyInfo->data) {
        psydata_t *psydata = (psydata_t *)psyInfo->data;
        float peak = 0.0f, mean = 0.0f;

        for (i = 0; i < 2 * SUBBLOCKS_PER_FRAME; i++) {
            float eng = (i < SUBBLOCKS_PER_FRAME) ?
                psydata->eng[ENG_WIN_PREV + i] :
                psydata->eng[ENG_WIN_CUR + (i - SUBBLOCKS_PER_FRAME)];
            if (eng > peak) peak = eng;
            mean += eng;
        }
        mean /= (float)(2 * SUBBLOCKS_PER_FRAME);
        if (mean < TNS_MIN_ENERGY || peak < TNS_TD_PEAK_GATE * mean)
            return;
    }

    /* Per-band RMS-normalize before autocorrelation, floored at 1% of the
     * loudest band's RMS. Un-normalized, Levinson-Durbin would fit whatever
     * band has the most energy and ignore quieter ones -- but pre-echo is
     * audible in quiet bands too, so the filter needs to whiten across the
     * whole range, not just the peak. */
    {
        float maxrms = 0.0f, floorrms;
        float sum_rms = 0.0f, sum_log_rms = 0.0f;
        int nbands = b_stop - b_start;
        int b;

        for (b = b_start; b < b_stop; b++) {
            int s0 = sfbOffsetTable[b], s1 = sfbOffsetTable[b + 1];
            float e = 0.0f, rms, rms_fl;

            for (i = s0; i < s1; i++)
                e += (float)(spec[i] * spec[i]);
            rms = sqrtf(e / (float)(s1 - s0));
            if (rms > maxrms) maxrms = rms;

            /* rms_fl keeps logf() away from 0 for silent bands; folded into
             * the same loop as maxrms rather than a second pass. */
            rms_fl = rms > TNS_MIN_ENERGY ? rms : TNS_MIN_ENERGY;
            sum_rms += rms_fl;
            sum_log_rms += logf(rms_fl);
        }

        /* Spectral flatness (geomean/arithmean of per-band RMS) near 1.0
         * means the band is noise-like, which PNS (quantize.c) is about to
         * replace anyway -- skip the LPC work; it only pays off on
         * tonal/peaky bands. */
        if (expf(sum_log_rms / (float)nbands) / (sum_rms / (float)nbands) > TNS_PNS_SFM_SKIP)
            return;

        floorrms = maxrms * 0.01f;
        if (floorrms < TNS_MIN_ENERGY) floorrms = TNS_MIN_ENERGY;

        for (b = b_start; b < b_stop; b++) {
            int s0 = sfbOffsetTable[b], s1 = sfbOffsetTable[b + 1];
            float e = 0.0f, rms, wgt;

            for (i = s0; i < s1; i++)
                e += (float)(spec[i] * spec[i]);
            rms = sqrtf(e / (float)(s1 - s0));
            wgt = 1.0f / (rms > floorrms ? rms : floorrms);
            for (i = s0; i < s1; i++)
                wspec[i - i_start] = (float)spec[i] * wgt;
        }
    }

    /* Decimate the spectral lines to compute autocorrelation.
     * Halves or quarters the autocorrelation loop complexity for massive performance gains,
     * while retaining more than enough resolution for a low-order (8th-order) LPC filter. */
#if FAAC_TNS_DECIMATION > 1
    {
        float dec_wspec[BLOCK_LEN_LONG / FAAC_TNS_DECIMATION];
        int dec_length = length / FAAC_TNS_DECIMATION;
        if (dec_length > lpc_order) {
            for (i = 0; i < dec_length; i++) {
                dec_wspec[i] = wspec[FAAC_TNS_DECIMATION * i];
            }
            calc_autocorr_f(lpc_order, dec_length, dec_wspec, r);
        } else {
            calc_autocorr_f(lpc_order, length, wspec, r);
        }
    }
#else
    calc_autocorr_f(lpc_order, length, wspec, r);
#endif
    gain = compute_lpc(lpc_order, r, k);
    if (gain < TNS_GAIN_LIMIT || gain > TNS_GAIN_CLAMP)
        return;

    filter = &tnsInfo->windowData.tnsFilter[0];
    quantize_coeffs(lpc_order, DEF_TNS_COEFF_RES, k, filter->index);

    /* Drop trailing taps that quantized away to ~nothing: they cost bits
     * without changing what the filter does. */
    order = lpc_order;
    while (order > 0 && fabsf(k[order]) < (float)DEF_TNS_COEFF_THRESH)
        order--;
    if (order == 0)
        return;

    filter->order = order;
    filter->length = tnsInfo->tnsNumSwbLong - b_start;

    /* Determine filter direction dynamically based on the temporal position of the peak energy
     * across the exact 2048-sample MDCT window (previous + current frame, 16 subblocks).
     * Causal/forward prediction (direction=0) is ideal for energy peaks in the second half of the
     * window (current frame), while anti-causal/backward prediction (direction=1) is ideal for
     * energy peaks in the first half of the window (previous frame). */
    filter->direction = 0;
    if (psyInfo && psyInfo->data) {
        psydata_t *psydata = (psydata_t *)psyInfo->data;
        float max_eng = 0.0f;
        int peak_idx = -1;

        for (win = 0; win < 2 * SUBBLOCKS_PER_FRAME; win++) {
            float eng = (win < SUBBLOCKS_PER_FRAME) ?
                psydata->eng[ENG_WIN_PREV + win] :
                psydata->eng[ENG_WIN_CUR + (win - SUBBLOCKS_PER_FRAME)];
            if (eng > max_eng) {
                max_eng = eng;
                peak_idx = win;
            }
        }

        if (peak_idx >= 0 && peak_idx < SUBBLOCKS_PER_FRAME) {
            filter->direction = 1;
        }
    }

    /* Coefficients that all fit in one fewer bit each can be transmitted at
     * reduced resolution; the spec's coefCompress flag signals that. */
    filter->coefCompress = 1;
    limit = 1 << (DEF_TNS_COEFF_RES - 2);
    for (i = 1; i <= order; i++) {
        if (filter->index[i] < -limit || filter->index[i] >= limit) {
            filter->coefCompress = 0;
            break;
        }
    }

    finalize_filter(order, k, filter->aCoeffs);

    /* compute_lpc's gain estimate was on the un-quantized coefficients;
     * quantization can erode it enough that the filter actually being
     * transmitted no longer pays for itself. Re-check on a trial run of the
     * real (quantized) filter before committing to writing it out. */
    {
        float trial[BLOCK_LEN_LONG];
        float orig_e = 0.0f, filt_e = 0.0f;

        memcpy(trial, wspec, length * sizeof(float));
        filter_spec(length, order, filter->direction, filter->aCoeffs, trial);
        for (i = 0; i < length; i++) {
            orig_e += wspec[i] * wspec[i];
            filt_e += trial[i] * trial[i];
        }
        if (filt_e < TNS_MIN_ENERGY)
            filt_e = TNS_MIN_ENERGY;
        if (orig_e < TNS_MEASURED_GAIN * filt_e)
            return;
    }

    filter_spec(length, order, filter->direction, filter->aCoeffs, band);
    tnsInfo->windowData.numFilters = 1;
    tnsInfo->windowData.coefResolution = DEF_TNS_COEFF_RES;
    tnsInfo->tnsDataPresent = 1;
}
