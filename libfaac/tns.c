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
#include <float.h>
#include "frame.h"
#include "coder.h"
#include "tns.h"
#include "util.h"

/* ISO/IEC 14496-3 Table 4.110 - TNS max/min bands for long and short windows
 * indexed by sampling_frequency_index (0..11). */
static const struct {
    uint8_t min_long;
    uint8_t max_long;
    uint8_t min_short;
    uint8_t max_short;
} tns_sfb_range[12] = {
    {11, 31, 2, 9},   /* 96000 */
    {11, 31, 2, 9},   /* 88200 */
    {15, 34, 3, 10},  /* 64000 */
    {16, 40, 3, 14},  /* 48000 */
    {17, 42, 4, 14},  /* 44100 */
    {20, 51, 4, 14},  /* 32000 */
    {25, 46, 4, 14},  /* 24000 */
    {26, 46, 4, 14},  /* 22050 */
    {24, 42, 3, 14},  /* 16000 */
    {28, 42, 4, 14},  /* 12000 */
    {30, 42, 4, 14},  /* 11025 */
    {31, 39, 4, 14}   /* 8000 */
};

#define TNS_MAX_ORDER_LONG   12
#define TNS_MAX_ORDER_SHORT  7

#define TNS_GAIN_LIMIT       1.2f  /* Levinson-Durbin prediction gain threshold */
#define TNS_MEASURED_GAIN    1.15f /* Measured gain required after quantization */

#define TNS_MIN_ENERGY       1e-9f
#define TNS_PNS_SFM_SKIP     0.90f

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

    if (err <= TNS_MIN_ENERGY)
        return INFINITY;
    return r[0] / err;
}

static void quantize_coeffs(int order, int res, const float * k_in, float * k_out, int8_t * idx)
{
    const float s_p = (float)(((1 << (res - 1)) - 0.5f) / (M_PI / 2));
    const float s_n = (float)(((1 << (res - 1)) + 0.5f) / (M_PI / 2));
    const int i_max = (1 << (res - 1)) - 1;
    const int i_min = -(1 << (res - 1));
    int i;

    for (i = 1; i <= order; i++) {
        float val = k_in[i];
        float s = (val >= 0.0f) ? s_p : s_n;
        int q = (int)floorf(asinf(val) * s + 0.5f);

        if (q > i_max) q = i_max;
        else if (q < i_min) q = i_min;
        idx[i] = q;

        s = (q >= 0) ? s_p : s_n;
        k_out[i] = sinf((float)q / s);
    }
}

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

static void filter_spec(int length, int order, int direction, const float * a, const float * src, float * dst)
{
    int i, j;
    int limit;

    if (direction == 0) {
        /* Upward filtering (low to high frequency):
         * y[i] = src[i] + sum_{j=1}^order a[j] * src[i - j] */
        limit = order < length ? order : length;
        for (i = 0; i < limit; i++) {
            float acc = src[i];
            for (j = 1; j <= i; j++)
                acc += a[j] * src[i - j];
            dst[i] = acc;
        }
        for (; i < length; i++) {
            float acc = src[i];
            for (j = 1; j <= order; j++)
                acc += a[j] * src[i - j];
            dst[i] = acc;
        }
    } else {
        /* Downward filtering (high to low frequency):
         * y[idx] = src[idx] + sum_{j=1}^order a[j] * src[idx + j] */
        limit = order < length ? order : length;
        for (i = 0; i < limit; i++) {
            int idx = length - 1 - i;
            float acc = src[idx];
            for (j = 1; j <= i; j++)
                acc += a[j] * src[idx + j];
            dst[idx] = acc;
        }
        for (; i < length; i++) {
            int idx = length - 1 - i;
            float acc = src[idx];
            for (j = 1; j <= order; j++)
                acc += a[j] * src[idx + j];
            dst[idx] = acc;
        }
    }
}

void TnsInit(faacEncStruct* hEncoder)
{
    unsigned int ch;
    int fs = hEncoder->sampleRateIdx;

    for (ch = 0; ch < hEncoder->numChannels; ch++) {
        TnsInfo *info = &hEncoder->coderInfo[ch].tnsInfo;

        info->tnsMaxBandsLong = tns_sfb_range[fs].max_long;
        info->tnsMinBandNumberLong = tns_sfb_range[fs].min_long;
        info->tnsNumSwbLong = hEncoder->srInfo->num_cb_long;

        info->tnsMaxBandsShort = tns_sfb_range[fs].max_short;
        info->tnsMinBandNumberShort = tns_sfb_range[fs].min_short;
        info->tnsNumSwbShort = hEncoder->srInfo->num_cb_short;
    }
}

