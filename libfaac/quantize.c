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

#include <limits.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "quantize.h"
#include "huff2.h"
#include "cpu_compute.h"
#include "stats.h"
#include "core_inject.h"

typedef int (*QuantizeFunc)(const float * __restrict xr, int * __restrict xi, int n4, float sfacfix);

#if defined(HAVE_SSE2)
extern int quantize_sse2(const float * __restrict xr, int * __restrict xi, int n4, float sfacfix);
#endif

/* Written so the loop auto-vectorizes: fabsf() makes the sqrtf() argument
 * provably non-negative (no errno path), the sign is re-applied as a
 * two's-complement mask, and the width is a known multiple of four. */
static int quantize_scalar(const float * __restrict xr, int * __restrict xi, int n4, float sfacfix)
{
    int i, maxq = 0;
    for (i = 0; i < 4 * n4; i++)
    {
        float val = xr[i];
        float tmp = fabsf(val * sfacfix);
        int q, m;
        tmp = sqrtf(tmp * sqrtf(tmp));
        q = (int)(tmp + MAGIC_NUMBER);
        m = -(val < 0.0f);
        if (q > maxq) maxq = q;
        xi[i] = (q ^ m) - m;
    }
    return maxq;
}

static QuantizeFunc qfunc = quantize_scalar;
static float sfstep;
static float max_quant_limit;

#define GAIN_LUT_SIZE 512
#define GAIN_LUT_BIAS 256
/* ISO 14496-3 §8.3.4 scalefactors use quarter-dB steps (1 unit = 2^(1/4) in amplitude).
 * Precomputed 2^(sfac/4) LUT eliminates repeated transcendental powf calls during gain coupling. */
static float gain_lut[GAIN_LUT_SIZE];
static float log10_width_sf_lut[128];

#define SF_CHAIN_UNSET INT_MIN

void QuantizeInit(void)
{
    int i;
#if defined(HAVE_SSE2)
    CPUCaps caps = get_cpu_caps();
    if (caps & CPU_CAP_SSE2)
        qfunc = quantize_sse2;
    else
#endif
        qfunc = quantize_scalar;

    sfstep = SF_STEP_AMPL;
    /* Pre-calculate lookup table for 10^(sfac / sfstep) = 2^(sfac / 4) */
    for (i = -GAIN_LUT_BIAS; i < GAIN_LUT_SIZE - GAIN_LUT_BIAS; i++)
        gain_lut[i + GAIN_LUT_BIAS] = powf(10.0f, (float)i / sfstep);

    /* Pre-multiply width logarithm by SF_STEP_ENRG (= sfstep / 2) */
    for (i = 1; i < 128; i++)
        log10_width_sf_lut[i] = log10f((float)i) * SF_STEP_ENRG;

    /* One-time constant: computed in double so the stored float is
     * correctly rounded, at zero runtime cost. */
    max_quant_limit = (float)pow((double)MAX_HUFF_ESC_VAL + 1.0 - (double)MAGIC_NUMBER, 4.0/3.0);
}

static inline float sfac_to_gain(int sfac)
{
    unsigned int idx = (unsigned int)(sfac + GAIN_LUT_BIAS);
    if (idx < GAIN_LUT_SIZE)
        return gain_lut[idx];
    return powf(10.0f, (float)sfac / sfstep);
}

/* sfac and gain are coupled; clamping one forces a recompute of the other. */
static float gain_with_overflow_clamp(int *sfac, float band_peak)
{
    float gain = sfac_to_gain(*sfac);
    if (band_peak > 0.0f && gain * band_peak > max_quant_limit)
    {
        gain = max_quant_limit / band_peak;
        *sfac = (int)floorf(log10f(gain) * sfstep);
        gain = sfac_to_gain(*sfac);
    }
    return gain;
}

// masking target per scalefactor band: 0 marks a band inaudible
#define SILENCE_RMS            0.4f     // per-sample RMS gate for silence
#define AVG_ENERGY_WEIGHT      0.2f     // noise-like (average-energy) share of the target
#define PEAK_ENERGY_WEIGHT     0.45f    // tonal (peak-energy) share of the remainder
#define LOUDNESS_EXPONENT      0.4f     // Zwicker-ish loudness compression
#define AVG_ENERGY_FLOOR_FRAC  0.0010f  // -30 dB floor, keeps quiet bands from collapsing the target
#define PEAK_ENERGY_FLOOR_FRAC 0.0050f  // ~-23 dB floor, same purpose for peak energy
#define QUIET_BAND_FRAC        0.0003f  // ~-35 dB below the frame mean: masked by the frame as a whole

typedef struct
{
    float sum;      /* energy summed across group windows */
    float peak_energy; /* mean of the per-window peak energies */
} BandEnergy;

