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
#include <stdio.h>
#include <string.h>
#include "frame.h"
#include "coder.h"
#include "tns.h"
#include "util.h"
#include "tuning.h"

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

/* TNS_MAX_BANDS for EIGHT_SHORT_SEQUENCE, Main/LC profile, same table and same
 * indexing as above (ISO/IEC 14496-3 Table 4.157). A short window carries 128
 * lines against the long window's 1024, so the band counts are far smaller. */
static const unsigned char tns_max_bands_short[12] = {
    9, 9, 10, 14, 14, 14, 14, 14, 14, 14, 14, 14
};

/* The spec caps short-window filter order at 7, which is also all the
 * bitstream can express: LEN_TNS_ORDERS is 3 bits. */
#define TNS_MAX_ORDER_SHORT 7

/* Segments the pre-autocorrelation normalization averages over on short
 * windows. Per-scalefactor-band normalization is fine across a long window's
 * wide bands, but a short window's low bands are 4 lines each: flattening every
 * one of those to unit RMS erases the spectral envelope the LPC is meant to
 * model, leaving it to fit noise. Coarse equal-width segments keep the envelope
 * while still stopping the loudest region from dominating the fit. */
#define TNS_NORM_SEGMENTS 4

#define TNS_LPC_ORDER       8     /* fixed filter order; spec allows up to TNS_MAX_ORDER but higher orders rarely paid for themselves here */
/* Prediction-gain floor on the un-quantized fit. This is a cheap early-out, NOT
 * a quality knob: TNS_MEASURED_GAIN below is the same bar applied to the filter
 * actually transmitted, so it re-rejects everything a lower limit here would
 * admit. Measured on 9 SQAM clips at 32k -- dropping this alone to 1.05 moves 75
 * frames from this gate to that one and leaves the output byte-identical. Its
 * real job is skipping quantize+trial-filter work on ~74% of frames. */
#define TNS_GAIN_LIMIT      1.4f
/* No upper bound on prediction gain. There used to be one (6.0), on the theory
 * that a very high gain meant a near-singular fit worth rejecting, but the
 * reflection coefficients are already clamped to +-0.999 in compute_lpc so the
 * filter cannot be unstable however good the fit looks. All it did was throw
 * away the strongest filters on exactly the material TNS is for: removing it
 * measured +0.034 zimtohrli over 9 SQAM clips at 32k (95% CI [+0.005, +0.072]),
 * from glockenspiel +0.124, castanets +0.136 and flute +0.042 with the other
 * six clips bit-identical and nothing regressing -- and it saved bits.
 * Genuinely degenerate fits are caught by the isfinite() test instead. */
/* Post-quantization re-check on the filter actually being transmitted -- the
 * authoritative admission test. Sweeping this and TNS_GAIN_LIMIT together to
 * 1.2/1.1/1.05 measured -0.013/-0.012/-0.007 zimtohrli over 9 SQAM clips at 32k
 * (all CIs spanning zero), so 1.4 stays: loosening admits filters that hurt a
 * few clips more than they help the rest. */
#define TNS_MEASURED_GAIN   1.4f

/* Below this, a band's spectral energy is indistinguishable from float
 * rounding noise, so there's nothing real for TNS to whiten. Also reused
 * below as the floor for RMS-normalization and the LPC residual check,
 * rather than inventing a separate near-zero constant for each. */
#define TNS_MIN_ENERGY      1e-9f
/* SFM (geomean/arithmean of per-band RMS) near 1.0 means the band is
 * noise-like, which PNS (quantize.c) is about to replace anyway -- skip
 * TNS's LPC work there; it only pays off on tonal/peaky bands. */
#define TNS_PNS_SFM_SKIP    0.85f

#ifdef FAAC_TUNING
/* Per-gate reject counts. An aggregate "TNS helped/hurt" number says nothing
 * about which threshold to move; a funnel dominated by one stage does. */
static struct {
    long frames, applied;
    long shortw, range, energy, sfm, gainlo, gainhi, order, measured;
} tnsStats;

#define TNS_TALLY(field) do { if (FAAC_TUNE_ON(tns_stats)) tnsStats.field++; } while (0)

