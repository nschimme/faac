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
#include "tuning.h"

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
/* Zwicker-ish loudness compression. This is the shape parameter: it alone
 * decides how much of the allocation follows band energy. At 0.4 the result is
 * close to white (gain ~ E^-0.1, noise ~ E^0.2), which looks flat for a
 * perceptual coder and reads like an oversight.
 *
 * It measures as an optimum, not an oversight. At 64 kbps/ch (ViSQOL, n=10
 * music, paired) every direction is worse: 0.25 is -0.0074, 0.32 is -0.0049,
 * 0.5 is -0.0022, 0.6 is -0.0065. Unimodal, peaked on the shipped value.
 *
 * The flatness is structural rather than a badly chosen constant: with no
 * spreading function and no tonality estimate, the model has no basis for a
 * steeper allocation, and steepening this exponent alone just misallocates. */
#define LOUDNESS_EXPONENT      0.4f
#define AVG_ENERGY_FLOOR_FRAC  0.0010f  // -30 dB floor, keeps quiet bands from collapsing the target
/* ~-23 dB floor, same purpose for peak energy -- but it is not only a guard:
 * it binds on 34-50% of bands, so for two fifths of the spectrum the tonal term
 * is this constant rather than a measurement of peak energy.
 *
 * That reach is not headroom. Screened at 64 kbps/ch (ViSQOL, n=10 music,
 * paired): 0.0025 is +0.0008, 0.01 is -0.0023, 0.02 is -0.0114. Halving does
 * nothing measurable and raising is monotonically worse, so the shipped value
 * sits at or just below the knee. Left alone deliberately. */
#define PEAK_ENERGY_FLOOR_FRAC 0.0050f

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

static float loudness(float energy_ratio, float exponent)
{
    return powf(energy_ratio, exponent);
}

#ifdef FAAC_TUNING
/* Census of the masking model, so a constant is only swept once its branch is
 * known to bind. Every one of these constants applies to every band of every
 * frame, which makes them look like large levers; a floor that engages on 0.1%
 * of bands is not a lever at all, whatever its value.
 *
 * Counts bands, not frames: the silence gate is the one group-level decision
 * and is counted separately. */
static struct {
    long groups, silent_groups;
    long bands, avg_floored, peak_floored, zeroed, pns;
    long long block_type[4];
    long pns_by_type[4], bands_by_type[4];
} psyCensus;

/* Block types are not all reachable in every run -- a corpus with no transients
 * never produces a START -- so the denominator is legitimately zero. */
static double PsyPnsPct(int blockType)
{
    long n = psyCensus.bands_by_type[blockType];

    return n ? 100.0 * psyCensus.pns_by_type[blockType] / n : 0.0;
}

static void PsyCensusReport(void)
{
    long b = psyCensus.bands ? psyCensus.bands : 1;
    long g = psyCensus.groups ? psyCensus.groups : 1;

    fprintf(stderr,
            "PSY_STATS groups=%ld silent=%.2f%% bands=%ld "
            "avg_floor=%.2f%% peak_floor=%.2f%% zero=%.2f%% pns=%.2f%% "
            /* Per group, not per frame: a short frame contributes several
             * groups, so this over-weights short relative to BS_STATS. */
            "groups_by_type=L%lld/S%lld/LS%lld/SL%lld "
            "pns_by_type=L%.1f%%/S%.1f%%/LS%.1f%%/SL%.1f%%\n",
            psyCensus.groups, 100.0 * psyCensus.silent_groups / g,
            psyCensus.bands,
            100.0 * psyCensus.avg_floored / b,
            100.0 * psyCensus.peak_floored / b,
            100.0 * psyCensus.zeroed / b,
            100.0 * psyCensus.pns / b,
            psyCensus.block_type[ONLY_LONG_WINDOW],
            psyCensus.block_type[ONLY_SHORT_WINDOW],
            psyCensus.block_type[LONG_SHORT_WINDOW],
            psyCensus.block_type[SHORT_LONG_WINDOW],
            PsyPnsPct(ONLY_LONG_WINDOW),
            PsyPnsPct(ONLY_SHORT_WINDOW),
            PsyPnsPct(LONG_SHORT_WINDOW),
            PsyPnsPct(SHORT_LONG_WINDOW));
}
#endif

/* Global scale of every masking target. Degenerate with treble_rolloff's
 * numerator, PEAK_ENERGY_WEIGHT, SHORT_BLOCK_TIGHTEN and quality/DEFQUAL: all
 * five multiply the same scalar. Exposed only so Stage 1 can demonstrate that
 * degeneracy, never to be swept for a win -- in VBR it is just a different -q,
 * and in ABR the rate controller absorbs it and re-emits it as changed quality,
 * which also drives stereo and PNS. */
#define PSY_GLOBAL_SCALE 10.0f