static float measure_band_energy(const CoderInfo * __restrict ci, const float * __restrict xr0,
                                  int gnum, int cutoff, BandEnergy * __restrict out)
{
    int gsize = ci->groups.len[gnum];
    float group_total = 0.0f;
    int sfb;

    for (sfb = 0; sfb < ci->sfbn; sfb++)
    {
        int lo = ci->sfb_offset[sfb];
        int len = ci->sfb_offset[sfb + 1] - lo;
        float sum = 0.0f, peak = 0.0f;
        int w;

        /* Above the coded cutoff a short block is zero, so both the sum and the
         * peak there are exactly 0.0f. */
        if (lo >= cutoff)
        {
            out[sfb].sum = 0.0f;
            out[sfb].peak_energy = 0.0f;
            continue;
        }

        for (w = 0; w < gsize; w++)
        {
            const float * __restrict line = xr0 + w * BLOCK_LEN_SHORT + lo;
            float wpeak = 0.0f;
            int k;
            for (k = 0; k < len; k += 4)
            {
                float a = line[k], b = line[k + 1], c = line[k + 2], d = line[k + 3];
                float ea = a * a, eb = b * b, ec = c * c, ed = d * d;

                sum += ea; if (ea > wpeak) wpeak = ea;
                sum += eb; if (eb > wpeak) wpeak = eb;
                sum += ec; if (ec > wpeak) wpeak = ec;
                sum += ed; if (ed > wpeak) wpeak = ed;
            }

            /* Mean of the per-window peaks rather than the group maximum: a
             * maximum over more windows is systematically larger, which would
             * tie the tonal term to the grouping decision instead of the signal. */
            peak += wpeak;
        }
        peak /= (float)gsize;

        /* An M/S band is coded against half its weaker channel's L/R level;
         * its own level would code the side as finely as the mid. A half
         * that is silent on its own stays silent. */
        float ms = ci->msEl[gnum * ci->sfbn + sfb];
        if (ms > 0.0f && sum >= (SILENCE_RMS * SILENCE_RMS) * (float)(gsize * len))
        {
            float ref = 0.5f * fminf(ms, ci->msPeer[gnum * ci->sfbn + sfb]);
            if (sum > 0.0f)
                peak *= ref / sum;
            sum = ref;
        }

        out[sfb].sum = sum;
        out[sfb].peak_energy = peak;
        group_total += sum;
    }

    return group_total;
}

static float loudness(float energy_ratio)
{
    return powf(energy_ratio, LOUDNESS_EXPONENT);
}

// masking sensitivity drops above ~4 kHz; de-emphasize bands toward Nyquist
static float treble_rolloff(int lo, int hi, float slope_per_line)
{
    return 10.0f / (1.0f + (float)(lo + hi) * slope_per_line);
}

static void derive_masking_targets(CoderInfo * __restrict ci, int gnum, float quality,
                                    float treble_slope, const BandEnergy * __restrict be, float group_total,
                                    float * __restrict target_out)
{
    int gsize = ci->groups.len[gnum];
    int total_len = ci->sfb_offset[ci->sfbn];
    int sfb;

    // whole group below the silence gate: force every band to a zero target
    if (group_total < (SILENCE_RMS * SILENCE_RMS) * (float)(gsize * total_len))
    {
        for (sfb = 0; sfb < ci->sfbn; sfb++)
        {
            target_out[sfb] = 0.0f;
        }
        return;
    }

    int block_len = (ci->block_type == ONLY_SHORT_WINDOW) ? BLOCK_LEN_SHORT : BLOCK_LEN_LONG;
    float inv_block_len = 1.0f / (float)block_len;
    float slope_per_line = treble_slope * inv_block_len;

    for (sfb = 0; sfb < ci->sfbn; sfb++)
    {
        int lo = ci->sfb_offset[sfb], hi = ci->sfb_offset[sfb + 1];
        float avg = be[sfb].sum;
        float peak = be[sfb].peak_energy;
        float ref = (group_total * inv_block_len) * (hi - lo);
        /* avg and ref are both group totals, so their ratio is independent of
         * group size. peak is a single window's energy, so it needs a
         * single-window reference; otherwise the tonal term decays as 1/gsize. */
        float ref_win = ref / (float)gsize;
        float target;

        // floor before pow(): formula is monotonic, so this floors the output too
        if (avg < ref * AVG_ENERGY_FLOOR_FRAC) avg = ref * AVG_ENERGY_FLOOR_FRAC;
        if (peak < ref_win * PEAK_ENERGY_FLOOR_FRAC) peak = ref_win * PEAK_ENERGY_FLOOR_FRAC;

        target = AVG_ENERGY_WEIGHT * loudness(avg / ref)
               + (1.0f - AVG_ENERGY_WEIGHT) * PEAK_ENERGY_WEIGHT * loudness(peak / ref_win);
        target *= treble_rolloff(lo, hi, slope_per_line);
        /* A band this far under the frame is masked by it; the floors above
         * would otherwise pin its target at the floor. Falls at half the
         * loudness exponent. */
        if (be[sfb].sum < ref * QUIET_BAND_FRAC)
            target *= sqrtf(loudness(be[sfb].sum / (ref * QUIET_BAND_FRAC)));

        target_out[sfb] = target * quality;
    }
}

