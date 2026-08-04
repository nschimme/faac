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

/* Filters per long window. The spec allows up to 3; this ships 1. Raising it
 * is the obvious explanation for why TNS almost never admits a filter, and it
 * is wrong -- the multi-filter path below exists to keep that answerable, not
 * because it pays.
 *
 * The argument for 3 is real as far as it goes: one 8th-order fit across the
 * whole 16..40 band span averages sub-ranges that need not share a temporal
 * envelope, and the best of 3 sub-ranges beats the whole-range fit on 62-79%
 * of frames, lifting average prediction gain from ~1.04 to ~1.15. Measured at
 * -b 128 with TNS forced on, the frames that actually admit a filter go:
 *
 *   clip      1 filter   3 filters   3 filters, flatness gate off
 *   sandman     0.0%       0.0%               0.0%
 *   trumpet     0.0%       0.3%                --
 *   Waiting     0.0%       0.3%               2.2%
 *   fms         0.7%       0.7%               1.0%
 *
 * A prediction of 2-12% came from modelling only the first of three gates.
 * Narrow sub-ranges look noise-like, so the spectral-flatness skip fires far
 * more on them -- 194 of 354 sub-range fits on Waiting -- and what survives it
 * still fails on prediction gain: 347 rejections on Waiting with that gate
 * disabled outright. Even that ceiling is ~2%.
 *
 * The spectra are not predictable along frequency at any of these widths, and
 * splitting the range does not change it. Default stays 1; FAAC_TNS_FILTERS
 * keeps the path measurable without shipping it. */
#define TNS_NUM_FILTERS     1

#define TNS_LPC_ORDER       8     /* fixed filter order; spec allows up to TNS_MAX_ORDER but higher orders rarely paid for themselves here */
/* Prediction-gain floor on the un-quantized fit. This is a cheap early-out, NOT
 * a quality knob: TNS_MEASURED_GAIN below is the same bar applied to the filter
 * actually transmitted, so it re-rejects everything a lower limit here would
 * admit. Measured on 9 SQAM clips at 32k -- dropping this alone to 1.05 moves 75
 * frames from this gate to that one and leaves the output byte-identical. Its
 * real job is skipping quantize+trial-filter work on ~74% of frames. */
#define TNS_GAIN_LIMIT      1.4f
/* Deliberately unbounded above. A very high prediction gain looks like a
 * near-singular fit worth rejecting, but compute_lpc already clamps the
 * reflection coefficients to +-0.999, so the filter cannot be unstable however
 * good the fit looks, and isfinite() catches the genuinely degenerate ones. A
 * ceiling here only discards the strongest filters on exactly the material TNS
 * exists for: at 6.0 it costs -0.034 zimtohrli over 9 SQAM clips at 32k (95% CI
 * [+0.005, +0.072] for its removal) -- glockenspiel -0.124, castanets -0.136,
 * flute -0.042, six clips bit-identical, nothing gained -- and spends bits. */
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
    }
}

/* Fit one TNS filter over scalefactor bands [b0, b1) and, if it earns its
 * place, whiten that range of `spec` in place and fill *filter.
 *
 * Returns 1 when a filter was written (order > 0), 0 otherwise. A 0 return is
 * not a failure for the caller to route around: the spec allows an order-0
 * filter, which covers its region and filters nothing, so a range that does
 * not pay still occupies its slot and the regions stay contiguous. */
static int tns_fit_range(int b0, int b1, int *sfbOffsetTable,
                         float *spec, int direction, TnsFilterData *filter,
                         int lpc_order, float gain_limit, float gain_clamp,
                         float measured_gain, float sfm_skip)
{
    int i_start = sfbOffsetTable[b0];
    int length = sfbOffsetTable[b1] - i_start;
    int b_start = b0, b_stop = b1;
    float *band, energy;
    float wspec[BLOCK_LEN_LONG];
    float r[TNS_MAX_ORDER + 1] = {0};
    float k[TNS_MAX_ORDER + 1] = {0};
    float gain;
    int order, limit, i;

    filter->order = 0;
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

    /* Fixed, and provably unchooseable from the analysis above: calc_autocorr_f
     * is invariant under sequence reversal, so both directions yield the same
     * LPC fit and indistinguishable prediction gain. Direction has to come from
     * the time-domain transient position, which TnsEncode never sees. Until it
     * does, FAAC_TNS_DIR exists to measure which way is right. */
    filter->direction = direction;

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
    return 1;
}

