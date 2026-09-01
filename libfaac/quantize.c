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
#include "stats.h"

typedef int (*QuantizeFunc)(const float * __restrict xr, int * __restrict xi, int n, float sfacfix);

#if defined(HAVE_SSE2)
extern int quantize_sse2(const float * __restrict xr, int * __restrict xi, int n, float sfacfix);
#endif

static int quantize_scalar(const float * __restrict xr, int * __restrict xi, int n, float sfacfix)
{
    const float magic = MAGIC_NUMBER;
    int i, maxq = 0;
    for (i = 0; i < n; i++)
    {
        float val = xr[i];
        float tmp = fabsf(val) * sfacfix;
        tmp = sqrtf(tmp * sqrtf(tmp));
        int q = (int)(tmp + magic);
        if (q > maxq) maxq = q;
        xi[i] = (val < 0) ? -q : q;
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
                                 int gnum, int cutoff, BandEnergy * __restrict out)
{
    int gsize = ci->groups.len[gnum];
    int sfb;

    for (sfb = 0; sfb < ci->sfbn; sfb++)
    {
        int lo = ci->sfb_offset[sfb], hi = ci->sfb_offset[sfb + 1];
        int len;

        /* Short blocks carry a zeroed stopband above the coded cutoff; summing
           and max-ing those lines yields exactly 0.0f either way. */
        if (lo >= cutoff)
        {
            out[sfb].sum = 0.0f;
            out[sfb].peak_amp = 0.0f;
            continue;
        }
        if (hi > cutoff) hi = cutoff;
        len = hi - lo;
        float sum = 0.0f, peak = 0.0f;
        int w;

        for (w = 0; w < gsize; w++)
        {
            const float * __restrict line = xr0 + w * BLOCK_LEN_SHORT + lo;
            int k;
            for (k = 0; k < len; k++)
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
                                   const BandEnergy * __restrict be, int gnum, int pnslevel,
                                   int * __restrict p_last_abs)
{
    int gsize = ci->groups.len[gnum];
    float pns_threshold = 0.1f * (float)pnslevel;
    int sb;

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
        float avg_per_window = bandenrg[sb] / (float)gsize;
        float rms = sqrtf(avg_per_window / width);

        if (rms < SILENCE_RMS || target[sb] == 0.0f)
        {
            ci->book[band] = HCB_ZERO;
            ci->bandcnt++;
            continue;
        }

        /* Log-domain identity: log10(target/rms) = log10(target) - 0.5*log10(avg) + 0.5*log10(width).
         * Reuses sf_enrg_avg (log10(avg) * SF_STEP_ENRG) shared with PNS to avoid division and sqrtf. */
        float sf_enrg_avg = log10f(avg_per_window) * SF_STEP_ENRG;

        /* PNS is fine inside TNS-covered bands -- the decoder's inverse
         * TNS filter shapes the substituted noise too. */
        if (target[sb] < pns_threshold)
        {
            ci->book[band] = HCB_PNS;
#ifdef FAAC_STATS
            g_faacStats.pnsBands++;
#endif
            ci->sf[band] += lrintf(sf_enrg_avg);
            ci->bandcnt++;
            continue;
        }

        float log10_w_sf = (width < 128) ? log10_width_sf_lut[width] : log10f((float)width) * SF_STEP_ENRG;
        int sfac = lrintf(log10f(target[sb]) * sfstep - sf_enrg_avg + log10_w_sf);
        int sf_rel = SF_OFFSET - sfac;
        int sf_bias = ci->sf[band];

        if (sf_rel < SF_MIN)
        {
            ci->book[band] = HCB_ZERO;
        }
        else
        {
            int sf_abs;
            float gain = resolve_band_gain(sfac, sf_bias, be[sb].peak_amp, *p_last_abs, &sf_rel, &sf_abs);
            int xi[FRAME_LEN];
            int win, maxq = 0;

            for (win = 0; win < gsize; win++)
            {
                int qm = qfunc(xr0 + win * BLOCK_LEN_SHORT + lo, xi + win * width, width, gain);
                if (qm > maxq) maxq = qm;
            }
            huffbook(ci, xi, gsize * width, maxq);
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
    int i, lastsf = SF_CHAIN_UNSET;
    float *gxr = xr;
    int cutoff = (coder->block_type == ONLY_SHORT_WINDOW)
               ? aacquantCfg->max_l / 8 : coder->sfb_offset[coder->sfbn];

    coder->bandcnt = coder->datacnt = 0;
    for (i = 0; i < coder->groups.n; i++)
    {
        measure_band_energy(coder, gxr, i, cutoff, be);
        derive_masking_targets(coder, i, (float)aacquantCfg->quality / DEFQUAL, be, target, bandenrg);
        assign_band_codebooks(coder, gxr, target, bandenrg, be, i, aacquantCfg->pnslevel, &lastsf);
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

/* Bounds of one short band, clamped to the coded cutoff. Returns 0 once the
   bands start past it: those coefficients were just zeroed, so their energy is
   zero and BlocGroup already cleared the matrix. */
static inline int band_bounds(const int * __restrict sfb_offset, int sfb, int cutoff,
                               int * __restrict start_k, int * __restrict len)
{
    int lo = sfb_offset[sfb];
    if (lo >= cutoff)
        return 0;

    int hi = sfb_offset[sfb + 1];
    if (hi > cutoff)
        hi = cutoff;

    *start_k = lo;
    *len = hi - lo;
    return 1;
}

/* The quantizer reads every band up to sfb_offset[maxsfb], but only lines below
   the cutoff carry signal. */
static inline void zero_stopband(float * __restrict w, int cutoff, int stop)
{
    int k;
    for (k = cutoff; k < stop; k++)
        w[k] = 0.0f;
}

/* One short window of one channel: per-band sums of squares. */
static void window_band_energy(const int * __restrict sfb_offset, float * __restrict w,
                                int maxsfb, int stop, int cutoff, float * __restrict e)
{
    int sfb;

    zero_stopband(w, cutoff, stop);

    for (sfb = 0; sfb < maxsfb; sfb++)
    {
        int start_k, len, k;
        float sum = 0.0f;

        if (!band_bounds(sfb_offset, sfb, cutoff, &start_k, &len))
            break;

        const float * __restrict line = w + start_k;
        for (k = 0; k < len; k++)
            sum += line[k] * line[k];

        e[sfb] = sum;
    }
}

/* Both channels of a CPE over one window, carrying the L*R cross term along
   for AACstereo. Reading the two spectra together costs one extra multiply-add
   per coefficient and saves AACstereo a second full scan of both of them. */
static void window_band_energy_cpe(const int * __restrict sfb_offset,
                                    float * __restrict wl, float * __restrict wr,
                                    int maxsfb, int stop, int cutoff,
                                    float * __restrict eL, float * __restrict eR,
                                    float * __restrict eLR)
{
    int sfb;

    zero_stopband(wl, cutoff, stop);
    zero_stopband(wr, cutoff, stop);

    for (sfb = 0; sfb < maxsfb; sfb++)
    {
        int start_k, len, k;
        float sumL = 0.0f, sumR = 0.0f, sumLR = 0.0f;

        if (!band_bounds(sfb_offset, sfb, cutoff, &start_k, &len))
            break;

        const float * __restrict lineL = wl + start_k;
        const float * __restrict lineR = wr + start_k;
        for (k = 0; k < len; k++)
        {
            float l = lineL[k], r = lineR[k];
            sumL  += l * l;
            sumR  += r * r;
            sumLR += l * r;
        }

        eL[sfb] = sumL;
        eR[sfb] = sumR;
        eLR[sfb] = sumLR;
    }
}

/* Splits the 8 short windows into groups at detected onsets, and leaves the
   per-(window, band) energies behind for AACstereo. A CPE passes both channels
   (ci_r != NULL): their energies are summed so one grouping is derived for the
   pair and copied to the right channel, which is what common_window requires
   anyway. Callers gate on ONLY_SHORT_WINDOW; long blocks get their single group
   set in faacEncEncode.

   Both channels of a short CPE share one sfb_offset table, so the scan reads it
   from a local and the window loop keeps the onset decision inline: each window
   feeds the running min/max that the next one tests against, and that carried
   dependence is also what keeps the compiler from unrolling eight copies of the
   vectorized band loop. */
void BlocGroup(CoderInfo *coderInfo, float *xr, CoderInfo *ci_r, float *xr_r,
               ShortBandEnergy *energy, AACQuantCfg *cfg)
{
    const int * __restrict sfb_offset = coderInfo->sfb_offset;
    int maxsfb = cfg->max_cbs;
    int cutoff = cfg->max_l / 8;
    const int stop = sfb_offset[maxsfb];
    int active_bands = maxsfb - GROUP_MIN_SFB;
    int onset_quorum = (active_bands * 3) >> 2;

    float run_min[NSFB_SHORT], run_max[NSFB_SHORT];
    int win, sfb, group_start = 0;

    /* Bands at or past the cutoff are never written, so clear once here rather
       than per window: they read back as zero, which is what summing their
       (now zeroed) coefficients would have produced anyway. */
    memset(energy, 0, sizeof(*energy));
    energy->cpe = (ci_r != NULL);

    coderInfo->groups.n = 0;

    for (win = 0; win < MAX_SHORT_WINDOWS; win++)
    {
        float * __restrict el = energy->ll[win];
        float * __restrict er = energy->rr[win];
        int onset_votes = 0;

        if (ci_r)
            window_band_energy_cpe(sfb_offset, xr + win * BLOCK_LEN_SHORT,
                                   xr_r + win * BLOCK_LEN_SHORT, maxsfb, stop, cutoff,
                                   el, er, energy->lr[win]);
        else
            window_band_energy(sfb_offset, xr + win * BLOCK_LEN_SHORT,
                               maxsfb, stop, cutoff, el);

        if (win == group_start)
        {
            for (sfb = GROUP_MIN_SFB; sfb < maxsfb; sfb++)
                run_min[sfb] = run_max[sfb] = el[sfb] + er[sfb];
            continue;
        }

        for (sfb = GROUP_MIN_SFB; sfb < maxsfb; sfb++)
        {
            float v = el[sfb] + er[sfb];
            if (v < run_min[sfb]) run_min[sfb] = v;
            if (v > run_max[sfb]) run_max[sfb] = v;
            if (run_max[sfb] > GROUP_ONSET_RATIO * run_min[sfb]) onset_votes++;
        }

        if (onset_votes > onset_quorum)
        {
            coderInfo->groups.len[coderInfo->groups.n++] = win - group_start;
            group_start = win;
            for (sfb = GROUP_MIN_SFB; sfb < maxsfb; sfb++)
                run_min[sfb] = run_max[sfb] = el[sfb] + er[sfb];
        }
    }
    coderInfo->groups.len[coderInfo->groups.n++] = MAX_SHORT_WINDOWS - group_start;

    if (ci_r)
        ci_r->groups = coderInfo->groups;
}