// per-band codebook assignment: zero / PNS / regular+Huffman

/* Re-derives gain after each clamp stage since scalefactor and gain are
 * coupled. Reports the final relative (bitstream-delta) and absolute
 * scalefactors. */
static float resolve_band_gain(int sfac, int sf_bias, float band_peak, int last_abs,
                                    int * __restrict out_sf_rel, int * __restrict out_sf_abs)
{
    float gain = gain_with_overflow_clamp(&sfac, band_peak);
    int sf_rel = SF_OFFSET - sfac;
    int sf_abs = sf_bias + sf_rel;

    if (last_abs != SF_CHAIN_UNSET)
    {
        int wanted = sf_abs - last_abs;
        int allowed = clamp_sf_diff(wanted);
        if (allowed != wanted)
        {
            sf_abs = last_abs + allowed;
            sf_rel = sf_abs - sf_bias;
            sfac = SF_OFFSET - sf_rel;
            gain = gain_with_overflow_clamp(&sfac, band_peak);
            sf_rel = SF_OFFSET - sfac;
            sf_abs = sf_bias + sf_rel;
        }
    }

    if (sf_abs < 0 || sf_abs > SF_MAX_ABS)
    {
        sf_abs = (sf_abs < 0) ? 0 : SF_MAX_ABS;
        sf_rel = sf_abs - sf_bias;
        sfac = SF_OFFSET - sf_rel;
        gain = gain_with_overflow_clamp(&sfac, band_peak);
        sf_rel = SF_OFFSET - sfac;
        sf_abs = sf_bias + sf_rel;
    }

    *out_sf_rel = sf_rel;
    *out_sf_abs = sf_abs;
    return gain;
}