static int tns_fit_range(int max_order, int b_bottom, int b_top, int numBands,
                         const int *sfbOffsetTable,
                         float *spec, TnsFilterData *filter, int req_res)
{
    int active_top = min(b_top, numBands);
    if (active_top <= b_bottom)
        return 0;

    int i_start = sfbOffsetTable[b_bottom];
    int length = sfbOffsetTable[active_top] - i_start;
    float wspec[BLOCK_LEN_LONG];
    float trial_up[BLOCK_LEN_LONG];
    float trial_dn[BLOCK_LEN_LONG];
    float r[TNS_MAX_ORDER + 1] = {0};
    float k_lpc[TNS_MAX_ORDER + 1] = {0};
    float gain;
    int order, best_dir, i, compress;

    if (length <= max_order)
        return 0;

    /* Per-band RMS normalization */
    {
        float maxrms = 0.0f, floorrms;
        float sum_rms = 0.0f, sum_log_rms = 0.0f;
        float total_energy = 0.0f;
        int nbands = active_top - b_bottom;
        float rms_band[NSFB_LONG];
        int b;

        for (b = b_bottom; b < active_top; b++) {
            int s0 = sfbOffsetTable[b], s1 = sfbOffsetTable[b + 1];
            float e = 0.0f, rms, rms_fl;

            for (i = s0; i < s1; i++)
                e += (float)(spec[i] * spec[i]);
            total_energy += e;
            rms = sqrtf(e / (float)(s1 - s0));
            rms_band[b - b_bottom] = rms;
            if (rms > maxrms) maxrms = rms;

            rms_fl = rms > TNS_MIN_ENERGY ? rms : TNS_MIN_ENERGY;
            sum_rms += rms_fl;
            sum_log_rms += logf(rms_fl);
        }

        if (total_energy < TNS_MIN_ENERGY)
            return 0;

        if (expf(sum_log_rms / (float)nbands) / (sum_rms / (float)nbands) > TNS_PNS_SFM_SKIP)
            return 0;

        floorrms = maxrms * 0.01f;
        if (floorrms < TNS_MIN_ENERGY) floorrms = TNS_MIN_ENERGY;

        for (b = b_bottom; b < active_top; b++) {
            int s0 = sfbOffsetTable[b], s1 = sfbOffsetTable[b + 1];
            float rms = rms_band[b - b_bottom];
            float wgt = 1.0f / (rms > floorrms ? rms : floorrms);
            for (i = s0; i < s1; i++)
                wspec[i - i_start] = (float)spec[i] * wgt;
        }
    }

    calc_autocorr_f(max_order, length, wspec, r);
    gain = compute_lpc(max_order, r, k_lpc);
    if (gain < TNS_GAIN_LIMIT || !isfinite(gain))
        return 0;

    order = max_order;
    while (order > 0 && fabsf(k_lpc[order]) < (float)DEF_TNS_COEFF_THRESH)
        order--;

    if (order == 0)
        return 0;

    int best_compress = 0;
    int8_t best_idx[TNS_MAX_ORDER + 1] = {0};
    float best_k_q[TNS_MAX_ORDER + 1] = {0};
    float best_a[TNS_MAX_ORDER + 1] = {0};
    float best_filt_e = FLT_MAX;
    best_dir = 0;

    /* Evaluate with requested window resolution `req_res` and compress options (0 vs 1) */
    for (compress = 0; compress <= 1; compress++) {
        int effective_res = req_res - compress;
        int limit = 1 << (effective_res - 1);
        int fits = 1;

        int8_t test_idx[TNS_MAX_ORDER + 1] = {0};
        float test_k_q[TNS_MAX_ORDER + 1] = {0};
        float test_a[TNS_MAX_ORDER + 1] = {0};

        /* Quantize and dequantize at effective resolution */
        quantize_coeffs(order, effective_res, k_lpc, test_k_q, test_idx);

        if (compress) {
            for (i = 1; i <= order; i++) {
                if (test_idx[i] < -(limit / 2) || test_idx[i] >= (limit / 2)) {
                    fits = 0;
                    break;
                }
            }
            if (!fits) continue;
        }

        finalize_filter(order, test_k_q, test_a);

        float e_up = 0.0f, e_dn = 0.0f;
        filter_spec(length, order, 0, test_a, wspec, trial_up);
        filter_spec(length, order, 1, test_a, wspec, trial_dn);
        for (i = 0; i < length; i++) {
            e_up += trial_up[i] * trial_up[i];
            e_dn += trial_dn[i] * trial_dn[i];
        }

        int cur_dir = (e_dn < e_up) ? 1 : 0;
        float cur_e = (e_dn < e_up) ? e_dn : e_up;

        if (cur_e < best_filt_e) {
            best_filt_e = cur_e;
            best_compress = compress;
            best_dir = cur_dir;
            memcpy(best_idx, test_idx, (order + 1) * sizeof(int8_t));
            memcpy(best_k_q, test_k_q, (order + 1) * sizeof(float));
            memcpy(best_a, test_a, (order + 1) * sizeof(float));
        }
    }

    if (best_filt_e < TNS_MIN_ENERGY)
        best_filt_e = TNS_MIN_ENERGY;

    if (r[0] < TNS_MEASURED_GAIN * best_filt_e)
        return 0;

    filter->order = order;
    filter->direction = best_dir;
    filter->coefCompress = best_compress;
    memcpy(filter->index, best_idx, (order + 1) * sizeof(int8_t));

    /* Apply filter in-place to the window using direct-form coefficients best_a matching decoder dequantization */
    float *band = spec + i_start;
    filter_spec(length, order, best_dir, best_a, band, trial_up);
    memcpy(band, trial_up, length * sizeof(float));

    return 1;
}

