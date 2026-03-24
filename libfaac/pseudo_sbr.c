/*
 * FAAC - Freeware Advanced Audio Coder
 * Pseudo Spectral Band Replication (encoder-side)
 *
 * Copyright (C) 2026  Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <string.h>

#include "pseudo_sbr.h"
#include "coder.h"
#include "filtbank.h"
#include "faac_real.h"
#include "util.h"

/* SBR design constants */

#define MAX_SBR_PATCHES      4
#define MIN_PATCH_BINS       16

/* Increased fill-ratio to allow activation on speech/vss signals (e.g. 16kHz @ 40kbps) */
#define SBR_FILL_RATIO_MAX   0.75f

#ifdef FAAC_PRECISION_SINGLE
#  define SBR_PATCH_ROLLOFF  0.50f
#else
#  define SBR_PATCH_ROLLOFF  0.50
#endif

#define SBR_MIN_EXTENSION_HZ  500u

/* -----------------------------------------------------------------------
 * Private helpers
 * --------------------------------------------------------------------- */

static faac_real band_energy(const faac_real * __restrict mdct, int len)
{
    faac_real e = 0.0;
    int i;
    for (i = 0; i < len; i++)
        e += mdct[i] * mdct[i];
    return e;
}

static float compute_sfm(const faac_real * __restrict buf, int len)
{
    float am = 0.0f, lm = 0.0f;
    int i, n = 0;
    for (i = 0; i < len; i++) {
        float v = (float)(buf[i] * buf[i]);
        am += v;
        if (v > 1e-20f) { lm += logf(v); n++; }
    }
    if (am < 1e-20f || n == 0) return 0.0f;   /* tonal / silence → no noise */
    am /= len;
    lm  = expf(lm / len);                      /* geometric mean over full len */
    float sfm = lm / am;
    /* Clamp to [0,1] — numerical noise can push it slightly over */
    return sfm > 1.0f ? 1.0f : (sfm < 0.0f ? 0.0f : sfm);
}

/* Fill [bw_bin, tgt_bin) with noise-dithered copies of the coded bandwidth. */
static void apply_sbr_patch(faac_real * __restrict mdct,
                            int bw_bin, int tgt_bin,
                            unsigned int bitRate,
                            unsigned int *rand)
{
    int tgt       = bw_bin;
    faac_real cum_gain = (faac_real)1.0;
    unsigned int local_rand = *rand;
    int p;

    for (p = 0; p < MAX_SBR_PATCHES; p++)
    {
        int remaining, src_start, i, patch_len;
        faac_real src_e, tgt_rms_goal, sig_scale;
        float sfm, adaptive_noise_frac;

        /* Point 4: Source patch selection from top octave of coded BW. */
        patch_len = bw_bin >> (p + 1);

        if (patch_len < MIN_PATCH_BINS)
            break;

        remaining = tgt_bin - tgt;
        if (remaining < MIN_PATCH_BINS)
            break;

        if (patch_len > remaining)
            patch_len = remaining;

        src_start = bw_bin - patch_len;

        src_e = band_energy(mdct + src_start, patch_len);

        if (src_e <= (faac_real)1e-20)
        {
            tgt      += patch_len;
            cum_gain *= (faac_real)SBR_PATCH_ROLLOFF;
            continue;
        }

        /* 1. Calculate target goal (RMS) */
        tgt_rms_goal = FAAC_SQRT(src_e / (faac_real)patch_len) * cum_gain;

        /* Conservative safeguard for very low bitrates: reduce SBR energy to protect core bit depth. */
        if (bitRate < 24000u) tgt_rms_goal *= (faac_real)0.5;

        /* 2. Determine noise fraction via SFM (Point 3) */
        sfm = compute_sfm(mdct + src_start, patch_len);
        adaptive_noise_frac = 0.04f + sfm * 0.20f;

        sig_scale = (faac_real)1.0 - (faac_real)adaptive_noise_frac;

        /* 3. Fill patch using signal-proportional noise (Point 6 alternative) for zero-mean and stability. */
        {
            faac_real * __restrict p_tgt = mdct + tgt;
            const faac_real * __restrict p_src = mdct + src_start;

            for (i = 0; i < patch_len; i++) {
                local_rand = local_rand * 1664525u + 1013904223u;
                faac_real noise_sign = (local_rand & 0x80000000u) ? (faac_real)1.0 : (faac_real)-1.0;
                p_tgt[i] = p_src[i] * sig_scale + noise_sign * FAAC_FABS(p_src[i]) * (faac_real)adaptive_noise_frac;
            }
        }

        /* 4. Energy normalization correction to match target goal (Point 2) */
        {
            faac_real written_e = band_energy(mdct + tgt, patch_len);
            if (written_e > (faac_real)1e-20) {
                faac_real correction = tgt_rms_goal / FAAC_SQRT(written_e / (faac_real)patch_len);
                /* Safety clamp for numerical stability and bit protection */
                if (correction > (faac_real)2.0) correction = (faac_real)2.0;
                if (correction < (faac_real)0.5) correction = (faac_real)0.5;
                for (i = 0; i < patch_len; i++)
                    mdct[tgt + i] *= correction;
            }
        }

        tgt      += patch_len;
        cum_gain *= (faac_real)SBR_PATCH_ROLLOFF;
    }
    *rand = local_rand;
}

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