static void assign_band_codebooks(CoderInfo * __restrict ci, const float * __restrict xr0,
                                   const float * __restrict target,
                                   const BandEnergy * __restrict be, int gnum, int pnslevel,
                                   int * __restrict p_last_abs, int * __restrict qs, int * __restrict p_qlen,
                                   int * __restrict p_sf_anchor)
{
    int gsize = ci->groups.len[gnum];
    static float env_pns_thresh = -1.0f;
    static int env_pns_min_sb = -1;
    if (env_pns_thresh < -0.5f) {
        const char *e = getenv("FAAC_PNS_THRESH");
        env_pns_thresh = e ? (float)atof(e) : -1.0f;
    }
    if (env_pns_min_sb < 0) {
        const char *e = getenv("FAAC_PNS_MIN_SB");
        env_pns_min_sb = e ? atoi(e) : 0;
    }

    float pns_threshold = (env_pns_thresh >= 0.0f) ? env_pns_thresh : (0.1f * (float)pnslevel);
    int pns_min_sb = env_pns_min_sb;
    int sb;

    /* Long blocks: pull each coded band's scalefactor 0.6 of the way towards
     * the mean of its coded neighbours (probe port of sf_smooth.patch). */
    int smooth_sfac[MAX_SCFAC_BANDS];
    int smooth = ci->block_type != ONLY_SHORT_WINDOW;
    static float alpha = -1.0f;
    if (alpha < 0.0f) { const char *e = getenv("FAAC_SF_SMOOTH"); alpha = e ? (float)atof(e) : 0.0f; /* probe default off: keeps Stage E-H controls; set FAAC_SF_SMOOTH=0.6 for PR #595 behaviour */ }
    if (alpha == 0.0f) smooth = 0;
    if (smooth)
    {
        int want[MAX_SCFAC_BANDS], coded[MAX_SCFAC_BANDS];
        int n = ci->sfbn;
        struct CoreInject *pcinj = CoreInjectGet();
        if (n > MAX_SCFAC_BANDS - ci->bandcnt) n = MAX_SCFAC_BANDS - ci->bandcnt;
        for (sb = 0; sb < n; sb++)
        {
            int band = ci->bandcnt + sb;
            int width = ci->sfb_offset[sb + 1] - ci->sfb_offset[sb];
            float avg_per_window = be[sb].sum / (float)gsize;
            int fcb = -1;
            coded[sb] = 0;
            smooth_sfac[sb] = INT_MIN;
            if (ci->book[band] != HCB_NONE)
                continue;
            if (pcinj && (CoreInjectFields(pcinj) & CI_CLASS))
            {
                int matched = CoreInjectLookup(pcinj, ci->ciFrame, ci->ciCh, band, 0,
                                                ci->sfbn, ci->groups.n, ci->groups.len,
                                                &fcb, NULL, NULL);
                if (!matched || fcb == 14 || fcb == 15) fcb = -1;
            }
            if (fcb == 0 || (fcb < 0 && (sqrtf(avg_per_window / width) < SILENCE_RMS || target[sb] == 0.0f)))
                continue;

            bool want_pns = (fcb == 13) || (fcb < 0 && sb >= pns_min_sb && (target[sb] < pns_threshold || (ci->msEl[band] > 0.0f && ci->msUsed && !ci->msUsed[band])));
            if (want_pns)
                continue; /* PNS bands are not smoothed */

            float log10_w_sf = (width < 128) ? log10_width_sf_lut[width] : log10f((float)width) * SF_STEP_ENRG;
            int sfac = lrintf(log10f(target[sb]) * sfstep - log10f(avg_per_window) * SF_STEP_ENRG + log10_w_sf);
            if (SF_OFFSET - sfac < SF_MIN)
                continue;
            smooth_sfac[sb] = sfac;
            want[sb] = ci->sf[band] - sfac;
            coded[sb] = 1;
        }
        for (sb = 0; sb < n; sb++)
        {
            if (!coded[sb])
                continue;
            int cnt = 0, sum = 0;
            if (sb > 0 && coded[sb - 1]) { sum += want[sb - 1]; cnt++; }
            if (sb + 1 < n && coded[sb + 1]) { sum += want[sb + 1]; cnt++; }
            if (cnt)
                smooth_sfac[sb] += lrintf(alpha * ((float)want[sb] - (float)sum / (float)cnt));
            /* smoothing must not turn a coded band into a zero band */
            if (SF_OFFSET - smooth_sfac[sb] < SF_MIN)
                smooth_sfac[sb] = SF_OFFSET - SF_MIN;
        }
    }

    for (sb = 0; sb < ci->sfbn && ci->bandcnt < MAX_SCFAC_BANDS; sb++)
    {
        int band = ci->bandcnt;

#ifdef FAAC_STATS
        g_faacStats.totalBands++;
#endif

        if (ci->book[band] != HCB_NONE)
        {
            ci->bandcnt++;
            continue;
        }

        int lo = ci->sfb_offset[sb], hi = ci->sfb_offset[sb + 1];
        int width = hi - lo;
        float avg_per_window = be[sb].sum / (float)gsize;
        float rms = sqrtf(avg_per_window / width);

        /* Log-domain identity: log10(target/rms) = log10(target) - 0.5*log10(avg) + 0.5*log10(width).
         * Reuses sf_enrg_avg (log10(avg) * SF_STEP_ENRG) shared with PNS to avoid division and sqrtf.
         * Computed unconditionally (harmless when the band ends up zeroed) so
         * a forced-PNS override below can reuse it exactly like the natural path. */
        float sf_enrg_avg = log10f(avg_per_window) * SF_STEP_ENRG;

        /* Probe-only (core_inject.c): fdk's ground-truth class for this band,
         * on frames whose window layout already matches FAAC's own. Only
         * zero/PNS/coded are forced; intensity (14/15) is left to stereo.c's
         * own decision, made earlier and unrelated to this loop. */
        int force_cb = -1;
        struct CoreInject *cinj = CoreInjectGet();
        if (cinj && (CoreInjectFields(cinj) & CI_CLASS))
        {
            int is_short = (ci->block_type == ONLY_SHORT_WINDOW);
            int matched = CoreInjectLookup(cinj, ci->ciFrame, ci->ciCh, band, is_short,
                                            ci->sfbn, ci->groups.n, ci->groups.len,
                                            &force_cb, NULL, NULL);
            CoreInjectNoteFrame(cinj, matched);
            if (!matched || force_cb == 14 || force_cb == 15) force_cb = -1;
        }

        if (force_cb == 0 || (force_cb < 0 && (rms < SILENCE_RMS || target[sb] == 0.0f)))
        {
            ci->book[band] = HCB_ZERO;
            ci->bandcnt++;
            continue;
        }

        /* PNS is fine inside TNS-covered bands -- the decoder's inverse
         * TNS filter shapes the substituted noise too. Decoders skip M/S on a
         * noise band, so an M/S band that wants noise goes back to L/R noise
         * in both channels, decided on the mid. Its flag isn't restored on a
         * retry, so the fallback sticks. A side band left under M/S drops to
         * zero instead, leaving the band mono. */
        bool want_pns = (force_cb == 13) || (force_cb < 0 && sb >= pns_min_sb && (target[sb] < pns_threshold || (ci->msEl[band] > 0.0f && ci->msUsed && !ci->msUsed[band])));
        if (want_pns)
        {
            int ms_cross = ci->msEl[band] > 0.0f;
            if (ms_cross)
            {
                CoderInfo *r = ci->partner;
                if (!r)
                {
                    ci->book[band] = HCB_ZERO;
                    ci->bandcnt++;
                    continue;
                }
                ci->msUsed[band] = 0;
                sf_enrg_avg = log10f(ci->msEl[band] / (float)gsize) * SF_STEP_ENRG;
                r->book[band] = HCB_PNS;
                r->sf[band] += lrintf(log10f(r->msEl[band] / (float)gsize) * SF_STEP_ENRG);
            }
            ci->book[band] = HCB_PNS;
#ifdef FAAC_STATS
            g_faacStats.pnsBands++;
#endif
            /* Probe-only (core_inject.c): fdk's noise-level SHAPE for a PNS
             * band, same anchor+shape idea as the coded-band case below.
             * Skipped on the M/S cross-channel path (ms_cross) -- that path
             * already overwrites both channels' sf through the partner
             * pointer above, and forcing on top of that here isn't scoped
             * for this probe. */
            struct CoreInject *sfcinj = CoreInjectGet();
            int forced_pns = 0;
            if (!ms_cross && sfcinj && (CoreInjectFields(sfcinj) & CI_SF)
                && *p_sf_anchor != SF_CHAIN_UNSET)
            {
                int donor_cb, sf_shape;
                int is_short = (ci->block_type == ONLY_SHORT_WINDOW);
                int matched = CoreInjectLookup(sfcinj, ci->ciFrame, ci->ciCh, band, is_short,
                                                ci->sfbn, ci->groups.n, ci->groups.len,
                                                &donor_cb, &sf_shape, NULL);
                if (matched && donor_cb == 13)
                {
                    ci->sf[band] = *p_sf_anchor + sf_shape;
                    forced_pns = 1;
                }
            }
            if (!forced_pns)
                ci->sf[band] += lrintf(sf_enrg_avg);
            ci->bandcnt++;
            continue;
        }

        float log10_w_sf = (width < 128) ? log10_width_sf_lut[width] : log10f((float)width) * SF_STEP_ENRG;
        int sfac = lrintf(log10f(target[sb]) * sfstep - sf_enrg_avg + log10_w_sf);
        if (smooth && smooth_sfac[sb] != INT_MIN)
            sfac = smooth_sfac[sb];
        int sf_bias = ci->sf[band];

        /* Probe-only (core_inject.c): fdk's scalefactor SHAPE for this coded
         * band (fdk's own absolute sf minus fdk's own global_gain), replacing
         * the natural target-derived `sfac` outright -- not a post-hoc
         * relabel of ci->sf, so the qfunc() call below actually quantizes at
         * the forced step, the way class-forcing without this was shown to
         * NOT do (and paid for on 15-Good-evening in Step W). Floats on
         * FAAC's own anchor: the first band this frame that ends up
         * naturally coded is left unforced (see the anchor-set below) so it,
         * and the `quality` knob the outer rate-control retry loop moves,
         * still sets the reference level the whole forced shape rides on. */
        int wanted_sf_abs = 0, forcing_sf = 0;
        struct CoreInject *sfcinj = CoreInjectGet();
        if (sfcinj && (CoreInjectFields(sfcinj) & CI_SF) && *p_sf_anchor != SF_CHAIN_UNSET)
        {
            int donor_cb, sf_shape;
            int is_short = (ci->block_type == ONLY_SHORT_WINDOW);
            int matched = CoreInjectLookup(sfcinj, ci->ciFrame, ci->ciCh, band, is_short,
                                            ci->sfbn, ci->groups.n, ci->groups.len,
                                            &donor_cb, &sf_shape, NULL);
            if (matched && donor_cb >= 1 && donor_cb <= 11)
            {
                wanted_sf_abs = *p_sf_anchor + sf_shape;
                sfac = SF_OFFSET - (wanted_sf_abs - sf_bias);
                forcing_sf = 1;
            }
        }

        int sf_rel = SF_OFFSET - sfac;

        if (sf_rel < SF_MIN)
        {
            ci->book[band] = HCB_ZERO;
            if (forcing_sf) CoreInjectNoteSf(sfcinj, 1, 1); /* wanted below SF_MIN: can't honor at all */
        }
        else
        {
            int sf_abs;
            float gain = resolve_band_gain(sfac, sf_bias, sqrtf(be[sb].peak_energy), *p_last_abs, &sf_rel, &sf_abs);
            int *xi = qs + *p_qlen;
            int win, maxq = 0;

            for (win = 0; win < gsize; win++)
            {
                int qm = qfunc(xr0 + win * BLOCK_LEN_SHORT + lo, xi + win * width, width >> 2, gain);
                if (qm > maxq) maxq = qm;
            }
            /* huffbook picks the final book; record the lowest that covers maxq */
            ci->book[band] = !maxq ? HCB_ZERO : maxq <= LAV_1 ? HCB_1 : maxq <= LAV_2 ? HCB_3
                           : maxq <= LAV_4 ? HCB_5 : maxq <= LAV_7 ? HCB_7 : maxq <= LAV_12 ? HCB_9 : HCB_ESC;
            if (maxq)
                *p_qlen += gsize * width;
            *p_last_abs = sf_abs;

            if (forcing_sf)
                CoreInjectNoteSf(sfcinj, 1, sf_abs != wanted_sf_abs);
            else if (*p_sf_anchor == SF_CHAIN_UNSET && maxq)
                /* First band this frame that ends up naturally coded: this
                 * IS the anchor every later sf-forced band floats on (not
                 * forced itself, so the anchor still moves with `quality`). */
                *p_sf_anchor = sf_abs;
        }

        ci->sf[ci->bandcnt++] += sf_rel;
    }
}