void TnsEncode(CoderInfo *coderInfo, float *spec)
{
    TnsInfo *tnsInfo = &coderInfo->tnsInfo;
    int numBands = coderInfo->sfbn;
    const int *sfbOffsetTable = coderInfo->sfb_offset;

    tnsInfo->tnsDataPresent = 0;
    memset(tnsInfo->windowData, 0, sizeof(tnsInfo->windowData));

    if (coderInfo->block_type == ONLY_SHORT_WINDOW) {
        int b_top_global = tnsInfo->tnsMaxBandsShort;
        int b_bottom_global = tnsInfo->tnsMinBandNumberShort;
        if (b_top_global <= b_bottom_global)
            return;

        int w;
        int has_active = 0;

        for (w = 0; w < MAX_SHORT_WINDOWS; w++) {
            float *w_spec = spec + w * BLOCK_LEN_SHORT;
            TnsWindowData *winData = &tnsInfo->windowData[w];
            int coef_res = DEF_TNS_COEFF_RES;

            TnsFilterData test_flt;
            memset(&test_flt, 0, sizeof(TnsFilterData));

            if (tns_fit_range(TNS_MAX_ORDER_SHORT, b_bottom_global, b_top_global, numBands,
                              sfbOffsetTable, w_spec, &test_flt, coef_res)) {
                test_flt.length = b_top_global - b_bottom_global;
                winData->tnsFilter[0] = test_flt;
                winData->numFilters = 1;
                winData->coefResolution = coef_res;
                has_active = 1;
#ifdef FAAC_STATS
                g_faacStats.shortBlocksTNS++;
#endif
            } else {
                winData->numFilters = 0;
            }
        }
        if (has_active) {
            tnsInfo->tnsDataPresent = 1;
        }
    } else {
        /* Long window: up to 3 filters, emitted top-down starting from tnsMaxBandsLong per ISO/IEC 14496-3 */
        int b_top_global = tnsInfo->tnsMaxBandsLong;
        int b_bottom_global = tnsInfo->tnsMinBandNumberLong;
        if (b_top_global <= b_bottom_global)
            return;

        TnsWindowData *winData = &tnsInfo->windowData[0];
        int num_filters = 0;
        int coef_res = DEF_TNS_COEFF_RES;

        int total_bands = b_top_global - b_bottom_global;
        int max_fits = (total_bands >= 24) ? 3 : (total_bands >= 12) ? 2 : 1;
        int bands_per_fit = total_bands / max_fits;

        int current_top = b_top_global;
        int f_idx;

        for (f_idx = 0; f_idx < max_fits; f_idx++) {
            int current_bottom = (f_idx == max_fits - 1) ? b_bottom_global : (current_top - bands_per_fit);
            if (current_bottom >= current_top)
                break;

            TnsFilterData test_flt;
            memset(&test_flt, 0, sizeof(TnsFilterData));

            if (tns_fit_range(TNS_MAX_ORDER_LONG, current_bottom, current_top, numBands,
                              sfbOffsetTable, spec, &test_flt, coef_res)) {
                test_flt.length = current_top - current_bottom;
                winData->tnsFilter[num_filters] = test_flt;
                num_filters++;
            } else {
                /* Emit order=0 filter with length to preserve band alignment in top-down iteration */
                test_flt.order = 0;
                test_flt.length = current_top - current_bottom;
                winData->tnsFilter[num_filters] = test_flt;
                num_filters++;
            }
            current_top = current_bottom;
        }

        int has_active = 0;
        for (f_idx = 0; f_idx < num_filters; f_idx++) {
            if (winData->tnsFilter[f_idx].order > 0) {
                has_active = 1;
                break;
            }
        }

        if (has_active) {
            winData->numFilters = num_filters;
            winData->coefResolution = coef_res;
            tnsInfo->tnsDataPresent = 1;
#ifdef FAAC_STATS
            g_faacStats.longBlocksTNS++;
#endif
        } else {
            winData->numFilters = 0;
            tnsInfo->tnsDataPresent = 0;
        }
    }
}
