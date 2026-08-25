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

/* Short-window (eight-short) pooled-group filter: much lower order than the
 * long path (128 spectral lines per window vs 1024 leaves little room for
 * an 8-tap filter to generalize across a whole group) and a much stricter
 * acceptance bar, since a bad short filter is applied to -- and can hurt --
 * every window in its group at once, and short-window pre-echo is already
 * largely handled by the block switch itself. Independently tunable from
 * the long path's constants above rather than reusing them. */
#define TNS_SHORT_LPC_ORDER       5
#define TNS_SHORT_GAIN_LIMIT      3.2f
#define TNS_SHORT_MEASURED_GAIN   3.2f

/* ISO/IEC 13818-7/14496-3's TNS tool table for 128-sample (short) windows,
 * indexed by sampleRateIdx -- the short-window sibling of tns_sfb_range's
 * max column above. There's no separate short min-band column in the spec
 * table the way there is for long; band 0 is used as the short start band
 * (see TnsInit), which keeps this table to one row of maxima. */
static const unsigned char tns_max_bands_short[12] = {
    9, 9, 10, 14, 14, 14, 14, 14, 14, 14, 14, 14
};

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

        info->tnsNumSwbShort = hEncoder->srInfo->num_cb_short;
        info->tnsMaxBandsShort = min((int)tns_max_bands_short[fs], info->tnsNumSwbShort);
        info->tnsMinBandNumberShort = 0; /* simplest choice: the long path's
            b_start is a spec table lookup with no short-window sibling, and
            short's already-narrow, already-low-band-count range doesn't
            leave much room to trim a low end from anyway. */
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

/* Fits and applies one pooled TNS filter to a group of groupLen consecutive
 * short windows (starting at win0) of a single channel's spectrum, over
 * scalefactor bands [b_start, b_stop). freqBuff is window-major -- window w
 * occupies spec[w*BLOCK_LEN_SHORT .. w*BLOCK_LEN_SHORT+BLOCK_LEN_SHORT), and
 * sfbOffsetTable indexes within one window's 128 lines the same way for
 * every window (see BlocGroup, quantize.c) -- so each window's band range
 * lives at the same offsets, just shifted by w*BLOCK_LEN_SHORT.
 *
 * Concept follows the well-known pooled-group approach used by other AAC
 * encoders (one filter fit on the group's concatenated, RMS-normalized
 * spectra, applied per window) -- see tns.h -- but this is an independent
 * implementation against faac's own data structures and gates, not a port.
 *
 * On acceptance, fills windowData.tnsFilter[0] identically for every window
 * in the group and whitens each window's spectrum in place; returns 1. On
 * rejection, leaves both untouched and returns 0. */