void ResetCoderSections(CoderInfo *coder)
{
    int i, n = coder->groups.n * coder->sfbn;
    coder->partner = NULL;
    coder->useRef = 0;
    coder->msUsed = NULL;
    coder->msPeer = NULL;
    /* msEl[] is only read below n. */
    memset(coder->msEl, 0, n * sizeof(coder->msEl[0]));
    for (i = 0; i < n; i++)
    {
        coder->book[i] = HCB_NONE;
        coder->sf[i] = 0;
    }
}

/* The band scans here and in stereo.c step four coefficients with no remainder,
 * which holds only because every band width in srInfo[] is a multiple of four.
 * That is a property of the tables, so verify the one actually selected. */
static void assert_band_widths_align(const CoderInfo * __restrict ci)
{
    int sfb;

    for (sfb = 0; sfb < ci->sfbn; sfb++)
        assert((ci->sfb_offset[sfb + 1] - ci->sfb_offset[sfb]) % 4 == 0);
}

/* Decoders disagree on whether an intensity band copies the left channel's
 * substituted noise or its still-empty lines, so an intensity band over a
 * noise band can decode silent. Code it as noise at the level the intensity
 * position implies (both are 1.5 dB steps). Runs after the left
 * channel's BlocQuant and before the right's. */
static void ResolveIntensityNoise(const CoderInfo *left, CoderInfo *right)
{
    for (int i = 0; i < left->bandcnt; i++) {
        int b = right->book[i];
        if (left->book[i] == HCB_PNS && (b == HCB_INTENSITY || b == HCB_INTENSITY2)) {
            right->book[i] = HCB_PNS;
            right->sf[i] = left->sf[i] - right->sf[i];
        }
    }
}

