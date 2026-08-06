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

/* 2^(k/4), correctly rounded. Indexed by a scalefactor's remainder mod 4. */
static const float pow2_quarter[4] = {
    1.0f, 1.18920708f, 1.41421354f, 1.68179286f
};

/* SF_STEP_AMPL is 4/log10(2), so 10^(sfac/sfstep) is exactly 2^(sfac/4). With
 * sfac an integer that is a table lookup and an exponent adjust, not a powf:
 * the fractional quarter comes from the table and the integer part from ldexpf.
 *
 * The mask-and-divide is deliberate. sfac is routinely negative, and >> on a
 * negative signed int is implementation-defined, so `sfac >> 2` would be a
 * portability trap on the embedded targets rather than a shortcut. Every
 * compiler folds this back to a shift for the two's-complement case anyway. */
static float pow10_over_sfstep(int sfac)
{
    unsigned r = (unsigned)sfac & 3u;

    return ldexpf(pow2_quarter[r], (sfac - (int)r) / 4);
}

/* sfac and gain are coupled; clamping one forces a recompute of the other. */
static float gain_with_overflow_clamp(int *sfac, float band_peak)
{
    float gain = pow10_over_sfstep(*sfac);
    if (band_peak > 0.0f && gain * band_peak > max_quant_limit)
    {
        gain = max_quant_limit / band_peak;
        /* log10(gain) * sfstep is log2(gain) * 4, by the same identity. */
        *sfac = (int)floorf(log2f(gain) * 4.0f);
        gain = pow10_over_sfstep(*sfac);
    }
    return gain;
}

// masking target per scalefactor band: 0 marks a band inaudible
#define SILENCE_RMS            0.4f     // per-sample RMS gate for silence
#define AVG_ENERGY_WEIGHT      0.2f     // noise-like (average-energy) share of the target
#define PEAK_ENERGY_WEIGHT     0.45f    // tonal (peak-energy) share of the remainder
/* Short blocks get a tighter target per unit of energy -- but how much they
 * should is not a constant: it changes sign with bitrate. Screened at 0.8
 * against 0.45 (ViSQOL, n=10 music, paired, kbps/channel):
 *
 *    32k  -0.0098  2W/5L      96k  +0.0054  5W/3L
 *    48k  -0.0143  3W/4L     128k  +0.0092  7W/0L
 *    64k  -0.0064  3W/4L     192k  +0.0074  3W/1L
 *
 * Where bits are scarce, spending them evenly across a transient's sub-blocks
 * pays; where they are not, tightening over-serves short blocks at the expense
 * of everything else. The crossover sits between 32k and 48k per channel, so
 * interpolate between the measured anchors either side of it and clamp
 * outside. 0.45 at or below 32k/ch keeps every low-rate encode bit-identical
 * to master, and it holds at 0.8 above 64k/ch because nothing looser has been
 * measured there.
 *
 * Resolved once from the configured bitrate, never from aacquantCfg.quality:
 * the rate controller rewrites quality every frame to hit the target, so
 * deriving tightening from it would close a feedback loop -- tighter targets
 * spend fewer bits, the controller raises quality, which tightens further. */
#define SHORT_TIGHTEN_LO       0.45f   // at or below SHORT_TIGHTEN_LO_RATE
#define SHORT_TIGHTEN_HI       0.80f   // at or above SHORT_TIGHTEN_HI_RATE
#define SHORT_TIGHTEN_LO_RATE  32000.0f  /* per channel */
#define SHORT_TIGHTEN_HI_RATE  64000.0f   /* per channel */
#define SHORT_TIGHTEN_MIN_SR   32000UL    /* anchors were measured at 48 kHz */