static void tns_stats_report(void)
{
    if (!FAAC_TUNE_ON(tns_stats) || tnsStats.frames % 25 != 0)
        return;
    fprintf(stderr, "TNS_STATS frames=%ld applied=%ld pct=%.1f "
                    "short=%ld range=%ld energy=%ld sfm=%ld gainlo=%ld "
                    "gainhi=%ld order=%ld measured=%ld\n",
            tnsStats.frames, tnsStats.applied,
            100.0 * tnsStats.applied / tnsStats.frames,
            tnsStats.shortw, tnsStats.range, tnsStats.energy, tnsStats.sfm,
            tnsStats.gainlo, tnsStats.gainhi, tnsStats.order, tnsStats.measured);
}
#else
#define TNS_TALLY(field) ((void)0)
#define tns_stats_report() ((void)0)
#endif

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
     * infinite gain so the caller's upper bound rejects it whatever that bound
     * is set to, instead of dividing by ~0. */
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
    /* This is an all-zero filter, so only the `order` most recent unfiltered
     * inputs are live state and a TNS_MAX_ORDER ring would do. Measured both
     * ways on TNS-heavy material: a shift register costs ~2% and a double-write
     * ring ~1%, because the contiguous snapshot lets the tap loop vectorize
     * against linear memory while ring indexing does not. 4 KB of stack is
     * cheaper here than 1-2% of encode time -- don't "optimize" this again
     * without measuring. */
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

    FaacTuningInit();

    for (ch = 0; ch < hEncoder->numChannels; ch++) {
        TnsInfo *info = &hEncoder->coderInfo[ch].tnsInfo;

        info->tnsMaxBandsLong = tns_sfb_range[fs].max;
        info->tnsNumSwbLong = hEncoder->srInfo->num_cb_long;
        info->tnsMinBandNumberLong = tns_sfb_range[fs].min;

        info->tnsMaxBandsShort = tns_max_bands_short[fs];
        info->tnsNumSwbShort = hEncoder->srInfo->num_cb_short;
        /* The spec fixes the upper band limit but not where TNS starts, and
         * the long start band above is a FAAC choice. Rather than invent a
         * second magic table, translate that choice to short windows by
         * frequency: a short window's 128 lines span the same bandwidth as the
         * long window's 1024, so line k short == line 8k long. */
        {
            int long_start = 0, short_start = 0, b;

            for (b = 0; b < info->tnsMinBandNumberLong &&
                        b < hEncoder->srInfo->num_cb_long; b++)
                long_start += hEncoder->srInfo->cb_width_long[b];

            info->tnsMinBandNumberShort = info->tnsMaxBandsShort;
            for (b = 0; b < hEncoder->srInfo->num_cb_short; b++) {
                if (short_start * 8 >= long_start) {
                    info->tnsMinBandNumberShort = b;
                    break;
                }
                short_start += hEncoder->srInfo->cb_width_short[b];
            }
        }
    }
}

/* Analyse one window and, if the filter pays for itself, whiten it in place.
 * Returns 1 if a filter was written to `win`. Shared by both block types: a
 * long block is simply the one-window case, and tns_data() in the bitstream
 * has the same shape for both, only with narrower fields for short windows. */