int BlocQuant(CoderInfo * __restrict coder, float * __restrict xr, AACQuantCfg *aacquantCfg)
{
    float target[MAX_SCFAC_BANDS];
    BandEnergy be[NSFB_LONG];
    int qs[FRAME_LEN];
    int i, lastsf = SF_CHAIN_UNSET, qlen = 0;
    /* Probe-only (core_inject.c): the sf-injection anchor, shared across every
     * group/band in this channel's frame (same scope as `lastsf`/global_gain
     * below) -- the first naturally-coded band sets it once, and every later
     * `sf`-forced band's absolute scalefactor floats on it. */
    int sfAnchor = SF_CHAIN_UNSET;
    float *gxr = xr;
    int cutoff = (coder->block_type == ONLY_SHORT_WINDOW)
               ? aacquantCfg->max_l / 8 : coder->sfb_offset[coder->sfbn];

    assert_band_widths_align(coder);

    coder->bandcnt = coder->datacnt = 0;
    for (i = 0; i < coder->groups.n; i++)
    {
        float group_total = measure_band_energy(coder, gxr, i, cutoff, be);
        if (coder->useRef)
            group_total = coder->refTotal[i];

        derive_masking_targets(coder, i, (float)aacquantCfg->quality / DEFQUAL, aacquantCfg->treble_slope, be, group_total, target);
        /* Ladder measurement only: expose the normal quantizer's per-band
         * masking target and energy beside the forced-SF spectrum probe. */
        if (getenv("FAAC_G_MASK_DUMP"))
        {
            static FILE *mask_dump;
            static int mask_dump_tried;
            if (!mask_dump_tried)
            {
                mask_dump_tried = 1;
                mask_dump = fopen(getenv("FAAC_G_MASK_DUMP"), "a");
            }
            if (mask_dump)
            {
                for (int sb = 0; sb < coder->sfbn; sb++)
                    fprintf(mask_dump, "M %d %d %d %d %.9g %.9g %.9g\n",
                            coder->ciFrame, coder->ciCh, i, sb,
                            target[sb], be[sb].sum, be[sb].peak_energy);
                fflush(mask_dump);
            }
        }
        assign_band_codebooks(coder, gxr, target, be, i, aacquantCfg->pnslevel, &lastsf, qs, &qlen, &sfAnchor);
        gxr += coder->groups.len[i] * BLOCK_LEN_SHORT;
    }
    huffbook(coder, qs);

    // global_gain must come from a regular band: it's an 8-bit bitstream field,
    // and intensity/PNS bands store stereo-position/noise-energy on a different
    // (possibly negative) scale that would truncate and desync the decoder.
    coder->global_gain = 0;
    for (i = 0; i < coder->bandcnt; i++)
    {
        int b = coder->book[i];
        if (b && b != HCB_INTENSITY && b != HCB_INTENSITY2 && b != HCB_PNS)
        {
            coder->global_gain = coder->sf[i];
            break;
        }
    }

    int lastis = 0, lastpns = coder->global_gain - SF_PNS_OFFSET;
    for (i = 0; i < coder->bandcnt; i++)
    {
        int b = coder->book[i];
        if (b == HCB_INTENSITY || b == HCB_INTENSITY2)
        {
            int diff = clamp_sf_diff(coder->sf[i] - lastis);
            lastis += diff;
            coder->sf[i] = lastis;
        }
        else if (b == HCB_PNS)
        {
            int diff = clamp_sf_diff(coder->sf[i] - lastpns);
            lastpns += diff;
            coder->sf[i] = lastpns;
        }
    }
    if (coder->partner)
        ResolveIntensityNoise(coder, coder->partner);
    return 1;
}

