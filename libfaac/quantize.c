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
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quantize.h"
#include "huff2.h"
#include "cpu_compute.h"

typedef void (*QuantizeFunc)(const float * __restrict xr, int * __restrict xi, int n, float sfacfix);

#if defined(HAVE_SSE2)
extern void quantize_sse2(const float * __restrict xr, int * __restrict xi, int n, float sfacfix);
#endif

static void quantize_scalar(const float * __restrict xr, int * __restrict xi, int n, float sfacfix)
{
    const float magic = MAGIC_NUMBER;
    int i;
    for (i = 0; i < n; i++)
    {
        float val = xr[i];
        float tmp = fabsf(val) * sfacfix;
        tmp = sqrtf(tmp * sqrtf(tmp));
        int q = (int)(tmp + magic);
        xi[i] = (val < 0) ? -q : q;
    }
}

static QuantizeFunc qfunc = quantize_scalar;
static float sfstep;
static float max_quant_limit;

#define SF_CHAIN_UNSET INT_MIN

void QuantizeInit(void)
{
#if defined(HAVE_SSE2)
    CPUCaps caps = get_cpu_caps();
    if (caps & CPU_CAP_SSE2)
        qfunc = quantize_sse2;
    else
#endif
        qfunc = quantize_scalar;

    sfstep = SF_STEP_AMPL;
    /* One-time constant: computed in double so the stored float is
     * correctly rounded, at zero runtime cost. */
    max_quant_limit = (float)pow((double)MAX_HUFF_ESC_VAL + 1.0 - (double)MAGIC_NUMBER, 4.0/3.0);
}

/* sfac and gain are coupled; clamping one forces a recompute of the other. */
static float gain_with_overflow_clamp(int *sfac, float band_peak)
{
    float gain = powf(10, *sfac / sfstep);
    if (band_peak > 0.0f && gain * band_peak > max_quant_limit)
    {
        gain = max_quant_limit / band_peak;
        *sfac = (int)floorf(log10f(gain) * sfstep);
        gain = powf(10, *sfac / sfstep);
    }
    return gain;
}

// masking target per scalefactor band: 0 marks a band inaudible
#define SILENCE_RMS            0.4f     // per-sample RMS gate for silence
#define AVG_ENERGY_WEIGHT      0.2f     // noise-like (average-energy) share of the target
#define PEAK_ENERGY_WEIGHT     0.45f    // tonal (peak-energy) share of the remainder
#define SHORT_BLOCK_TIGHTEN    0.45f    // short blocks get a tighter target per unit of energy
#define LOUDNESS_EXPONENT      0.4f     // Zwicker-ish loudness compression
#define AVG_ENERGY_FLOOR_FRAC  0.0010f  // -30 dB floor, keeps quiet bands from collapsing the target
#define PEAK_ENERGY_FLOOR_FRAC 0.0050f  // ~-23 dB floor, same purpose for peak energy

typedef struct
{
    float sum;      /* energy summed across group windows */
    float peak_amp; /* sqrt of the largest single-coefficient energy seen */
} BandEnergy;

static void measure_band_energy(const CoderInfo * __restrict ci, const float * __restrict xr0,
                                 int gnum, BandEnergy * __restrict out)
{
    int gsize = ci->groups.len[gnum];
    int sfb;

    for (sfb = 0; sfb < ci->sfbn; sfb++)
    {
        int lo = ci->sfb_offset[sfb], hi = ci->sfb_offset[sfb + 1];
        float sum = 0.0f, peak = 0.0f;
        int w;

        for (w = 0; w < gsize; w++)
        {
            const float *line = xr0 + w * BLOCK_LEN_SHORT + lo;
            int k;
            for (k = 0; k < hi - lo; k++)
            {
                float e = line[k] * line[k];
                sum += e;
                if (e > peak) peak = e;
            }
        }
        out[sfb].sum = sum;
        out[sfb].peak_amp = sqrtf(peak);
    }
}

static float loudness(float energy_ratio)
{
    return powf(energy_ratio, LOUDNESS_EXPONENT);
}

// masking sensitivity drops above ~4 kHz; de-emphasize bands toward Nyquist
static float treble_rolloff(int lo, int hi, float inv_block_len)
{
    return 10.0f / (1.0f + (float)(lo + hi) * inv_block_len);
}