void PseudoSBR(CoderInfo    *coderInfo,
               faac_real    *freqBuff,
               unsigned int  sampleRate,
               unsigned int  baseBW,
               unsigned int  sbrBW,
               unsigned int  bitRate,
               unsigned int *rand)
{
    int bw_bin, tgt_bin;
    faac_real invSampleRate;

    if (baseBW >= sbrBW || sampleRate == 0)
        return;

    if ((sbrBW - baseBW) < SBR_MIN_EXTENSION_HZ)
        return;

    invSampleRate = (faac_real)2.0 / (faac_real)sampleRate;

    if (coderInfo->block_type == ONLY_SHORT_WINDOW)
    {
        int win;

        bw_bin  = (int)((faac_real)baseBW * BLOCK_LEN_SHORT * invSampleRate);
        tgt_bin = (int)((faac_real)sbrBW  * BLOCK_LEN_SHORT * invSampleRate);

        if (tgt_bin > BLOCK_LEN_SHORT) tgt_bin = BLOCK_LEN_SHORT;
        if (bw_bin >= tgt_bin || bw_bin < MIN_PATCH_BINS * 2)
            return;

        for (win = 0; win < MAX_SHORT_WINDOWS; win++)
            apply_sbr_patch(freqBuff + win * BLOCK_LEN_SHORT,
                            bw_bin, tgt_bin, bitRate, rand);
    }
    else
    {
        bw_bin  = (int)((faac_real)baseBW * BLOCK_LEN_LONG * invSampleRate);
        tgt_bin = (int)((faac_real)sbrBW  * BLOCK_LEN_LONG * invSampleRate);

        if (tgt_bin > BLOCK_LEN_LONG) tgt_bin = BLOCK_LEN_LONG;
        if (bw_bin >= tgt_bin || bw_bin < MIN_PATCH_BINS * 2)
            return;

        apply_sbr_patch(freqBuff, bw_bin, tgt_bin, bitRate, rand);
    }
}

/* Calculate SBR target bandwidth based on bitrate and core protection. */
unsigned int PseudoSBRTargetBW(unsigned int sampleRate,
                                unsigned int baseBW,
                                unsigned int bitRate)
{
    float fillRatio;
    unsigned int ext_percent;
    unsigned int extended, nyquist90;

    if (sampleRate == 0 || baseBW == 0)
        return baseBW;

    /* Stage 1: fill-ratio gate. */
    fillRatio = (float)baseBW / (float)(sampleRate / 2);
    if (fillRatio >= SBR_FILL_RATIO_MAX)
        return baseBW;

    /* Stage 2: bitrate-tier extension amount.
       Using conservative percentage tiers for bitrates < 32kbps to protect core bit depth. */
    if (bitRate < 12000u)
        ext_percent = 15u;
    else if (bitRate < 24000u)
        ext_percent = 25u;
    else
        ext_percent = 40u;

    extended  = baseBW + (baseBW * ext_percent / 100u);

    /* Point 5: Target specific absolute output bandwidths for higher bitrates (>= 32kbps). */
    if (bitRate >= 32000u) {
        unsigned int abs_tgt;
        if (bitRate < 80000u) abs_tgt = 14000u;
        else                  abs_tgt = 16000u;

        if (abs_tgt > extended) extended = abs_tgt;
    }

    nyquist90 = sampleRate * 9u / 20u;

    if (extended > nyquist90)
        extended = nyquist90;

    /* Global safety: don't extend more than 50% beyond core BW to prevent excessive spectral starvation. */
    if (extended > (baseBW * 3u / 2u))
        extended = baseBW * 3u / 2u;

    if (extended <= baseBW + SBR_MIN_EXTENSION_HZ)
        return baseBW;

    return extended;
}

/* Returns 1 if SBR is beneficial for the current configuration. */
int PseudoSBRShouldEnable(unsigned int sampleRate, unsigned int naturalBW)
{
    float fillRatio;

    if (sampleRate == 0 || naturalBW == 0)
        return 0;

    fillRatio = (float)naturalBW / (float)(sampleRate / 2);
    return (fillRatio < SBR_FILL_RATIO_MAX) ? 1 : 0;
}