// masking sensitivity drops above ~4 kHz; de-emphasize bands toward Nyquist
static float treble_rolloff(int lo, int hi, float inv_block_len)
{
    float scale = PSY_GLOBAL_SCALE;

    FAAC_TUNE_F(scale, psy_global);
    return scale / (1.0f + (float)(lo + hi) * inv_block_len);
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

#ifdef FAAC_TUNING
    if (FAAC_TUNE_ON(psy_stats))
    {
        psyCensus.groups++;
        psyCensus.block_type[ci->block_type & 3]++;
        /* Periodic rather than atexit: matches BS_STATS, and keeps short clips
         * from producing no line at all. */
        if (psyCensus.groups % 200 == 0)
            PsyCensusReport();
    }
#endif

    // whole group below the silence gate: force every band to a zero target
    if (group_total < (SILENCE_RMS * SILENCE_RMS) * (float)(gsize * total_len))
    {
#ifdef FAAC_TUNING
        if (FAAC_TUNE_ON(psy_stats))
            psyCensus.silent_groups++;
#endif
        for (sfb = 0; sfb < ci->sfbn; sfb++)
        {
            target_out[sfb] = 0.0f;
            avg_out[sfb] = 0.0f;
        }
        return;
    }

    int block_len = (ci->block_type == ONLY_SHORT_WINDOW) ? BLOCK_LEN_SHORT : BLOCK_LEN_LONG;
    float inv_block_len = 1.0f / (float)block_len;

    /* Hoisted once per group: the sweep must not add a per-band branch to a
     * loop that runs for every band of every frame. */
    float avg_floor = AVG_ENERGY_FLOOR_FRAC;
    float peak_floor = PEAK_ENERGY_FLOOR_FRAC;
    float loud_exp = LOUDNESS_EXPONENT;
    float avg_weight = AVG_ENERGY_WEIGHT;

    FAAC_TUNE_F(avg_floor, psy_avg_floor);
    FAAC_TUNE_F(peak_floor, psy_peak_floor);
    FAAC_TUNE_F(loud_exp, psy_loudness);
    FAAC_TUNE_F(avg_weight, psy_avg_weight);

    for (sfb = 0; sfb < ci->sfbn; sfb++)
    {
        int lo = ci->sfb_offset[sfb], hi = ci->sfb_offset[sfb + 1];
        float avg = be[sfb].sum;
        float peak = be[sfb].peak_amp * be[sfb].peak_amp;
        float ref = (group_total * inv_block_len) * (hi - lo);
        float target;

        // floor before pow(): formula is monotonic, so this floors the output too
#ifdef FAAC_TUNING
        if (FAAC_TUNE_ON(psy_stats))
        {
            psyCensus.bands++;
            if (avg < ref * avg_floor) psyCensus.avg_floored++;
            if (peak < ref * peak_floor) psyCensus.peak_floored++;
        }
#endif
        if (avg < ref * avg_floor) avg = ref * avg_floor;
        if (peak < ref * peak_floor) peak = ref * peak_floor;

        target = avg_weight * loudness(avg / ref, loud_exp)
               + (1.0f - avg_weight) * PEAK_ENERGY_WEIGHT * loudness(peak / ref, loud_exp);
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

#ifdef FAAC_TUNING
        if (FAAC_TUNE_ON(psy_stats))
            psyCensus.bands_by_type[ci->block_type & 3]++;
#endif
        float rms = sqrtf(avg_per_window / width);

        if (rms < SILENCE_RMS || target[sb] == 0.0f)
        {
#ifdef FAAC_TUNING
            if (FAAC_TUNE_ON(psy_stats))
                psyCensus.zeroed++;
#endif
            ci->book[band] = HCB_ZERO;
            ci->bandcnt++;
            continue;
        }

        /* PNS is fine inside TNS-covered bands -- the decoder's inverse
         * TNS filter shapes the substituted noise too. */
        if (target[sb] < pns_threshold)
        {
#ifdef FAAC_TUNING
            if (FAAC_TUNE_ON(psy_stats))
            {
                psyCensus.pns++;
                psyCensus.pns_by_type[ci->block_type & 3]++;
            }
#endif
            ci->book[band] = HCB_PNS;
            ci->sf[band] += lrintf(log10f(avg_per_window) * SF_STEP_ENRG);
            ci->bandcnt++;
            continue;
        }

        int sfac = lrintf(log10f(target[sb] / rms) * sfstep);
        int sf_rel = SF_OFFSET - sfac;
        int sf_bias = ci->sf[band];

        /* Belt-and-braces: provably unreachable, and measured so. `target` is
         * bounded above (peak/ref <= block_len/width, loudness compresses by
         * ^0.4, treble_rolloff <= 10, quality <= MAXQUAL/DEFQUAL) at ~2550, and
         * any band with rms < SILENCE_RMS was already zeroed above, so
         * sfac = log10(target/rms)*sfstep <= ~51 and sf_rel >= 49. A counter
         * over 40k bands across castanets, glockenspiel, bass, stereo music at
         * 96k, near-lossless -q 2000 and 16 kbps found zero hits. Kept anyway:
         * it guards an 8-bit bitstream field, and a correctly-predicted compare
         * costs nothing. */
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

static void window_band_energy(const CoderInfo * __restrict ci, const float * __restrict w,
                                int from_sfb, int to_sfb, float * __restrict e_out)
{
    int sfb;
    for (sfb = from_sfb; sfb < to_sfb; sfb++)
    {
        float e = 0.0f;
        int k;
        for (k = ci->sfb_offset[sfb]; k < ci->sfb_offset[sfb + 1]; k++)
            e += w[k] * w[k];
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

        window_band_energy(coderInfo, w, GROUP_MIN_SFB, maxsfb, band_e);

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
