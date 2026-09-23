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
 * ISO/IEC 13818-7 / 14496-3 TNS tool tables (indexed by sampleRateIdx). */
static const struct {
    uint8_t min;
    uint8_t max;
} tns_sfb_range[12] = {
    {11, 31}, {12, 31}, {15, 34}, {16, 40}, {17, 42}, {20, 51},
    {25, 46}, {26, 46}, {24, 42}, {28, 42}, {30, 42}, {31, 39}
};

#define TNS_GAIN_LIMIT      1.25f  /* Base prediction gain threshold */
#define TNS_MIN_ENERGY      1e-9f  /* Minimum energy floor */
#define TNS_PNS_SFM_SKIP    0.85f  /* Skip TNS on noise-like flat spectrums */

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

/* Calculate 1D autocorrelation over length samples for lags 0..order */
static void calc_autocorr(int order, int length, const float * restrict work, float * restrict r)
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

/* Levinson-Durbin recursion for reflection coefficients k[1..order].
 * Clamps k to [-0.999, 0.999] and returns estimated prediction gain. */
static float compute_lpc(int order, const float * r, float * k)
{
    float a[TNS_MAX_ORDER + 1] = {1.0f};
    float a_prev[TNS_MAX_ORDER + 1] = {1.0f};
    float err;
    int i, j;

    if (r[0] <= TNS_MIN_ENERGY) {
        for (i = 1; i <= order; i++)
            k[i] = 0.0f;
        return 1.0f;
    }

    err = r[0];
    a[0] = 1.0f;
    for (i = 1; i <= order; i++) {
        float lambda = r[i];
        float rc;

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

        memcpy(a_prev, a, i * sizeof(float));
        for (j = 1; j < i; j++)
            a[j] = a_prev[j] + rc * a_prev[i - j];
        a[i] = rc;

        err *= (1.0f - rc * rc);
        if (err <= 0.0f)
            break;
    }

    if (err <= TNS_MIN_ENERGY)
        return INFINITY;
    return r[0] / err;
}

/* Arcsine quantization of reflection coefficients k into index array idx at target bit resolution res. */
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

        s = (q >= 0) ? s_p : s_n;
        k[i] = sinf((float)q / s);
    }
}

/* Step-up lattice conversion from reflection coefficients k to direct-form FIR a. */
static void finalize_filter(int order, const float * k, float * a)
{
    float a_prev[TNS_MAX_ORDER + 1] = {1.0f};
    int i, m;

    a[0] = 1.0f;
    for (m = 1; m <= order; m++) {
        float km = k[m];

        memcpy(a_prev, a, m * sizeof(float));
        for (i = 1; i < m; i++)
            a[i] = a_prev[i] + km * a_prev[m - i];
        a[m] = km;
    }
}

/* Perform trial FIR filtering on wspec and evaluate residual energies in
 * forward (dir 0) and backward (dir 1) directions using scalar accumulators
 * without extra array stack allocations.
 * Returns chosen direction (0 or 1) and sets *p_filt_e to the minimum residual energy. */
static int evaluate_filtering_direction(int length, int order,
                                         const float * restrict a,
                                         const float * restrict wspec,
                                         float * restrict p_filt_e)
{
    float e_fwd = 0.0f, e_bwd = 0.0f;
    int i, j;

    /* Forward FIR prediction energy */
    for (i = 0; i < length; i++) {
        float acc = wspec[i];
        int limit = i < order ? i : order;
        for (j = 1; j <= limit; j++)
            acc += a[j] * wspec[i - j];
        e_fwd += acc * acc;
    }

    /* Backward FIR prediction energy */
    for (i = length - 1; i >= 0; i--) {
        float acc = wspec[i];
        int limit = (length - 1 - i) < order ? (length - 1 - i) : order;
        for (j = 1; j <= limit; j++)
            acc += a[j] * wspec[i + j];
        e_bwd += acc * acc;
    }

    if (e_fwd <= e_bwd) {
        *p_filt_e = e_fwd;
        return 0;
    } else {
        *p_filt_e = e_bwd;
        return 1;
    }
}

/* Filter spectrum segment in-place using FIR polynomial aCoeffs in direction dir */
static void apply_tns_filter(int length, int order, int dir,
                             const float * restrict a,
                             float * restrict band,
                             float * restrict trial)
{
    int i, j;

    if (dir == 0) {
        for (i = 0; i < length; i++) {
            float acc = band[i];
            int limit = i < order ? i : order;
            for (j = 1; j <= limit; j++)
                acc += a[j] * band[i - j];
            trial[i] = acc;
        }
    } else {
        for (i = length - 1; i >= 0; i--) {
            float acc = band[i];
            int limit = (length - 1 - i) < order ? (length - 1 - i) : order;
            for (j = 1; j <= limit; j++)
                acc += a[j] * band[i + j];
            trial[i] = acc;
        }
    }
    memcpy(band, trial, length * sizeof(float));
}

/* Attempts to fit one TNS filter over scalefactor bands [b_start, b_stop).
 * Fills *filter and whitens band in-place if successful.
 * Uses sharedWorkBuffLong for wspec and trial scratch space to maintain near-zero call stack footprint.
 * Returns 1 on success, 0 on rejection. */