float ShortBlockTighten(unsigned long bitRatePerChannel, unsigned long sampleRate)
{
    float r = (float)bitRatePerChannel;

    /* Every anchor above was measured at 48 kHz. At 16 kHz a long window spans
     * 128 ms rather than 43, and the content that goes there is speech, which
     * is short-block-dominated in a way music is not. Loosening short blocks
     * makes them cheaper, and on chop-heavy speech the rate controller then
     * fails to spend the budget at all: on 16k_mono_40k the three worst clips
     * came in at 28.0k, 27.6k and 29.2k against a 40k target -- 70% of the
     * budget -- for -0.27, -0.13 and -0.12 MOS. The scenario mean stayed
     * positive (+0.004, 157 wins to 113), so this is not a broken lever, just
     * one whose anchors do not transfer. PSY_TD_THRESH is floored below
     * 34.3 kHz for the same reason. */
    if (sampleRate < SHORT_TIGHTEN_MIN_SR)
        return SHORT_TIGHTEN_LO;

    /* VBR has no configured rate to key off, and the effect has only been
     * measured in ABR, so it stays at the low anchor there. */
    if (r <= SHORT_TIGHTEN_LO_RATE)
        return SHORT_TIGHTEN_LO;
    if (r >= SHORT_TIGHTEN_HI_RATE)
        return SHORT_TIGHTEN_HI;
    return SHORT_TIGHTEN_LO + (SHORT_TIGHTEN_HI - SHORT_TIGHTEN_LO)
           * (r - SHORT_TIGHTEN_LO_RATE)
           / (SHORT_TIGHTEN_HI_RATE - SHORT_TIGHTEN_LO_RATE);
}
/* START and STOP are the frames either side of a transient: a long transform
 * straddling an attack, which smears quantisation noise back across the quiet
 * run-up. They want a tighter target than a steady long block, and a test for
 * short blocks alone will not give them one.
 *
 * Applied only at or above 48 kbps/channel, which is where it measures. Per
 * scenario on the full CI corpus (ViSQOL, mean with 95% confidence interval):
 *
 *   48k_stereo_96k   +0.011  [+0.008, +0.015]  32W/3L
 *   48k_stereo_128k  +0.015  [+0.010, +0.019]  31W/1L
 *   48k_stereo_160k  +0.012  [+0.008, +0.016]  30W/1L
 *   48k_stereo_192k  +0.007  [+0.004, +0.010]  23W/5L
 *
 * Below that it buys nothing and costs stability. Every stereo scenario from
 * 24k to 64k sat within +-0.004 of zero, and 16 kHz speech churned hard in
 * both directions -- 24 clips past -0.10 against 24 past +0.10, for a scenario
 * mean of -0.002 on 90 wins and 110 losses. That is metric noise at a rate
 * where ViSQOL is unstable rather than a quality change, but it fails a
 * per-clip gate either way. PSY_TD_THRESH is floored below 34.3 kHz for the
 * same reason.
 *
 * Resolved from the configured rate, not from aacquantCfg.quality, which the
 * ABR loop rewrites every frame. */
#define START_STOP_TIGHTEN     0.3f
#define START_STOP_MIN_RATE    48000.0f  /* per channel */

float StartStopTighten(unsigned long bitRatePerChannel)
{
    return (float)bitRatePerChannel >= START_STOP_MIN_RATE
           ? START_STOP_TIGHTEN : 1.0f;
}
/* Zwicker-ish loudness compression. This is the shape parameter: it alone
 * decides how much of the allocation follows band energy. At 0.4 the result is
 * close to white (gain ~ E^-0.1, noise ~ E^0.2), which looks flat for a
 * perceptual coder and reads like an oversight.
 *
 * It measures as an optimum, not an oversight. At -b 64 (ViSQOL, n=10
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
 * That reach is not headroom. Screened at -b 64 (ViSQOL, n=10 music,
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

static float loudness(float energy_ratio)
{
    return powf(energy_ratio, LOUDNESS_EXPONENT);
}


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
    return PSY_GLOBAL_SCALE / (1.0f + (float)(lo + hi) * inv_block_len);
}

static void derive_masking_targets(CoderInfo * __restrict ci, int gnum, float quality,
                                    float start_stop_tighten, float short_tighten,
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

    /* block_type is invariant across the band loop, so the choice sits outside
     * it. Not worth treating as an optimisation: ci is __restrict, so the
     * compare hoists either way, and two powf() per band dominate anything
     * saved here. */
    float tighten;
    if (ci->block_type == ONLY_SHORT_WINDOW)
        tighten = short_tighten;
    else if (ci->block_type == LONG_SHORT_WINDOW || ci->block_type == SHORT_LONG_WINDOW)
        tighten = start_stop_tighten;
    else
        tighten = 1.0f;

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
        target *= tighten;
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
    /* Deliberately block-type blind. PNS spreads synthetic noise across the
     * whole window, so on a START or STOP window -- a long transform
     * straddling a transient -- it covers the quiet run-up as well, which
     * looks like a second route to pre-echo. Measured, that reasoning is
     * wrong: suppressing PNS on those windows costs -0.0106 zimtohrli (0 of 5
     * clips improved) at -b 64, and -0.0627 when combined with START/STOP
     * target tightening. The bits PNS saves there are worth more than the
     * smearing costs. */
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
        derive_masking_targets(coder, i, (float)aacquantCfg->quality / DEFQUAL,
                               aacquantCfg->start_stop_tighten,
                               aacquantCfg->short_tighten, be, target, bandenrg);
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