static int tns_fit_window(TnsWindowData *win, float *spec, const int *sfbOffsetTable,
                          int b_start, int b_stop, int numSwb, int max_order,
                          int coarseNorm)
{
    int seg_off[TNS_NORM_SEGMENTS + 1];
    int nseg;
    float wspec[BLOCK_LEN_LONG];
    float r[TNS_MAX_ORDER + 1] = {0};
    float k[TNS_MAX_ORDER + 1] = {0};
    float *band, energy, gain;
    TnsFilterData *filter;
    int i_start, length, order, limit, i;
    int lpc_order = TNS_LPC_ORDER;
    float gain_limit = TNS_GAIN_LIMIT;
    float gain_clamp = INFINITY;
    float measured_gain = TNS_MEASURED_GAIN;
    float sfm_skip = TNS_PNS_SFM_SKIP;

    FAAC_TUNE_I(lpc_order, tns_order);
    FAAC_TUNE_F(gain_limit, tns_gain);
    FAAC_TUNE_F(gain_clamp, tns_gain_clamp);
    FAAC_TUNE_F(measured_gain, tns_measured);
    FAAC_TUNE_F(sfm_skip, tns_sfm);
    lpc_order = clamp_int(lpc_order, 1, max_order);

    win->numFilters = 0;
    TNS_TALLY(frames);

    if (b_stop <= b_start) {
        TNS_TALLY(range);
        return 0;
    }

    i_start = sfbOffsetTable[b_start];
    length = sfbOffsetTable[b_stop] - i_start;
    if (length <= lpc_order) {
        TNS_TALLY(range);
        return 0;
    }

    band = spec + i_start;
    energy = 0.0f;
    for (i = 0; i < length; i++)
        energy += band[i] * band[i];
    if (energy < TNS_MIN_ENERGY) {
        TNS_TALLY(energy);
        return 0;
    }

    /* Per-band RMS-normalize before autocorrelation, floored at 1% of the
     * loudest band's RMS. Un-normalized, Levinson-Durbin would fit whatever
     * band has the most energy and ignore quieter ones -- but pre-echo is
     * audible in quiet bands too, so the filter needs to whiten across the
     * whole range, not just the peak. */
    /* Segment boundaries the normalization below works over: one per
     * scalefactor band for a long window, TNS_NORM_SEGMENTS equal slices of the
     * line range for a short one. */
    if (coarseNorm) {
        nseg = TNS_NORM_SEGMENTS;
        for (i = 0; i <= nseg; i++)
            seg_off[i] = i_start + (length * i) / nseg;
    } else {
        nseg = b_stop - b_start;
    }

    {
        float maxrms = 0.0f, floorrms;
        float sum_rms = 0.0f, sum_log_rms = 0.0f;
        int nbands = nseg;
        int b;

        for (b = 0; b < nseg; b++) {
            int s0 = coarseNorm ? seg_off[b]     : sfbOffsetTable[b_start + b];
            int s1 = coarseNorm ? seg_off[b + 1] : sfbOffsetTable[b_start + b + 1];
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
        if (expf(sum_log_rms / (float)nbands) / (sum_rms / (float)nbands) > sfm_skip) {
            TNS_TALLY(sfm);
            return 0;
        }

        floorrms = maxrms * 0.01f;
        if (floorrms < TNS_MIN_ENERGY) floorrms = TNS_MIN_ENERGY;

        /* Recomputes the per-band energy the pass above already had. Caching it
         * in a bandrms[] array measured ~1.2% SLOWER on TNS-heavy material:
         * ~800 multiply-adds out of L1 cost less than a dependent load that
         * stops this loop vectorizing. Left as-is deliberately. */
        for (b = 0; b < nseg; b++) {
            int s0 = coarseNorm ? seg_off[b]     : sfbOffsetTable[b_start + b];
            int s1 = coarseNorm ? seg_off[b + 1] : sfbOffsetTable[b_start + b + 1];
            float e = 0.0f, rms, wgt;

            for (i = s0; i < s1; i++)
                e += (float)(spec[i] * spec[i]);
            rms = sqrtf(e / (float)(s1 - s0));
            wgt = 1.0f / (rms > floorrms ? rms : floorrms);
            for (i = s0; i < s1; i++)
                wspec[i - i_start] = (float)spec[i] * wgt;
        }
    }

    calc_autocorr_f(lpc_order, length, wspec, r);
    gain = compute_lpc(lpc_order, r, k);
    if (gain < gain_limit) {
        TNS_TALLY(gainlo);
        return 0;
    }
    /* err collapsed to ~0: a "perfect" fit means degenerate input, not a great
     * filter. compute_lpc reports that as INFINITY. */
    if (!isfinite(gain) || gain > gain_clamp) {
        TNS_TALLY(gainhi);
        return 0;
    }

    filter = &win->tnsFilter[0];
    quantize_coeffs(lpc_order, DEF_TNS_COEFF_RES, k, filter->index);

    /* Drop trailing taps that quantized away to ~nothing: they cost bits
     * without changing what the filter does. */
    order = lpc_order;
    while (order > 0 && fabsf(k[order]) < (float)DEF_TNS_COEFF_THRESH)
        order--;
    if (order == 0) {
        TNS_TALLY(order);
        return 0;
    }

    filter->order = order;
    filter->length = numSwb - b_start;

    /* Direction is fixed rather than picked from a transient envelope; that
     * comes with FrameStrategy in a later commit. */
    filter->direction = 0;

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
        if (orig_e < measured_gain * filt_e) {
            TNS_TALLY(measured);
            return 0;
        }
    }

    filter_spec(length, order, filter->direction, filter->aCoeffs, band);
    win->numFilters = 1;
    win->coefResolution = DEF_TNS_COEFF_RES;
    TNS_TALLY(applied);
    return 1;
}

void TnsEncode(TnsInfo* tnsInfo, int numBands, enum WINDOW_TYPE blockType, int* sfbOffsetTable,
               float* spec)
{
    int w, any = 0;

    tnsInfo->tnsDataPresent = 0;

    if (blockType == ONLY_SHORT_WINDOW) {
        /* Each of the 8 short windows gets its own filter over its own 128
         * lines. This is where the transients are -- the block switcher sent
         * this frame short precisely because it found one -- so skipping TNS
         * here would leave it unused on the material it exists for. */
        int b_start = min(tnsInfo->tnsMinBandNumberShort, numBands);
        int b_stop = min(tnsInfo->tnsMaxBandsShort, numBands);

        tnsInfo->numWindows = MAX_SHORT_WINDOWS;
        for (w = 0; w < MAX_SHORT_WINDOWS; w++)
            any |= tns_fit_window(&tnsInfo->windowData[w],
                                  spec + w * BLOCK_LEN_SHORT, sfbOffsetTable,
                                  b_start, b_stop, tnsInfo->tnsNumSwbShort,
                                  TNS_MAX_ORDER_SHORT, 1);
    } else {
        tnsInfo->numWindows = 1;
        any = tns_fit_window(&tnsInfo->windowData[0], spec, sfbOffsetTable,
                             min(tnsInfo->tnsMinBandNumberLong, numBands),
                             min(tnsInfo->tnsMaxBandsLong, numBands),
                             tnsInfo->tnsNumSwbLong, TNS_MAX_ORDER, 0);
    }

    /* tns_data_present covers the whole frame; individual windows can still
     * carry n_filt == 0. Only claim it when something was actually filtered,
     * so a frame that declined everywhere costs no payload at all. */
    tnsInfo->tnsDataPresent = any;
    tns_stats_report();
}