static void derive_masking_targets(CoderInfo * __restrict ci, int gnum, float quality,
                                    const BandEnergy * __restrict be,
                                    float * __restrict target_out, float * __restrict avg_out)
{
    int gsize = ci->groups.len[gnum];
    int total_len = ci->sfb_offset[ci->sfbn];
    float group_total = 0.0f;
    int sfb;

    for (sfb = 0; sfb < ci->sfbn; sfb++)
        group_total += be[sfb].sum;

    // whole group below the silence gate: force every band to a zero target
    if (group_total < (SILENCE_RMS * SILENCE_RMS) * (float)(gsize * total_len))
    {
        for (sfb = 0; sfb < ci->sfbn; sfb++)
        {
            target_out[sfb] = 0.0f;
            avg_out[sfb] = 0.0f;
        }
        return;
    }

    int block_len = (ci->block_type == ONLY_SHORT_WINDOW) ? BLOCK_LEN_SHORT : BLOCK_LEN_LONG;
    float inv_block_len = 1.0f / (float)block_len;

    for (sfb = 0; sfb < ci->sfbn; sfb++)
    {
        int lo = ci->sfb_offset[sfb], hi = ci->sfb_offset[sfb + 1];
        float avg = be[sfb].sum;
        float peak = be[sfb].peak_amp * be[sfb].peak_amp;
        float ref = (group_total * inv_block_len) * (hi - lo);
        float target;

        // floor before pow(): formula is monotonic, so this floors the output too
        if (avg < ref * AVG_ENERGY_FLOOR_FRAC) avg = ref * AVG_ENERGY_FLOOR_FRAC;
        if (peak < ref * PEAK_ENERGY_FLOOR_FRAC) peak = ref * PEAK_ENERGY_FLOOR_FRAC;

        target = AVG_ENERGY_WEIGHT * loudness(avg / ref)
               + (1.0f - AVG_ENERGY_WEIGHT) * PEAK_ENERGY_WEIGHT * loudness(peak / ref);
        if (ci->block_type == ONLY_SHORT_WINDOW)
            target *= SHORT_BLOCK_TIGHTEN;
        target *= treble_rolloff(lo, hi, inv_block_len);

        target_out[sfb] = target * quality;
        avg_out[sfb] = be[sfb].sum;
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
                                   const float * __restrict target, const float * __restrict bandenrg,
                                   const float * __restrict bandpeak, int gnum, int pnslevel,
                                   int * __restrict p_last_abs)
{
    int gsize = ci->groups.len[gnum];
    float pns_threshold = 0.1f * (float)pnslevel;
    int sb;

    for (sb = 0; sb < ci->sfbn && ci->bandcnt < MAX_SCFAC_BANDS; sb++)
    {
        int band = ci->bandcnt;

        if (ci->book[band] != HCB_NONE)
        {
            ci->bandcnt++;
            continue;
        }

        int lo = ci->sfb_offset[sb], hi = ci->sfb_offset[sb + 1];
        int width = hi - lo;
        float avg_per_window = bandenrg[sb] / (float)gsize;
        float rms = sqrtf(avg_per_window / width);

        if (rms < SILENCE_RMS || target[sb] == 0.0f)
        {
            ci->book[band] = HCB_ZERO;
            ci->bandcnt++;
            continue;
        }

        /* PNS is fine inside TNS-covered bands -- the decoder's inverse
         * TNS filter shapes the substituted noise too. */
        if (target[sb] < pns_threshold)
        {
            ci->book[band] = HCB_PNS;
            ci->sf[band] += lrintf(log10f(avg_per_window) * SF_STEP_ENRG);
            ci->bandcnt++;
            continue;
        }

        int sfac = lrintf(log10f(target[sb] / rms) * sfstep);
        int sf_rel = SF_OFFSET - sfac;
        int sf_bias = ci->sf[band];

        if (sf_rel < SF_MIN)
        {
            ci->book[band] = HCB_ZERO;
        }
        else
        {
            int sf_abs;
            float gain = resolve_band_gain(sfac, sf_bias, bandpeak[sb], *p_last_abs, &sf_rel, &sf_abs);
            int xi[FRAME_LEN];
            int win;

            for (win = 0; win < gsize; win++)
                qfunc(xr0 + win * BLOCK_LEN_SHORT + lo, xi + win * width, width, gain);
            huffbook(ci, xi, gsize * width);
            *p_last_abs = sf_abs;
        }

        ci->sf[ci->bandcnt++] += sf_rel;
    }
}

void ResetCoderSections(CoderInfo *coder)
{
    int i, n = coder->groups.n * coder->sfbn;
    for (i = 0; i < n; i++)
    {
        coder->book[i] = HCB_NONE;
        coder->sf[i] = 0;
    }
}

int BlocQuant(CoderInfo * __restrict coder, float * __restrict xr, AACQuantCfg *aacquantCfg)
{
    float target[MAX_SCFAC_BANDS], bandenrg[MAX_SCFAC_BANDS];
    BandEnergy be[NSFB_LONG];
    float bandpeak[MAX_SCFAC_BANDS];
    int i, lastsf = SF_CHAIN_UNSET;
    float *gxr = xr;

    coder->bandcnt = coder->datacnt = 0;
    for (i = 0; i < coder->groups.n; i++)
    {
        int sfb;

        measure_band_energy(coder, gxr, i, be);
        for (sfb = 0; sfb < coder->sfbn; sfb++)
            bandpeak[sfb] = be[sfb].peak_amp;
        derive_masking_targets(coder, i, (float)aacquantCfg->quality / DEFQUAL, be, target, bandenrg);
        assign_band_codebooks(coder, gxr, target, bandenrg, bandpeak, i, aacquantCfg->pnslevel, &lastsf);
        gxr += coder->groups.len[i] * BLOCK_LEN_SHORT;
    }

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
    return 1;
}

void CalcBW(unsigned *bw, int rate, SR_INFO *sr, AACQuantCfg *aacquantCfg)
{
    int i, l = 0, max = *bw * (BLOCK_LEN_SHORT << 1) / rate;
    for (i = 0; i < sr->num_cb_short && l < max; i++)
        l += sr->cb_width_short[i];
    aacquantCfg->max_cbs = i;
    if (aacquantCfg->pnslevel) *bw = (float)l * rate / (BLOCK_LEN_SHORT << 1);

    l = 0, max = *bw * (BLOCK_LEN_LONG << 1) / rate;
    for (i = 0; i < sr->num_cb_long && l < max; i++)
        l += sr->cb_width_long[i];
    aacquantCfg->max_cbl = i;
    aacquantCfg->max_l = l;
    *bw = (float)l * rate / (BLOCK_LEN_LONG << 1);
}

// short-window grouping: keep spectrally-similar windows together so they
// share scalefactors; a transient onset starts a fresh group instead
#define GROUP_MIN_SFB     2    // bands below this are too coarse/DC-heavy to inform grouping
#define GROUP_ONSET_RATIO 3.0f  // running max/min energy ratio that counts as a transient

static void window_single_band_energy(const CoderInfo * __restrict cl, const float * __restrict w,
                                       int from_sfb, int to_sfb, float * __restrict e_out)
{
    const int * __restrict sfb_off = cl->sfb_offset;
    int sfb;
    for (sfb = from_sfb; sfb < to_sfb; sfb++)
    {
        float e = 0.0f;
        int k, start = sfb_off[sfb], end = sfb_off[sfb + 1];
        for (k = start; k < end; k++)
        {
            float val = w[k];
            e += val * val;
        }
        e_out[sfb] = e;
    }
}

void BlocGroup(float *xr, CoderInfo *coderInfo, AACQuantCfg *cfg)
{
    if (coderInfo->block_type != ONLY_SHORT_WINDOW)
    {
        coderInfo->groups.n = 1;
        coderInfo->groups.len[0] = 1;
        return;
    }

    int maxsfb = cfg->max_cbs;
    int cutoff = cfg->max_l / 8;
    int active_bands = maxsfb - GROUP_MIN_SFB;
    int onset_quorum = (active_bands * 3) >> 2;

    float band_e[NSFB_SHORT], run_min[NSFB_SHORT], run_max[NSFB_SHORT];
    int win, group_start = 0;

    coderInfo->groups.n = 0;

    for (win = 0; win < MAX_SHORT_WINDOWS; win++)
    {
        float *w = xr + win * BLOCK_LEN_SHORT;
        int k, sfb;

        for (k = cutoff; k < coderInfo->sfb_offset[maxsfb]; k++)
            w[k] = 0.0f;

        window_single_band_energy(coderInfo, w, GROUP_MIN_SFB, maxsfb, band_e);

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
    coderInfo->groups.len[coderInfo->groups.n++] = MAX_SHORT_WINDOWS - group_start;
}

void BlocGroupCPE(float *xrl, float *xrr, CoderInfo *cl, CoderInfo *cr, AACQuantCfg *cfg)
{
    if (cl->block_type != ONLY_SHORT_WINDOW)
    {
        cl->groups.n = 1;
        cl->groups.len[0] = 1;
        if (cr) { cr->groups.n = 1; cr->groups.len[0] = 1; }
        return;
    }
    if (cr && cr->block_type != ONLY_SHORT_WINDOW)
    {
        cr->groups.n = 1;
        cr->groups.len[0] = 1;
        xrr = NULL;
        cr = NULL;
    }

    int maxsfb = cfg->max_cbs;
    int cutoff = cfg->max_l / 8;
    int active_bands = maxsfb - GROUP_MIN_SFB;
    int onset_quorum = (active_bands * 3) >> 2;

    float band_el[NSFB_SHORT], run_min_l[NSFB_SHORT], run_max_l[NSFB_SHORT];
    float band_er[NSFB_SHORT], run_min_r[NSFB_SHORT], run_max_r[NSFB_SHORT];
    int win, group_start = 0;

    cl->groups.n = 0;
    if (cr) cr->groups.n = 0;

    for (win = 0; win < MAX_SHORT_WINDOWS; win++)
    {
        float *wl = xrl + win * BLOCK_LEN_SHORT;
        float *wr = xrr ? (xrr + win * BLOCK_LEN_SHORT) : NULL;
        int k, sfb;

        for (k = cutoff; k < cl->sfb_offset[maxsfb]; k++)
        {
            wl[k] = 0.0f;
            if (wr) wr[k] = 0.0f;
        }

        window_single_band_energy(cl, wl, GROUP_MIN_SFB, maxsfb, band_el);
        if (wr && cr) window_single_band_energy(cr, wr, GROUP_MIN_SFB, maxsfb, band_er);

        if (win == group_start)
        {
            for (sfb = GROUP_MIN_SFB; sfb < maxsfb; sfb++)
            {
                run_min_l[sfb] = run_max_l[sfb] = band_el[sfb];
                if (wr) run_min_r[sfb] = run_max_r[sfb] = band_er[sfb];
            }
            continue;
        }

        int onset_votes_l = 0, onset_votes_r = 0;
        for (sfb = GROUP_MIN_SFB; sfb < maxsfb; sfb++)
        {
            if (band_el[sfb] < run_min_l[sfb]) run_min_l[sfb] = band_el[sfb];
            if (band_el[sfb] > run_max_l[sfb]) run_max_l[sfb] = band_el[sfb];
            if (run_max_l[sfb] > GROUP_ONSET_RATIO * run_min_l[sfb]) onset_votes_l++;

            if (wr) {
                if (band_er[sfb] < run_min_r[sfb]) run_min_r[sfb] = band_er[sfb];
                if (band_er[sfb] > run_max_r[sfb]) run_max_r[sfb] = band_er[sfb];
                if (run_max_r[sfb] > GROUP_ONSET_RATIO * run_min_r[sfb]) onset_votes_r++;
            }
        }

        /* Joint short-window group split decision:
         * Trigger a joint group boundary split when both channels detect an onset,
         * OR when either channel detects a strong onset (> active_bands / 2).
         * For single-channel, trigger when onset_votes_l > onset_quorum. */
        int split_l = (onset_votes_l > onset_quorum);
        int split_r = (wr != NULL) && (onset_votes_r > onset_quorum);

        int joint_onset = (wr != NULL) ? ((split_l && split_r) ||
                                           onset_votes_l > (active_bands >> 1) ||
                                           onset_votes_r > (active_bands >> 1))
                                       : split_l;

        if (joint_onset)
        {
            int g_len = win - group_start;
            cl->groups.len[cl->groups.n++] = g_len;
            if (cr) cr->groups.len[cr->groups.n++] = g_len;
            group_start = win;
            for (sfb = GROUP_MIN_SFB; sfb < maxsfb; sfb++)
            {
                run_min_l[sfb] = run_max_l[sfb] = band_el[sfb];
                if (wr) run_min_r[sfb] = run_max_r[sfb] = band_er[sfb];
            }
        }
    }
    int last_g_len = MAX_SHORT_WINDOWS - group_start;
    cl->groups.len[cl->groups.n++] = last_g_len;
    if (cr) cr->groups.len[cr->groups.n++] = last_g_len;
}
