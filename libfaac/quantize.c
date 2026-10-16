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
#include "quantize.h"
#include "huff2.h"
#include "cpu_compute.h"
#include "stats.h"

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
#define SILENCE_RMS            0.4f     // time-domain RMS, in 16-bit PCM units
#define AVG_ENERGY_WEIGHT      0.2f     // noise-like (average-energy) share of the target
#define AVG_ENERGY_WEIGHT_TUNED 0.15f   // the same, for tuned streams
#define PEAK_ENERGY_WEIGHT     0.45f    // tonal (peak-energy) share of the remainder
#define LOUDNESS_EXPONENT      0.4f     // Zwicker-ish loudness compression
#define LOUDNESS_EXPONENT_TUNED 0.35f   // the same, for tuned streams
#define AVG_ENERGY_FLOOR_FRAC  0.0010f  // -30 dB floor, keeps quiet bands from collapsing the target
#define SF_SMOOTH              0.6f     // long-block pull of each scalefactor towards its neighbours
#define PEAK_ENERGY_FLOOR_FRAC 0.0050f  // ~-23 dB floor, same purpose for peak energy
#define QUIET_BAND_FRAC        0.0003f  // ~-35 dB below the frame mean: masked by the frame as a whole

typedef struct
{
    float sum;      /* energy summed across group windows */
    float peak_energy; /* mean of the per-window peak energies */
} BandEnergy;

static float silence_energy(const CoderInfo *ci)
{
    int len = (ci->block_type == ONLY_SHORT_WINDOW) ? BLOCK_LEN_SHORT : BLOCK_LEN_LONG;
    /* The unnormalised, doubled MDCT has 2*len times the input mean-square
     * energy for a sine-windowed stationary signal. Keep the silence floor
     * in sample units rather than making it depend on the transform length. */
    return (2.0f * len) * (SILENCE_RMS * SILENCE_RMS);
}