/* sfbOffsetShort/Long are filled as a side effect of the same bandwidth walk
 * that picks max_cbs/max_cbl -- a prefix sum over the same table, to the same
 * bound, so there's nothing left for a caller to redo afterward. Only
 * [0, max_cbs] and [0, max_cbl] are written; callers never index sfb_offset
 * past their own sfbn, which is exactly max_cbs/max_cbl. */
void CalcBW(unsigned *bw, int rate, SR_INFO *sr, AACQuantCfg *aacquantCfg,
            int *sfbOffsetShort, int *sfbOffsetLong)
{
    int i, l = 0, max = *bw * (BLOCK_LEN_SHORT << 1) / rate;
    for (i = 0; i < sr->num_cb_short && l < max; i++) {
        sfbOffsetShort[i] = l;
        l += sr->cb_width_short[i];
    }
    sfbOffsetShort[i] = l;
    aacquantCfg->max_cbs = i;

    /* Snap on the long grid only. Both loops round up, so also snapping to the
     * 14-band short grid compounds two round-ups and leaves the cutoff far
     * coarser than the long grid can express. */

    l = 0, max = *bw * (BLOCK_LEN_LONG << 1) / rate;
    for (i = 0; i < sr->num_cb_long && l < max; i++) {
        sfbOffsetLong[i] = l;
        l += sr->cb_width_long[i];
    }
    sfbOffsetLong[i] = l;
    aacquantCfg->max_cbl = i;
    aacquantCfg->max_l = l;
    *bw = (float)l * rate / (BLOCK_LEN_LONG << 1);
}

// short-window grouping: keep spectrally-similar windows together so they
// share scalefactors; a transient onset starts a fresh group instead
#define GROUP_MIN_SFB     2    // bands below this are too coarse/DC-heavy to inform grouping
#define GROUP_ONSET_RATIO 3.0f  // running max/min energy ratio that counts as a transient

/* Four independent lanes: strict FP forbids reordering one float accumulator,
 * so a single-sum loop can never vectorize. n4 is the width in quads. */
static inline float band_energy_sum(const float * __restrict line, int n4)
{
    float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
    int k;

    for (k = 0; k < 4 * n4; k += 4)
    {
        s0 += line[k] * line[k];
        s1 += line[k + 1] * line[k + 1];
        s2 += line[k + 2] * line[k + 2];
        s3 += line[k + 3] * line[k + 3];
    }
    return (s0 + s1) + (s2 + s3);
}