static int tns_fit_subrange(faacEncStruct *hEncoder, int b_start, int b_stop,
                            const int *sfbOffsetTable, float *spec, TnsFilterData *filter)
{
    int i_start = sfbOffsetTable[b_start];
    int length = sfbOffsetTable[b_stop] - i_start;
    float *band;
    float *wspec = hEncoder->gpsyInfo.sharedWorkBuffLong;
    float *trial = hEncoder->gpsyInfo.sharedWorkBuffLong + BLOCK_LEN_LONG;
    float r[TNS_MAX_ORDER + 1] = {0};
    float k[TNS_MAX_ORDER + 1] = {0};
    float est_gain;
    int order, limit, i, best_dir;
    float filt_e = 0.0f;
    int bit_cost;
    float min_required_gain;
    float maxrms = 0.0f, floorrms;
    float sum_rms = 0.0f, sum_log_rms = 0.0f;
    float total_energy = 0.0f;
    int nbands = b_stop - b_start;
    float rms_band[MAX_SCFAC_BANDS];
    int b;

    if (length <= TNS_MAX_ORDER)
        return 0;

    band = spec + i_start;

    /* Per-band RMS normalization */
    for (b = b_start; b < b_stop; b++) {
        int s0 = sfbOffsetTable[b], s1 = sfbOffsetTable[b + 1];
        float e = 0.0f, rms, rms_fl;

        for (i = s0; i < s1; i++)
            e += (float)(spec[i] * spec[i]);
        total_energy += e;
        rms = sqrtf(e / (float)(s1 - s0));
        rms_band[b - b_start] = rms;
        if (rms > maxrms) maxrms = rms;

        rms_fl = rms > TNS_MIN_ENERGY ? rms : TNS_MIN_ENERGY;
        sum_rms += rms_fl;
        sum_log_rms += logf(rms_fl);
    }

    if (total_energy < TNS_MIN_ENERGY)
        return 0;

    /* Skip TNS on noise-like flat spectrums (PNS takes over) */
    if (expf(sum_log_rms / (float)nbands) / (sum_rms / (float)nbands) > TNS_PNS_SFM_SKIP)
        return 0;

    floorrms = maxrms * 0.005f;
    if (floorrms < TNS_MIN_ENERGY) floorrms = TNS_MIN_ENERGY;

    for (b = b_start; b < b_stop; b++) {
        int s0 = sfbOffsetTable[b], s1 = sfbOffsetTable[b + 1];
        float rms = rms_band[b - b_start];
        float wgt = 1.0f / (rms > floorrms ? rms : floorrms);
        for (i = s0; i < s1; i++)
            wspec[i - i_start] = (float)spec[i] * wgt;
    }

    /* Single 1D autocorrelation */
    calc_autocorr(TNS_MAX_ORDER, length, wspec, r);
    est_gain = compute_lpc(TNS_MAX_ORDER, r, k);

    if (est_gain < TNS_GAIN_LIMIT || !isfinite(est_gain))
        return 0;

    /* Truncate small trailing taps */
    order = TNS_MAX_ORDER;
    while (order > 0 && fabsf(k[order]) < (float)DEF_TNS_COEFF_THRESH)
        order--;
    if (order == 0)
        return 0;

    filter->order = order;

    /* Quantize coefficients at full 4-bit resolution */
    quantize_coeffs(order, DEF_TNS_COEFF_RES, k, filter->index);

    /* Enable coefCompress = 1 if all indices fit in 3-bit signed range [-4, 3] */
    filter->coefCompress = 1;
    limit = 1 << (DEF_TNS_COEFF_RES - 2); /* limit = 4 */
    for (i = 1; i <= order; i++) {
        if (filter->index[i] < -limit || filter->index[i] >= limit) {
            filter->coefCompress = 0;
            break;
        }
    }

    finalize_filter(order, k, filter->aCoeffs);

    /* Evaluate forward vs backward FIR filtering direction on quantized filter */
    best_dir = evaluate_filtering_direction(length, order, filter->aCoeffs, wspec, &filt_e);
    filter->direction = best_dir;

    /* Bit-cost rate-distortion gating */
    bit_cost = LEN_TNS_LENGTHL + LEN_TNS_ORDERL + LEN_TNS_DIRECTION + LEN_TNS_COMPRESS
               + order * (DEF_TNS_COEFF_RES - filter->coefCompress);
    min_required_gain = TNS_GAIN_LIMIT + 0.004f * (float)bit_cost;

    if (filt_e < TNS_MIN_ENERGY)
        filt_e = TNS_MIN_ENERGY;

    if (r[0] < min_required_gain * filt_e)
        return 0;

    /* Apply chosen direction FIR whitening in-place on spec */
    apply_tns_filter(length, order, best_dir, filter->aCoeffs, band, trial);
    return 1;
}

void TnsEncode(faacEncStruct *hEncoder, CoderInfo *coderInfo, float *spec)
{
    TnsInfo *tnsInfo = &coderInfo->tnsInfo;
    int numBands = coderInfo->sfbn;
    const int *sfbOffsetTable = coderInfo->sfb_offset;
    int b_start, b_stop;

    tnsInfo->tnsDataPresent = 0;
    tnsInfo->windowData.numFilters = 0;

    b_start = min(tnsInfo->tnsMinBandNumberLong, numBands);
    b_stop = min(tnsInfo->tnsMaxBandsLong, numBands);
    if (b_stop <= b_start)
        return;

    /* Attempt fitting single filter over [b_start, b_stop) */
    if (tns_fit_subrange(hEncoder, b_start, b_stop, sfbOffsetTable, spec,
                          &tnsInfo->windowData.tnsFilter[0])) {
        tnsInfo->windowData.tnsFilter[0].length = tnsInfo->tnsNumSwbLong - b_start;
        tnsInfo->windowData.numFilters = 1;
        tnsInfo->windowData.coefResolution = DEF_TNS_COEFF_RES;
        tnsInfo->tnsDataPresent = 1;
#ifdef FAAC_STATS
        g_faacStats.longBlocksTNS++;
#endif
    }
}