void TnsEncode(TnsInfo* tnsInfo, int numBands, enum WINDOW_TYPE blockType, int* sfbOffsetTable,
               float* spec, int direction)
{
    int b_start, b_stop, nfilt, f, applied = 0;
    int lpc_order = TNS_LPC_ORDER;
    float gain_limit = TNS_GAIN_LIMIT;
    float gain_clamp = INFINITY;
    float measured_gain = TNS_MEASURED_GAIN;
    float sfm_skip = TNS_PNS_SFM_SKIP;
    int edge[TNS_MAX_FILTERS + 1];

    FAAC_TUNE_I(lpc_order, tns_order);
    FAAC_TUNE_F(gain_limit, tns_gain);
    FAAC_TUNE_F(gain_clamp, tns_gain_clamp);
    FAAC_TUNE_F(measured_gain, tns_measured);
    FAAC_TUNE_F(sfm_skip, tns_sfm);
    lpc_order = clamp_int(lpc_order, 1, TNS_MAX_ORDER);

    nfilt = TNS_NUM_FILTERS;
    FAAC_TUNE_I(nfilt, tns_filters);
    nfilt = clamp_int(nfilt, 1, TNS_MAX_FILTERS - 1);

    tnsInfo->tnsDataPresent = 0;
    tnsInfo->windowData.numFilters = 0;
    TNS_TALLY(frames);

    /* Short windows already have the temporal resolution to not need TNS. */
    if (blockType == ONLY_SHORT_WINDOW) {
        TNS_TALLY(shortw);
        goto done;
    }

    b_start = min(tnsInfo->tnsMinBandNumberLong, numBands);
    b_stop = min(tnsInfo->tnsMaxBandsLong, numBands);
    if (b_stop <= b_start) {
        TNS_TALLY(range);
        goto done;
    }

    /* The shipping path, kept separate so it stays byte-exact with master --
     * it has its own length convention, which the multi-filter path below does
     * not share. */
    if (nfilt == 1) {
        if (tns_fit_range(b_start, b_stop, sfbOffsetTable, spec,
                          direction, &tnsInfo->windowData.tnsFilter[0],
                          lpc_order, gain_limit, gain_clamp, measured_gain,
                          sfm_skip)) {
            /* Spans from b_start to the top of the spectrum rather than to
             * b_stop. That over-declares the region, and is harmless only
             * because the decoder clamps the bottom edge at tns_min_band --
             * with one filter there is nothing below it to displace. The
             * multi-filter path below cannot rely on that, since each filter's
             * region is measured from where the previous one ended. */
            tnsInfo->windowData.tnsFilter[0].length =
                tnsInfo->tnsNumSwbLong - b_start;
            tnsInfo->windowData.numFilters = 1;
            tnsInfo->windowData.coefResolution = DEF_TNS_COEFF_RES;
            tnsInfo->tnsDataPresent = 1;
            TNS_TALLY(applied);
        }
        goto done;
    }

    /* Split [b_start, b_stop) into nfilt contiguous ranges of equal band
     * count. A single 8th-order fit across the whole span averages sub-ranges
     * that need not share a temporal envelope, which is why the whole-range
     * prediction gain sits near 1.0 even on frames that do contain a
     * transient. Measured, the best of 3 sub-ranges beats the whole-range fit
     * on 62-79% of frames.
     *
     * Filters are transmitted top-down, each covering `length` bands measured
     * from where the previous one stopped, so the edges are walked in reverse
     * and every range is declared relative to b_stop. */
    for (f = 0; f <= nfilt; f++)
        edge[f] = b_start + (int)((long)(b_stop - b_start) * f / nfilt);

    for (f = 0; f < nfilt; f++) {
        int lo = edge[nfilt - 1 - f], hi = edge[nfilt - f];
        TnsFilterData *flt = &tnsInfo->windowData.tnsFilter[f];

        if (tns_fit_range(lo, hi, sfbOffsetTable, spec, direction,
                          flt, lpc_order, gain_limit, gain_clamp,
                          measured_gain, sfm_skip))
            applied = 1;
        else
            flt->order = 0;   /* legal: covers the region, filters nothing */
        flt->length = hi - lo;
    }

    if (applied) {
        tnsInfo->windowData.numFilters = nfilt;
        tnsInfo->windowData.coefResolution = DEF_TNS_COEFF_RES;
        tnsInfo->tnsDataPresent = 1;
        TNS_TALLY(applied);
    }

done:
    tns_stats_report();
}
