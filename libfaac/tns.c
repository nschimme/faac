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
    unsigned char min;
    unsigned char max;
} tns_sfb_range[12] = {
    {11, 31}, {12, 31}, {15, 34}, {16, 40}, {17, 42}, {20, 51},
    {25, 46}, {26, 46}, {24, 42}, {28, 42}, {30, 42}, {31, 39}
};

#define TNS_LPC_ORDER       8     /* fixed filter order; spec allows up to TNS_MAX_ORDER but higher orders rarely paid for themselves here */
#define TNS_GAIN_LIMIT      1.4f  /* Levinson-Durbin prediction gain below this isn't worth the filter's bit cost */
#define TNS_MEASURED_GAIN   1.4f  /* post-quantization re-check: same bar as TNS_GAIN_LIMIT, applied to the filter actually being transmitted */

/* Below this, a band's spectral energy is indistinguishable from float
 * rounding noise, so there's nothing real for TNS to whiten. Also reused
 * below as the floor for RMS-normalization and the LPC residual check,
 * rather than inventing a separate near-zero constant for each. */
#define TNS_MIN_ENERGY      1e-9f
/* SFM (geomean/arithmean of per-band RMS) near 1.0 means the band is
 * noise-like, which PNS (quantize.c) is about to replace anyway -- skip
 * TNS's LPC work there; it only pays off on tonal/peaky bands. */
#define TNS_PNS_SFM_SKIP    0.85f

/* Strided sampling trades autocorrelation precision for speed: at TNS's
 * default-on frame rate, this is a real cost on every long-window frame,
 * not just the promoted borderline ones. step=4 was the best measured
 * balance of throughput recovered vs. MOS retained on this codebase's own
 * gates (CPE joint fit, promotion) -- see project-tns-post-recovery-levers
 * memory, not carried over from any prior PR's numbers. */
#ifndef FAAC_TNS_DECIMATION
#define FAAC_TNS_DECIMATION 4
#endif

