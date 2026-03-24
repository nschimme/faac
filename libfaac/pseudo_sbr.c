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
#include <stdint.h>
#include <float.h>

#include "pseudo_sbr.h"
#include "coder.h"
#include "filtbank.h"
#include "faac_real.h"
#include "util.h"

/* SBR design constants */

#define MAX_SBR_PATCHES      4
#define MIN_PATCH_BINS       16

/* Maximum ratio of coded bandwidth to Nyquist frequency before SBR is disabled.
 * Beyond 75%, natural extension by the LC encoder is preferred. */
#define SBR_FILL_RATIO_MAX   0.75f

/* Patch gain roll-off per subsequent patch (linear scale).
 * 0.50 is -6dB, ensuring high frequencies roll off naturally. */
#define SBR_PATCH_ROLLOFF  0.50f

/* Minimum extension required (Hz) to justify SBR overhead. */
#define SBR_MIN_EXTENSION_HZ  250u

/* Adaptive noise parameters.
 * noise = offset + sfm * slope.
 * Tuned via MOS optimization suite. */
#define SBR_NOISE_OFFSET     0.06f
#define SBR_NOISE_SLOPE      0.15f

/* Linear Congruential Generator (LCG) parameters for noise generation.
 * Using constants from Numerical Recipes for 32-bit random numbers. */
#define LCG_MULTIPLIER       1664525u
#define LCG_INCREMENT        1013904223u
#define LCG_SHIFT            1u

/* SBR Target Ratios.
 * Targets and caps derived from natural bandwidth to scale with MOS-optimized base. */
#define SBR_EXTENSION_RATIO   1.25f
#define SBR_GROWTH_CAP_RATIO  0.35f


/* -----------------------------------------------------------------------
 * Private helpers
 * --------------------------------------------------------------------- */

#ifdef FAAC_PRECISION_SINGLE
#define NOISE_SCALE (faac_real)(1.0 / 2147483648.0f)
#else
#define NOISE_SCALE (faac_real)(1.0 / 2147483648.0)
#endif

static faac_real compute_sfm(const faac_real * __restrict mdct, int len)
{
    faac_real am = 0.0, gm = 0.0;
    int i;

    for (i = 0; i < len; i++) {
        faac_real e = mdct[i] * mdct[i];
        if (e <= 0.0f) return 0.0f; /* Any zero bin makes the geometric mean zero */
        am += e;
        gm += logf(e);
    }

    am /= len;
    gm = expf(gm / len);

    return (am > 0.0f) ? (gm / am) : 0.0f;
}

static faac_real band_energy(const faac_real * __restrict mdct, int len)
{
    faac_real e = 0.0;
    int i;
    for (i = 0; i < len; i++)
        e += mdct[i] * mdct[i];
    return e;
}

/* Fill [bw_bin, tgt_bin) with noise-dithered copies of the coded bandwidth. */
static void apply_sbr_patch(faac_real * __restrict mdct,
                            int bw_bin, int tgt_bin,
                            unsigned int *rand)
{
    int tgt = bw_bin;
    faac_real cum_gain = (faac_real)1.0;
    unsigned int local_rand = *rand;
    int p;

    for (p = 0; p < MAX_SBR_PATCHES; p++)
    {
        int remaining, patch_len, src_start, i;
        faac_real src_e, scale, sig_scale, noise_scale;

        remaining = tgt_bin - tgt;
        if (remaining < MIN_PATCH_BINS)
            break;

        /* Max usable patch is whatever fits in the target region, but can't exceed source */
        patch_len = min(bw_bin, remaining);

        src_start = bw_bin - patch_len;

        src_e = band_energy(mdct + src_start, patch_len);

        if (src_e > 0.0f)
        {
            faac_real sfm = compute_sfm(mdct + src_start, patch_len);
            /* Adaptive noise: less noise for tonal bands, more for noise-like regions. */
            faac_real adaptive_noise_frac = SBR_NOISE_OFFSET + sfm * SBR_NOISE_SLOPE;
            scale = cum_gain;
            sig_scale   = scale * ((faac_real)1.0 - adaptive_noise_frac);
            noise_scale = scale * adaptive_noise_frac * NOISE_SCALE;
        }
        else
        {
            tgt      += patch_len;
            cum_gain *= (faac_real)SBR_PATCH_ROLLOFF;
            continue;
        }

        {
            faac_real * __restrict p_tgt = mdct + tgt;
            const faac_real * __restrict p_src = mdct + src_start;
            faac_real written_e, correction;

            for (i = 0; i < patch_len; i++) {
                local_rand = local_rand * LCG_MULTIPLIER + LCG_INCREMENT;
                p_tgt[i] = p_src[i] * sig_scale + (int32_t)(local_rand >> LCG_SHIFT) * noise_scale;
            }

            /* Normalize extended patch energy to match target fraction of source energy.
             * Clamped correction factor [0.1, 1.5] prevents extreme gain fluctuations. */
            written_e = band_energy(p_tgt, patch_len);
            if (written_e > 0.0f)
            {
                correction = sqrtf((src_e * scale * scale) / written_e);
                if (correction > 1.5f) correction = 1.5f;
                if (correction < 0.1f) correction = 0.1f;
                for (i = 0; i < patch_len; i++)
                    p_tgt[i] *= correction;
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
                            bw_bin, tgt_bin, rand);
    }
    else
    {
        bw_bin  = (int)((faac_real)baseBW * BLOCK_LEN_LONG * invSampleRate);
        tgt_bin = (int)((faac_real)sbrBW  * BLOCK_LEN_LONG * invSampleRate);

        if (tgt_bin > BLOCK_LEN_LONG) tgt_bin = BLOCK_LEN_LONG;
        if (bw_bin >= tgt_bin || bw_bin < MIN_PATCH_BINS * 2)
            return;

        apply_sbr_patch(freqBuff, bw_bin, tgt_bin, rand);
    }
}

/* Calculate SBR target bandwidth based on natural bandwidth. */
unsigned int PseudoSBRTargetBW(unsigned int sampleRate,
                                unsigned int baseBW,
                                unsigned int bitRate)
{
    unsigned int extended, nyquist90;
    unsigned int targetBW, growth_cap;

    if (sampleRate == 0 || baseBW == 0)
        return baseBW;

    targetBW   = (unsigned int)((float)baseBW * SBR_EXTENSION_RATIO);
    growth_cap = (unsigned int)((float)baseBW * SBR_GROWTH_CAP_RATIO);

    extended = targetBW;
    if (extended > baseBW + growth_cap)
        extended = baseBW + growth_cap;

    nyquist90 = sampleRate * 9u / 20u;
    if (extended > nyquist90)
        extended = nyquist90;

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