static int tns_fit_pooled(int b_start, int b_stop, int *sfbOffsetTable,
                          float *spec, int win0, int groupLen, TnsInfo *info)
{
    int i_start = sfbOffsetTable[b_start];
    int clen = sfbOffsetTable[b_stop] - i_start;
    float pooled[BLOCK_LEN_LONG]; /* groupLen*clen <= MAX_SHORT_WINDOWS*BLOCK_LEN_SHORT == BLOCK_LEN_LONG */
    float bandrms[MAX_SHORT_WINDOWS][NSFB_SHORT];
    float floorrms[MAX_SHORT_WINDOWS];
    float r[TNS_MAX_ORDER + 1] = {0};
    float k[TNS_MAX_ORDER + 1] = {0};
    TnsFilterData filter = {0};
    int order, limit, i, b, w;
    float worst_gain;

    if (clen <= TNS_SHORT_LPC_ORDER)
        return 0;

    /* Per-window, per-band RMS-normalize (same idea as the long path's
     * tns_fit_range) before concatenating into one pooled signal -- so the
     * fit whitens across the whole group's bands and windows evenly,
     * instead of chasing whichever window/band happens to be loudest. */
    for (w = 0; w < groupLen; w++) {
        float *win = spec + (win0 + w) * BLOCK_LEN_SHORT;
        float maxrms = 0.0f;

        for (b = b_start; b < b_stop; b++) {
            int s0 = sfbOffsetTable[b], s1 = sfbOffsetTable[b + 1];
            float e = 0.0f, rms;

            for (i = s0; i < s1; i++)
                e += win[i] * win[i];
            rms = sqrtf(e / (float)(s1 - s0));
            bandrms[w][b] = rms;
            if (rms > maxrms) maxrms = rms;
        }
        floorrms[w] = maxrms * 0.01f;
        if (floorrms[w] < TNS_MIN_ENERGY) floorrms[w] = TNS_MIN_ENERGY;

        for (b = b_start; b < b_stop; b++) {
            int s0 = sfbOffsetTable[b], s1 = sfbOffsetTable[b + 1];
            float wgt = 1.0f / (bandrms[w][b] > floorrms[w] ? bandrms[w][b] : floorrms[w]);

            for (i = s0; i < s1; i++)
                pooled[w * clen + (i - i_start)] = win[i] * wgt;
        }
    }

    calc_autocorr_f(TNS_SHORT_LPC_ORDER, clen * groupLen, pooled, r);
    if (r[0] < TNS_MIN_ENERGY)
        return 0;
    {
        float gain = compute_lpc(TNS_SHORT_LPC_ORDER, r, k);
        if (gain < TNS_SHORT_GAIN_LIMIT || !isfinite(gain))
            return 0;
    }

    quantize_coeffs(TNS_SHORT_LPC_ORDER, DEF_TNS_COEFF_RES, k, filter.index);

    order = TNS_SHORT_LPC_ORDER;
    while (order > 0 && fabsf(k[order]) < (float)DEF_TNS_COEFF_THRESH)
        order--;
    if (order == 0)
        return 0;
    filter.order = order;
    filter.direction = 0; /* same reasoning as the long path: calc_autocorr_f
                              is direction-invariant and there's no per-window
                              transient-position signal available here. */

    filter.coefCompress = 1;
    limit = 1 << (DEF_TNS_COEFF_RES - 2);
    for (i = 1; i <= order; i++) {
        if (filter.index[i] < -limit || filter.index[i] >= limit) {
            filter.coefCompress = 0;
            break;
        }
    }

    finalize_filter(order, k, filter.aCoeffs);

    /* Strict, per-window re-check: every window in the group must clear the
     * measured bar on its own snippet (filtered independently -- a window's
     * predictor taps never reach across into a neighboring window), and the
     * WORST window governs acceptance. One bad-fitting window in the group
     * is enough to reject the whole group's filter, since accepting would
     * apply it there too. */
    worst_gain = INFINITY;
    for (w = 0; w < groupLen; w++) {
        float snippet[BLOCK_LEN_SHORT];
        float orig_e = 0.0f, filt_e = 0.0f;
        float g;

        memcpy(snippet, pooled + w * clen, clen * sizeof(float));
        for (i = 0; i < clen; i++)
            orig_e += snippet[i] * snippet[i];

        filter_spec(clen, order, filter.direction, filter.aCoeffs, snippet);
        for (i = 0; i < clen; i++)
            filt_e += snippet[i] * snippet[i];
        if (filt_e < TNS_MIN_ENERGY)
            filt_e = TNS_MIN_ENERGY;

        g = orig_e / filt_e;
        if (g < worst_gain) worst_gain = g;
    }
    if (worst_gain < TNS_SHORT_MEASURED_GAIN)
        return 0;

    /* Accepted: write the identical filter into every window's TNS side
     * info and whiten each window's spectrum in place. Declared from
     * b_start to the top of the short spectrum (over-declaring the region),
     * same convention the long path uses. */
    for (w = 0; w < groupLen; w++) {
        float *win = spec + (win0 + w) * BLOCK_LEN_SHORT;
        float snippet[BLOCK_LEN_SHORT];
        TnsWindowData *wd = &info->shortWindowData[win0 + w];

        memcpy(snippet, pooled + w * clen, clen * sizeof(float));
        filter_spec(clen, order, filter.direction, filter.aCoeffs, snippet);

        for (b = b_start; b < b_stop; b++) {
            int s0 = sfbOffsetTable[b], s1 = sfbOffsetTable[b + 1];
            float scale = bandrms[w][b] > floorrms[w] ? bandrms[w][b] : floorrms[w];

            for (i = s0; i < s1; i++)
                win[i] = snippet[i - i_start] * scale;
        }

        wd->numFilters = 1;
        wd->coefResolution = DEF_TNS_COEFF_RES;
        wd->tnsFilter[0] = filter;
        wd->tnsFilter[0].length = info->tnsNumSwbShort - b_start;
    }
    return 1;
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
                       WindowGroups **groups)
{
    int b_start, b_stop, ch;

    for (ch = 0; ch < nch; ch++) {
        tnsInfos[ch]->tnsDataPresent = 0;
        tnsInfos[ch]->windowData.numFilters = 0;
        if (blockType == ONLY_SHORT_WINDOW) {
            int w;
            for (w = 0; w < MAX_SHORT_WINDOWS; w++)
                tnsInfos[ch]->shortWindowData[w].numFilters = 0;
        }
    }

    if (blockType == ONLY_SHORT_WINDOW) {
        b_start = min(tnsInfos[0]->tnsMinBandNumberShort, numBands);
        b_stop = min(tnsInfos[0]->tnsMaxBandsShort, numBands);
        if (b_stop <= b_start)
            return;

        for (ch = 0; ch < nch; ch++) {
            TnsInfo *info = tnsInfos[ch];
            WindowGroups *g = groups[ch];
            int win0 = 0, gi;

            for (gi = 0; gi < g->n; gi++) {
                int groupLen = g->len[gi];

                if (tns_fit_pooled(b_start, b_stop, sfbOffsetTable, specs[ch],
                                   win0, groupLen, info))
                    info->tnsDataPresent = 1;
                win0 += groupLen;
            }
        }
        return;
    }

    b_start = min(tnsInfos[0]->tnsMinBandNumberLong, numBands);
    b_stop = min(tnsInfos[0]->tnsMaxBandsLong, numBands);
    if (b_stop <= b_start)
        return;

    for (ch = 0; ch < nch; ch++) {
        TnsInfo *info = tnsInfos[ch];

        if (!tns_fit_range(b_start, b_stop, sfbOffsetTable, specs[ch],
                           &info->windowData.tnsFilter[0]))
            continue;

        /* Declared from b_start to the top of the spectrum rather than to
         * b_stop, over-declaring the region. */
        info->windowData.tnsFilter[0].length = info->tnsNumSwbLong - b_start;
        info->windowData.numFilters = 1;
        info->windowData.coefResolution = DEF_TNS_COEFF_RES;
        info->tnsDataPresent = 1;
    }
}