/* Accumulates, so a CPE can sum both channels into one energy vector. */
static void window_band_energy(const CoderInfo * __restrict ci, const float * __restrict w,
                                int from_sfb, int to_sfb, float * __restrict e_out)
{
    int sfb;
    for (sfb = from_sfb; sfb < to_sfb; sfb++)
    {
        int lo = ci->sfb_offset[sfb];
        e_out[sfb] += band_energy_sum(w + lo, (ci->sfb_offset[sfb + 1] - lo) >> 2);
    }
}

/* Splits the 8 short windows into groups at detected onsets. For a CPE
 * (ci_r != NULL) both channels' band energies are summed so the pair shares one
 * grouping, which the bitstream requires anyway. Long blocks never reach here. */
void BlocGroup(CoderInfo *coderInfo, float *xr, CoderInfo *ci_r, float *xr_r, AACQuantCfg *cfg)
{
    CoderInfo *ci[2];
    float *xrs[2];
    int nch = ci_r ? 2 : 1;

    int maxsfb = cfg->max_cbs;
    int cutoff = cfg->max_l / 8;
    int active_bands = maxsfb - GROUP_MIN_SFB;
    int onset_quorum = (active_bands * 3) >> 2;

    float band_e[NSFB_SHORT], run_min[NSFB_SHORT], run_max[NSFB_SHORT];
    int win, group_start = 0;

    ci[0] = coderInfo; ci[1] = ci_r;
    xrs[0] = xr;       xrs[1] = xr_r;

    coderInfo->groups.n = 0;

    /* Probe-only (core_inject.c): with `win` injection active, fdk's own
     * group boundaries replace the onset detector below on frames it has an
     * ONLY_SHORT record for. coderInfo here is always the element-primary
     * channel (ciCh == 0): frame.c only ever passes el->channels[0] as `a`
     * to BlocGroup's first argument. The per-window zeroing above cutoff
     * still has to run every window regardless of which path picks the
     * groups, so it stays outside this branch. */
    int use_donor_groups = 0;
    int donor_groups = 0, donor_glen[8];
    {
        struct CoreInject *cinj = CoreInjectGet();
        if (cinj && (CoreInjectFields(cinj) & CI_WIN))
        {
            int donor_seq, donor_max_sfb;
            if (CoreInjectLookupWin(cinj, coderInfo->ciFrame, 0, &donor_seq,
                                     &donor_max_sfb, &donor_groups, donor_glen) &&
                donor_seq == ONLY_SHORT_WINDOW && donor_groups >= 1 && donor_groups <= 8)
            {
                int g, sum = 0;
                for (g = 0; g < donor_groups; g++) sum += donor_glen[g];
                if (sum == MAX_SHORT_WINDOWS) use_donor_groups = 1;
            }
        }
    }

    for (win = 0; win < MAX_SHORT_WINDOWS; win++)
    {
        int k, sfb, c;

        for (sfb = GROUP_MIN_SFB; sfb < maxsfb; sfb++)
            band_e[sfb] = 0.0f;

        for (c = 0; c < nch; c++)
        {
            float *w = xrs[c] + win * BLOCK_LEN_SHORT;

            for (k = cutoff; k < ci[c]->sfb_offset[maxsfb]; k++)
                w[k] = 0.0f;

            if (!use_donor_groups)
                window_band_energy(ci[c], w, GROUP_MIN_SFB, maxsfb, band_e);
        }

        if (use_donor_groups)
            continue;

        if (win == group_start)
        {
            for (sfb = GROUP_MIN_SFB; sfb < maxsfb; sfb++)
                run_min[sfb] = run_max[sfb] = band_e[sfb];
            continue;
        }

        int onset_votes = 0;
        for (sfb = GROUP_MIN_SFB; sfb < maxsfb; sfb++)
        {
            if (band_e[sfb] < run_min[sfb]) run_min[sfb] = band_e[sfb];
            if (band_e[sfb] > run_max[sfb]) run_max[sfb] = band_e[sfb];
            if (run_max[sfb] > GROUP_ONSET_RATIO * run_min[sfb]) onset_votes++;
        }

        if (onset_votes > onset_quorum)
        {
            coderInfo->groups.len[coderInfo->groups.n++] = win - group_start;
            group_start = win;
            for (sfb = GROUP_MIN_SFB; sfb < maxsfb; sfb++)
                run_min[sfb] = run_max[sfb] = band_e[sfb];
        }
    }

    if (use_donor_groups)
    {
        int g;
        coderInfo->groups.n = donor_groups;
        for (g = 0; g < donor_groups; g++)
            coderInfo->groups.len[g] = donor_glen[g];
    }
    else
    {
        coderInfo->groups.len[coderInfo->groups.n++] = MAX_SHORT_WINDOWS - group_start;
    }

    if (ci_r)
        ci_r->groups = coderInfo->groups;
}