static float measure_band_energy(const CoderInfo * __restrict ci, const float * __restrict xr0,
                                  int gnum, int cutoff, BandEnergy * __restrict out)
{
    int gsize = ci->groups.len[gnum];
    float group_total = 0.0f;
    float silence = silence_energy(ci);
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
        if (ms > 0.0f && sum >= silence * (float)(gsize * len))
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

static float loudness(float energy_ratio, float exponent)
{
    return powf(energy_ratio, exponent);
}

/* Target multiplier, in dB, by frequency in kHz at a 48 kHz rate (position
 * scales with the rate). Negative codes finer, positive coarser. Found by
 * coordinate search on the benchmark's LC stereo ladder against Zimtohrli;
 * the 1.5-3 kHz region gets finer and 11 kHz coarser, which moves noise
 * towards where Apple's encoder puts it. */
#define TILT_KNOTS 8
static const float tilt_khz[TILT_KNOTS] = { 0.5f, 1.5f, 3.0f, 5.0f, 8.0f, 11.0f, 14.0f, 18.0f };
static const float tilt_db[TILT_KNOTS]  = { 0.0f, -1.5f, -1.5f, 0.0f, 0.0f, 3.0f, 0.0f, -3.0f };
#define TUNED_SLOPE_MUL 1.5f

static float tilt_gain(float khz)
{
    float db = tilt_db[TILT_KNOTS - 1];
    int k;
    if (khz <= tilt_khz[0])
        db = tilt_db[0];
    else if (khz < tilt_khz[TILT_KNOTS - 1])
    {
        for (k = 0; khz > tilt_khz[k + 1]; k++) ;
        db = tilt_db[k] + (tilt_db[k + 1] - tilt_db[k]) * (khz - tilt_khz[k]) / (tilt_khz[k + 1] - tilt_khz[k]);
    }
    return powf(10.0f, db / 20.0f);
}

void QuantizeSetMaskTune(AACQuantCfg *cfg, int tuned, const int *sfbOffsetLong,
                         const int *sfbOffsetShort)
{
    int i;
    cfg->avg_weight = tuned ? AVG_ENERGY_WEIGHT_TUNED : AVG_ENERGY_WEIGHT;
    cfg->loud_exponent = tuned ? LOUDNESS_EXPONENT_TUNED : LOUDNESS_EXPONENT;
    for (i = 0; i < NSFB_LONG; i++)
        cfg->tilt_long[i] = 1.0f;
    for (i = 0; i < NSFB_SHORT; i++)
        cfg->tilt_short[i] = 1.0f;
    if (!tuned)
        return;
    cfg->treble_slope *= TUNED_SLOPE_MUL;
    /* band centre as a share of Nyquist, read at 48 kHz (24 kHz wide) */
    for (i = 0; i < cfg->max_cbl; i++)
        cfg->tilt_long[i] = tilt_gain(0.5f * (float)(sfbOffsetLong[i] + sfbOffsetLong[i + 1])
                                      * (24.0f / (float)BLOCK_LEN_LONG));
    for (i = 0; i < cfg->max_cbs; i++)
        cfg->tilt_short[i] = tilt_gain(0.5f * (float)(sfbOffsetShort[i] + sfbOffsetShort[i + 1])
                                       * (24.0f / (float)BLOCK_LEN_SHORT));
}

// masking sensitivity drops above ~4 kHz; de-emphasize bands toward Nyquist
static float treble_rolloff(int lo, int hi, float slope_per_line)
{
    return 10.0f / (1.0f + (float)(lo + hi) * slope_per_line);
}

static void derive_masking_targets(CoderInfo * __restrict ci, int gnum, float quality,
                                    const AACQuantCfg * __restrict cfg, const BandEnergy * __restrict be, float group_total,
                                    float * __restrict target_out)
{
    const float treble_slope = cfg->treble_slope;
    const float avg_weight = cfg->avg_weight;
    const float loud_exp = cfg->loud_exponent;
    const float *tilt = (ci->block_type == ONLY_SHORT_WINDOW) ? cfg->tilt_short : cfg->tilt_long;
    int gsize = ci->groups.len[gnum];
    int total_len = ci->sfb_offset[ci->sfbn];
    int sfb;

    // whole group below the silence gate: force every band to a zero target
    if (group_total < silence_energy(ci) * (float)(gsize * total_len))
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

        target = avg_weight * loudness(avg / ref, loud_exp)
               + (1.0f - avg_weight) * PEAK_ENERGY_WEIGHT * loudness(peak / ref_win, loud_exp);
        target *= treble_rolloff(lo, hi, slope_per_line) * tilt[sfb];
        /* A band this far under the frame is masked by it; the floors above
         * would otherwise pin its target at the floor. Falls at half the
         * loudness exponent. */
        if (be[sfb].sum < ref * QUIET_BAND_FRAC)
            target *= sqrtf(loudness(be[sfb].sum / (ref * QUIET_BAND_FRAC), loud_exp));

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

/* The scalefactor a coded band wants. Long blocks pull it most of the way
 * towards the mean of its coded neighbours: the masking target tracks local
 * band energy too steeply, which quantizes peaks too coarsely and the bands
 * beside them too finely. */
static int smoothed_sfac(const int *want, int sb, int n, int sf_bias, int smooth)
{
    int sfac = sf_bias - want[sb];
    if (smooth)
    {
        int cnt = 0, sum = 0;
        if (sb > 0 && want[sb - 1] != INT_MIN) { sum += want[sb - 1]; cnt++; }
        if (sb + 1 < n && want[sb + 1] != INT_MIN) { sum += want[sb + 1]; cnt++; }
        if (cnt)
            sfac += lrintf(SF_SMOOTH * ((float)want[sb] - (float)sum / (float)cnt));
        /* smoothing must not turn a coded band into a zero band */
        if (SF_OFFSET - sfac < SF_MIN)
            sfac = SF_OFFSET - SF_MIN;
    }
    return sfac;
}

/* ---- experiment: training-data logger (env SFLOG=path), not for merge ---- */
int g_sflog_rate;
static FILE *g_sflog;
static int g_sflog_init;
#define SFL_NF 48
static void sflog_row(const float *f)
{
    if (!g_sflog_init)
    {
        const char *e = getenv("SFLOG");
        g_sflog_init = 1;
        if (e) g_sflog = fopen(e, "wb");
    }
    if (g_sflog)
        fwrite(f, sizeof(float), SFL_NF, g_sflog);
}

static int g_lo = -2, g_hi = 2, g_env;
static double g_chain = 1.0, g_wd = 1.0, g_wb = 1.0, g_pen = 0.0;
static void sfenv(void)
{
    const char *e;
    g_env = 1;
    if ((e = getenv("SF_LO"))) g_lo = atoi(e);
    if ((e = getenv("SF_HI"))) g_hi = atoi(e);
    if ((e = getenv("SF_CHAIN"))) g_chain = atof(e);
    if ((e = getenv("SF_WD"))) g_wd = atof(e);
    if ((e = getenv("SF_WB"))) g_wb = atof(e);
    if ((e = getenv("SF_PEN"))) g_pen = atof(e);
}
#define FFMAX_F(a, b) ((a) > (b) ? (a) : (b))
#define SFV_RANGE 2
#ifndef SFV_EPS
#define SFV_EPS 1e-6
#endif
#ifndef SFV_OFF
#ifndef SFV_LAMBDA
#define SFV_LAMBDA 0.08f
#endif
#endif

static void assign_band_codebooks(CoderInfo * __restrict ci, const float * __restrict xr0,
                                   const float * __restrict target,
                                   const BandEnergy * __restrict be, int gnum, int pnslevel,
                                   int * __restrict p_last_abs, int * __restrict qs, int * __restrict p_qlen, int sf_search)
{
    int gsize = ci->groups.len[gnum];
    float silence = silence_energy(ci);
    float pns_threshold = 0.1f * (float)pnslevel;
    int smooth = ci->block_type != ONLY_SHORT_WINDOW;
    int base = ci->bandcnt;
    int n = ci->sfbn;
    /* per band: sf bias minus the wanted scalefactor, or INT_MIN if not coded */
    int want[MAX_SCFAC_BANDS];
    int sb;

    if (n > MAX_SCFAC_BANDS - base)
        n = MAX_SCFAC_BANDS - base;

    /* Pass 1: settle zero and PNS bands, and the scalefactor each coded band wants. */
    for (sb = 0; sb < n; sb++)
    {
        int band = base + sb;

#ifdef FAAC_STATS
        g_faacStats.totalBands++;
#endif

        want[sb] = INT_MIN;
        if (ci->book[band] != HCB_NONE)
            continue;

        int width = ci->sfb_offset[sb + 1] - ci->sfb_offset[sb];
        float avg_per_window = be[sb].sum / (float)gsize;
        if (avg_per_window < silence * width || target[sb] == 0.0f)
        {
            ci->book[band] = HCB_ZERO;
            continue;
        }

        /* Log-domain identity: log10(target/rms) = log10(target) - 0.5*log10(avg) + 0.5*log10(width).
         * Reuses sf_enrg_avg (log10(avg) * SF_STEP_ENRG) shared with PNS to avoid division and sqrtf. */
        float sf_enrg_avg = log10f(avg_per_window) * SF_STEP_ENRG;

        /* PNS is fine inside TNS-covered bands -- the decoder's inverse
         * TNS filter shapes the substituted noise too. Decoders skip M/S on a
         * noise band, so an M/S band that wants noise goes back to L/R noise
         * in both channels, decided on the mid. Its flag isn't restored on a
         * retry, so the fallback sticks. A side band left under M/S drops to
         * zero instead, leaving the band mono. */
        if (target[sb] < pns_threshold || (ci->msEl[band] > 0.0f && ci->msUsed && !ci->msUsed[band]))
        {
            if (ci->msEl[band] > 0.0f)
            {
                CoderInfo *r = ci->partner;
                if (!r)
                {
                    ci->book[band] = HCB_ZERO;
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
            ci->sf[band] += lrintf(sf_enrg_avg);
            continue;
        }

        float log10_w_sf = (width < 128) ? log10_width_sf_lut[width] : log10f((float)width) * SF_STEP_ENRG;
        int sfac = lrintf(log10f(target[sb]) * sfstep - sf_enrg_avg + log10_w_sf);
        if (SF_OFFSET - sfac < SF_MIN)
        {
            ci->book[band] = HCB_ZERO;
            ci->sf[band] += SF_OFFSET - sfac;
            continue;
        }
        want[sb] = ci->sf[band] - sfac;
    }

#ifdef SFV_LAMBDA
    /* Experiment: pick every coded band's scalefactor jointly. Each band
     * offers a few candidates around the wanted one; a Viterbi pass over the
     * group minimizes distortion against the band's allowed noise plus lambda
     * times the real Huffman bits, including the scalefactor-delta bits. */
    int sel[MAX_SCFAC_BANDS];
    for (sb = 0; sb < n; sb++)
        sel[sb] = INT_MIN;
    if (sf_search)
    {
        enum { NC = 2 * SFV_RANGE + 1 };
        /* Doubles throughout: a fused multiply-add on one target and not on
         * another moves a float cost by one part in 1e7, enough to flip a
         * near tie and make the bitstream differ between platforms. */
        double cost[NC], ncost[NC];
        double lam = SFV_LAMBDA;
        if (!g_env) sfenv();
        int absf[MAX_SCFAC_BANDS][NC], sfc[MAX_SCFAC_BANDS][NC], bp[MAX_SCFAC_BANDS][NC];
        double dp[MAX_SCFAC_BANDS][NC];
        float lf[MAX_SCFAC_BANDS][SFL_NF];
        int lsfac[MAX_SCFAC_BANDS];
        int order[MAX_SCFAC_BANDS], nb = 0, b, c, pc, pi;
        /* q^(4/3) for small q */
        static const double pow43[64] = {
            0.0, 1.0, 2.5198420997897464, 4.3267487109222245, 6.3496042078727974, 8.549879733383484, 10.902723556992836, 13.390518279406722, 15.999999999999998, 18.720754407467133, 21.544346900318832, 24.463780996262468, 27.47314182127996, 30.567350940369842, 33.74199169845321, 36.993181114957046, 40.317473596635935, 43.71178704118999, 47.173345095760126, 50.69963132571694, 54.28835233189812, 57.93740770400352, 61.6448652744185, 65.40894053658599, 69.22797937475559, 73.10044345532164, 77.02489777859161, 80.99999999999999, 85.02449121251853, 89.09718794488955, 93.21697517861574, 97.38280022413316, 101.59366732596474, 105.84863288986224, 110.14680124343441, 114.4873208566006, 118.86938096020653, 123.29220851090024, 127.75506545836058, 132.25724627755247, 136.79807573413572, 141.3769068556919, 145.99311908523086, 150.6461165966291, 155.33532675434674, 160.0601987020528, 164.8202020667335, 169.6148257665186, 174.44357691188534, 179.30597979112557, 184.20157493201927, 189.12991823257562, 194.09058015449685, 199.08314497371677, 204.1072100829694, 209.16238534187647, 214.24829247050752, 219.36456448277784, 224.51084515641213, 229.6867885365223, 234.89205847013176, 240.1263281692325, 245.38927980018505, 250.6806040974726
        };
        /* candidates by distance from the formula's value */
        static const signed char scan[NC] = { 2, 1, 3, 0, 4 };

        for (sb = 0; sb < n; sb++)
        {
            int band = base + sb;
            sel[sb] = INT_MIN;
            if (want[sb] == INT_MIN)
                continue;
            int sf_bias = ci->sf[band];
            int sfac = smoothed_sfac(want, sb, n, sf_bias, smooth);
            sel[sb] = sfac;

            int lo = ci->sfb_offset[sb];
            int width = ci->sfb_offset[sb + 1] - lo;
            double allowed = (double)target[sb] * target[sb] * be[sb].sum / (double)(gsize * width);
            float peak = sqrtf(be[sb].peak_energy);
            int tmp[2 * FRAME_LEN / 8 + 1024];
            /* energy of the band: what a candidate leaves as error when it
             * quantizes a line to zero */
            double a2 = 0.0;
            for (int w = 0; w < gsize; w++)
                for (int k = 0; k < width; k++)
                {
                    double a = xr0[w * BLOCK_LEN_SHORT + lo + k];
                    a2 += a * a;
                }
            lsfac[nb] = sfac;
            memset(lf[nb], 0, sizeof(lf[nb]));
            {
                float *f = lf[nb];
                f[0] = ci->block_type; f[1] = sb; f[2] = width; f[3] = gsize; f[4] = g_sflog_rate;
                f[5] = n; f[6] = (float)(log10((double)target[sb]) * 20.0);
                f[7] = (float)(log10(be[sb].sum / (double)(gsize * width) + 1e-9) * 10.0);
                f[8] = (float)(log10(be[sb].peak_energy + 1e-9) * 10.0);
                f[9] = sfac; f[10] = want[sb] - 0; f[11] = sf_bias;
                f[12] = (sb > 0 && want[sb - 1] != INT_MIN) ? want[sb] - want[sb - 1] : 99;
                f[13] = (sb + 1 < n && want[sb + 1] != INT_MIN) ? want[sb] - want[sb + 1] : 99;
                f[14] = (float)(log10(allowed + 1e-12) * 10.0);
                f[15] = (float)(log10(a2 + 1e-12) * 10.0);
                f[16] = (float)*p_last_abs;
            }
            for (c = 0; c < NC; c++)
            {
                int sc = sfac + (c - SFV_RANGE), win, k;
                double d = a2;
                if (SF_OFFSET - sc < SF_MIN || (c - SFV_RANGE) < g_lo || (c - SFV_RANGE) > g_hi)
                {
                    dp[nb][c] = INFINITY;
                    lf[nb][35 + c] = -1;
                    absf[nb][c] = 0;
                    sfc[nb][c] = sc;
                    continue;
                }
                float gain = gain_with_overflow_clamp(&sc, peak);
                for (win = 0; win < gsize; win++)
                    qfunc(xr0 + win * BLOCK_LEN_SHORT + lo, tmp + win * width, width >> 2, gain);
                for (win = 0; win < gsize; win++)
                    for (k = 0; k < width; k++)
                    {
                        int q = abs(tmp[win * width + k]);
                        if (q)
                        {
                            double a = fabsf(xr0[win * BLOCK_LEN_SHORT + lo + k]);
                            double e = a - (q < 64 ? pow43[q] : (double)q * cbrt((double)q)) / (double)gain;
                            d += e * e - a * a;
                        }
                    }
                int bb = band_spectral_bits(tmp, gsize * width);
                {
                    int qmx = 0, nz = 0, cmx = 0;
                    for (k = 0; k < gsize * width; k++)
                    {
                        int q = abs(tmp[k]);
                        if (q > qmx) { qmx = q; cmx = 0; }
                        if (q == qmx) cmx++;
                        if (q) nz++;
                    }
                    lf[nb][20 + c] = qmx; lf[nb][25 + c] = nz; lf[nb][30 + c] = cmx;
                    lf[nb][35 + c] = (float)bb;
                    lf[nb][40 + c] = (float)(d / FFMAX_F(allowed, 1e-12));
                }
                cost[c] = g_wd * d / FFMAX_F(allowed, 1e-12) + lam * (g_wb * (double)bb + g_pen * abs(c - SFV_RANGE));
                sfc[nb][c] = sc;
                absf[nb][c] = sf_bias + (SF_OFFSET - sc);
                dp[nb][c] = cost[c];
            }
            order[nb++] = sb;
        }
        /* Viterbi; the chain starts from the previous group's last band */
        for (b = 0; b < nb; b++)
        {
            for (c = 0; c < NC; c++)
            {
                double best = INFINITY;
                int arg = 0;
                if (!isfinite(dp[b][c])) { bp[b][c] = 0; ncost[c] = INFINITY; continue; }
                if (b == 0)
                {
                    best = (*p_last_abs == SF_CHAIN_UNSET) ? 0.0f
                         : (abs(absf[b][c] - *p_last_abs) > SF_DELTA ? INFINITY
                            : g_chain * lam * (double)sf_delta_bits(absf[b][c] - *p_last_abs));
                }
                else
                {
                    /* Costs within SFV_EPS of each other are ties and go to the
                     * candidate nearest the formula's value: the float spectrum
                     * differs in its last bits between platforms, and a tie
                     * broken by that noise would change the bitstream. */
                    for (pi = 0; pi < NC; pi++)
                    {
                        pc = scan[pi];
                        int dd = absf[b][c] - absf[b - 1][pc];
                        double v;
                        if (!isfinite(dp[b - 1][pc]) || abs(dd) > SF_DELTA)
                            continue;
                        v = dp[b - 1][pc] + g_chain * lam * (double)sf_delta_bits(dd);
                        if (!isfinite(best) || v < best - SFV_EPS * (1.0 + fabs(best))) { best = v; arg = pc; }
                    }
                }
                bp[b][c] = arg;
                ncost[c] = best + dp[b][c];
            }
            for (c = 0; c < NC; c++)
                dp[b][c] = ncost[c];
        }
        if (nb)
        {
            double best = INFINITY;
            int arg = SFV_RANGE;
            for (pi = 0; pi < NC; pi++)
            {
                c = scan[pi];
                if (!isfinite(best) || dp[nb - 1][c] < best - SFV_EPS * (1.0 + fabs(best))) { best = dp[nb - 1][c]; arg = c; }
            }
            if (isfinite(best))
                for (b = nb - 1; b >= 0; b--)
                {
                    sel[order[b]] = sfc[b][arg];
                    lf[b][17] = arg - SFV_RANGE;
                    arg = bp[b][arg];
                }
            if (isfinite(best))
                for (b = 0; b < nb; b++)
                {
                    lf[b][18] = b > 0 ? lf[b - 1][17] : 99;
                    sflog_row(lf[b]);
                }
        }
    }
#endif

    /* Pass 2: quantize the coded bands. Long blocks first pull each
     * scalefactor most of the way towards the mean of its coded neighbours:
     * the masking target tracks local band energy too steeply, which
     * quantizes peaks too coarsely and the bands beside them too finely. */
    for (sb = 0; sb < n; sb++)
    {
        int band = base + sb;

        if (want[sb] == INT_MIN)
            continue;

        int sf_bias = ci->sf[band];
        int sfac = smoothed_sfac(want, sb, n, sf_bias, smooth);
#ifdef SFV_LAMBDA
        if (sel[sb] != INT_MIN)
            sfac = sel[sb];
#endif

        int lo = ci->sfb_offset[sb];
        int width = ci->sfb_offset[sb + 1] - lo;
        int sf_rel, sf_abs;
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
        ci->sf[band] += sf_rel;
    }
    ci->bandcnt = base + n;
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

        derive_masking_targets(coder, i, (float)aacquantCfg->quality / DEFQUAL, aacquantCfg, be, group_total, target);
        assign_band_codebooks(coder, gxr, target, be, i, aacquantCfg->pnslevel, &lastsf, qs, &qlen, aacquantCfg->sf_search);
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

            window_band_energy(ci[c], w, GROUP_MIN_SFB, maxsfb, band_e);
        }

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

    if (ci_r)
        ci_r->groups = coderInfo->groups;
}