static void calc_autocorr_f(int order, int length, const float * work, float * r)
{
    const int step = FAAC_TNS_DECIMATION;
    int lag, i;

    for (lag = 0; lag <= order; lag++) {
        float acc = 0.0f;
        const float * p1 = work;
        const float * p2 = work + lag;
        int n = length - lag;

        for (i = 0; i < n; i += step)
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

/* Fits one TNS filter over scalefactor bands [b_start, b_stop) for a single
 * channel's spectrum and -- if it earns its place -- whitens it in place and
 * fills *filter. Returns 1 when a filter was written, 0 otherwise.
 *
 * Called once per channel: TNS runs here before AACstereo's M/S/IS mixing,
 * but each channel's tns_data is transmitted separately (WriteICS is called
 * once per channel in channels.c) and decoders undo M/S first, then invert
 * TNS per channel with that channel's own filter -- so independent
 * per-channel filters are spec-compliant and need no shared fit. */
static int tns_fit_range(int b_start, int b_stop, int *sfbOffsetTable,
                         float *spec, TnsFilterData *filter)
{
    int i_start = sfbOffsetTable[b_start];
    int length = sfbOffsetTable[b_stop] - i_start;
    float wspec[BLOCK_LEN_LONG];
    float r[TNS_MAX_ORDER + 1] = {0};
    float k[TNS_MAX_ORDER + 1] = {0};
    float bandrms[NSFB_LONG], floorrms;
    float gain, energy, maxrms, sum_rms, sum_log_rms;
    int order, limit, i, b, nbands = b_stop - b_start;

    if (length <= TNS_LPC_ORDER)
        return 0;

    energy = 0.0f;
    {
        float *band = spec + i_start;
        for (i = 0; i < length; i++)
            energy += band[i] * band[i];
    }
    if (energy < TNS_MIN_ENERGY)
        return 0;

    /* Per-band RMS-normalize before autocorrelation, floored at 1% of the
     * loudest band's RMS. Un-normalized, Levinson-Durbin would fit whatever
     * band has the most energy and ignore quieter ones -- but pre-echo is
     * audible in quiet bands too, so the filter needs to whiten across the
     * whole range, not just the peak. */
    maxrms = 0.0f;
    sum_rms = 0.0f;
    sum_log_rms = 0.0f;
    for (b = b_start; b < b_stop; b++) {
        int s0 = sfbOffsetTable[b], s1 = sfbOffsetTable[b + 1];
        float e = 0.0f, rms, rms_fl;

        for (i = s0; i < s1; i++)
            e += (float)(spec[i] * spec[i]);
        rms = sqrtf(e / (float)(s1 - s0));
        bandrms[b] = rms; /* kept for un-normalizing the filtered signal back later */
        if (rms > maxrms) maxrms = rms;

        /* rms_fl keeps logf() away from 0 for silent bands; folded into
         * the same loop as maxrms rather than a second pass. */
        rms_fl = rms > TNS_MIN_ENERGY ? rms : TNS_MIN_ENERGY;
        sum_rms += rms_fl;
        sum_log_rms += logf(rms_fl);
    }

    /* Spectral flatness (geomean/arithmean of per-band RMS) near 1.0 means
     * the band is noise-like, which PNS (quantize.c) is about to replace
     * anyway -- skip the LPC work; it only pays off on tonal/peaky bands. */
    if (expf(sum_log_rms / (float)nbands) / (sum_rms / (float)nbands) <= TNS_PNS_SFM_SKIP)
        return 0;

    floorrms = maxrms * 0.01f;
    if (floorrms < TNS_MIN_ENERGY) floorrms = TNS_MIN_ENERGY;

    for (b = b_start; b < b_stop; b++) {
        int s0 = sfbOffsetTable[b], s1 = sfbOffsetTable[b + 1];
        float wgt = 1.0f / (bandrms[b] > floorrms ? bandrms[b] : floorrms);

        for (i = s0; i < s1; i++)
            wspec[i - i_start] = (float)spec[i] * wgt;
    }

    calc_autocorr_f(TNS_LPC_ORDER, length, wspec, r);
    gain = compute_lpc(TNS_LPC_ORDER, r, k);
    if (gain < TNS_GAIN_LIMIT)
        return 0;
    /* No upper bound: compute_lpc clamps reflection coefficients to +-0.999,
     * so a high gain can't mean an unstable filter; isfinite() catches the
     * genuinely degenerate (near-zero-error) fits instead. */
    if (!isfinite(gain))
        return 0;

    quantize_coeffs(TNS_LPC_ORDER, DEF_TNS_COEFF_RES, k, filter->index);

    /* Drop trailing taps that quantized away to ~nothing: they cost bits
     * without changing what the filter does. */
    order = TNS_LPC_ORDER;
    while (order > 0 && fabsf(k[order]) < (float)DEF_TNS_COEFF_THRESH)
        order--;
    if (order == 0)
        return 0;

    filter->order = order;

    /* Fixed at 0, not chosen: calc_autocorr_f is invariant under sequence
     * reversal, so both directions give the same LPC fit and prediction gain.
     * Picking the right one needs the time-domain transient position, which
     * this function never sees. */
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
     * real (quantized) filter before committing to writing it out. Filter
     * wspec itself (the same normalized signal r and gain were fit on)
     * rather than the raw spectrum: applying the filter to a
     * differently-scaled signal than it was fit to would whiten
     * discontinuities the fit never saw, defeating the point of the filter,
     * and this re-check would be measuring a different filter response than
     * what gets committed below. */
    {
        float orig_e = 0.0f, filt_e = 0.0f;

        for (i = 0; i < length; i++)
            orig_e += wspec[i] * wspec[i];

        filter_spec(length, order, filter->direction, filter->aCoeffs, wspec);
        for (i = 0; i < length; i++)
            filt_e += wspec[i] * wspec[i];

        if (filt_e < TNS_MIN_ENERGY)
            filt_e = TNS_MIN_ENERGY;
        if (orig_e < TNS_MEASURED_GAIN * filt_e)
            return 0;
    }

    /* wspec now holds the filtered, normalized signal; scale each band back
     * up by the same per-band RMS the forward normalization divided out
     * before writing into the real spectrum for quantization. */
    for (b = b_start; b < b_stop; b++) {
        int s0 = sfbOffsetTable[b], s1 = sfbOffsetTable[b + 1];
        float scale = bandrms[b] > floorrms ? bandrms[b] : floorrms;

        for (i = s0; i < s1; i++)
            spec[i] = wspec[i - i_start] * scale;
    }
    return 1;
}

/* PNS (quantize.c's assign_band_codebooks) decides per band, AFTER
 * quantization has already run, whether that band's spectrum gets replaced
 * by synthetic noise -- its predicate needs the bit-search-derived quality
 * and the full avg/peak masking-target model, none of which exist yet at
 * TNS time (TNS runs on the raw spectrum before BlocQuant). Approximate it
 * here with a per-band spectral flatness measure (geomean/arithmean of
 * in-band line energies) on the untouched raw spectrum: PNS only ever
 * targets noise-like bands (SFM near 1.0) -- a tonal/peaky band (low SFM)
 * is exactly the case where quantize.c's peak-energy term keeps its target
 * above pns_threshold and PNS does NOT fire. This is the same "flat means
 * noise, TNS doesn't pay off there" intuition as TNS_PNS_SFM_SKIP above,
 * applied per band instead of averaged over the whole candidate range. Not
 * exact: it can predict PNS on a band the real decision keeps (costing a
 * few bands of filter coverage for nothing) or miss one PNS fires on later
 * (spending coverage on synthetic noise, same as today) -- it is a
 * conservative, causal stand-in, not the real predicate. */
#define TNS_PNS_BAND_SFM_SKIP 0.85f

static int band_pns_likely(int s0, int s1, const float *spec)
{
    float sumlin = 0.0f, sumlog = 0.0f;
    int n = s1 - s0, i;

    for (i = s0; i < s1; i++) {
        float e = spec[i] * spec[i];
        if (e < TNS_MIN_ENERGY) e = TNS_MIN_ENERGY;
        sumlin += e;
        sumlog += logf(e);
    }
    return (expf(sumlog / (float)n) / (sumlin / (float)n)) > TNS_PNS_BAND_SFM_SKIP;
}

/* Scans [b_start, b_stop) upward for the first band predicted PNS-eligible
 * and returns its index (the cap TNS's filtered range must stop below), or
 * b_stop if none is predicted -- the common case, and the no-op path: the
 * caller then fits over the full [b_start, b_stop) exactly as before this
 * change. Only meaningful when pnslevel > 0: with pnslevel == 0,
 * pns_threshold in quantize.c is 0 and PNS never fires (target[sb] is never
 * negative), so probing would only invent false caps. */
static int tns_pns_cap(int b_start, int b_stop, int *sfbOffsetTable, const float *spec)
{
    int b;
    for (b = b_start; b < b_stop; b++) {
        if (band_pns_likely(sfbOffsetTable[b], sfbOffsetTable[b + 1], spec))
            return b;
    }
    return b_stop;
}

/* Analyse one element -- nch of them, sharing this same band range/
 * blockType/sfbOffsetTable per the frame-wide block-type decision in
 * BlockSwitch (nch==1 for an SCE, 2 for a CPE's two channels) -- and fit
 * each channel its own independent filter via tns_fit_range. It's expected
 * and fine for the channels of a CPE to disagree: one may get a filter
 * while the other doesn't (tnsDataPresent differs), since each is filtered
 * (or left unfiltered) independently before AACstereo's M/S/IS mixing runs. */
void TnsEncodeElement(TnsInfo **tnsInfos, float **specs, int nch,
                       int numBands, enum WINDOW_TYPE blockType, int *sfbOffsetTable,
                       int pnslevel)
{
    int b_start, b_stop, ch;

    for (ch = 0; ch < nch; ch++) {
        tnsInfos[ch]->tnsDataPresent = 0;
        tnsInfos[ch]->windowData.numFilters = 0;
    }

    /* Short windows already have the temporal resolution to not need TNS. */
    if (blockType == ONLY_SHORT_WINDOW)
        return;

    b_start = min(tnsInfos[0]->tnsMinBandNumberLong, numBands);
    b_stop = min(tnsInfos[0]->tnsMaxBandsLong, numBands);
    if (b_stop <= b_start)
        return;

    for (ch = 0; ch < nch; ch++) {
        TnsInfo *info = tnsInfos[ch];
        TnsFilterData fit;
        int cap = b_stop;

        if (pnslevel > 0)
            cap = tns_pns_cap(b_start, b_stop, sfbOffsetTable, specs[ch]);

        /* cap == b_start (first band already predicted PNS) falls straight
         * through tns_fit_range's own "not enough lines" gate (length <=
         * TNS_LPC_ORDER) below, so no separate too-little-room check is
         * needed here. */
        if (!tns_fit_range(b_start, cap, sfbOffsetTable, specs[ch], &fit))
            continue;

        if (cap < b_stop) {
            /* Retreat applies: the bitstream's TNS regions are top-anchored
             * -- the decoder walks each filter's region down from the top
             * of the spectrum (faad2 tns_decode_frame: bottom starts at
             * num_swb, each filter's top = previous filter's bottom) -- so
             * the real filter can't simply "end" below cap; a null filter
             * has to occupy the retreated-from region above it first.
             * order == 0 carries no direction/compress/coefficient fields
             * (see channels.c's WriteICS: `if (flt->order > 0)`) and the
             * decoder skips it harmlessly (`if (!tns_order) continue;`). */
            memset(&info->windowData.tnsFilter[0], 0, sizeof(TnsFilterData));
            info->windowData.tnsFilter[0].length = info->tnsNumSwbLong - cap;

            info->windowData.tnsFilter[1] = fit;
            info->windowData.tnsFilter[1].length = cap - b_start;

            info->windowData.numFilters = 2;
        } else {
            /* No-op path: identical to pre-retreat behaviour, declared from
             * b_start to the top of the spectrum rather than to b_stop,
             * over-declaring the region. */
            info->windowData.tnsFilter[0] = fit;
            info->windowData.tnsFilter[0].length = info->tnsNumSwbLong - b_start;
            info->windowData.numFilters = 1;
        }

        info->windowData.coefResolution = DEF_TNS_COEFF_RES;
        info->tnsDataPresent = 1;
    }
}
