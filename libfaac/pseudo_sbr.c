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

#define SBR_FILL_RATIO_MAX   0.65f

#ifdef FAAC_PRECISION_SINGLE
#  define SBR_PATCH_ROLLOFF  0.354f
#  define SBR_NOISE_FRAC     0.12f
#else
#  define SBR_PATCH_ROLLOFF  0.354
#  define SBR_NOISE_FRAC     0.12
#endif

#define SBR_MIN_EXTENSION_HZ  500u

/* -----------------------------------------------------------------------
 * Private helpers
 * --------------------------------------------------------------------- */

#ifdef FAAC_PRECISION_SINGLE
#define NOISE_SCALE (faac_real)(1.0 / 2147483648.0f)
#else
#define NOISE_SCALE (faac_real)(1.0 / 2147483648.0)
#endif

static faac_real band_energy(const faac_real * __restrict mdct, int len)
{
    faac_real e = 0.0;
    int i;
    for (i = 0; i < len; i++)
        e += mdct[i] * mdct[i];
    return e;
}

/* Fill [bw_bin, tgt_bin) with noise-dithered copies of the coded bandwidth. */
static void apply_sbr_window(faac_real * __restrict mdct,
                             int bw_bin, int tgt_bin,
                             unsigned int *rand)
{
    int tgt       = bw_bin;
    int patch_len = bw_bin / 2;
    faac_real cum_gain = (faac_real)SBR_PATCH_ROLLOFF;
    unsigned int local_rand = *rand;
    int p;

    for (p = 0; p < MAX_SBR_PATCHES; p++)
    {
        int remaining, src_start, i;
        faac_real src_e, scale, sig_scale, noise_scale;

        if (patch_len < MIN_PATCH_BINS)
            break;

        remaining = tgt_bin - tgt;
        if (remaining < MIN_PATCH_BINS)
            break;

        if (patch_len > remaining)
            patch_len = remaining;

        src_start = bw_bin - patch_len;
        if (src_start < 0)
        {
            patch_len += src_start;
            src_start  = 0;
            if (patch_len < MIN_PATCH_BINS)
                break;
        }

        src_e = band_energy(mdct + src_start, patch_len);

        if (src_e > (faac_real)1e-20)
        {
            scale = cum_gain;
        }
        else
        {
            tgt      += patch_len;
            patch_len = patch_len >> 1;
            cum_gain *= (faac_real)SBR_PATCH_ROLLOFF;
            continue;
        }

        sig_scale   = scale * ((faac_real)1.0 - (faac_real)SBR_NOISE_FRAC);
        noise_scale = scale * (faac_real)SBR_NOISE_FRAC * NOISE_SCALE;

        {
            faac_real * __restrict p_tgt = mdct + tgt;
            const faac_real * __restrict p_src = mdct + src_start;

            for (i = 0; i < patch_len; i++) {
                local_rand = local_rand * 1664525u + 1013904223u;
                p_tgt[i] = p_src[i] * sig_scale + (int)local_rand * noise_scale;
            }
        }

        tgt      += patch_len;
        patch_len = patch_len >> 1;
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
            apply_sbr_window(freqBuff + win * BLOCK_LEN_SHORT,
                             bw_bin, tgt_bin, rand);
    }
    else
    {
        bw_bin  = (int)((faac_real)baseBW * BLOCK_LEN_LONG * invSampleRate);
        tgt_bin = (int)((faac_real)sbrBW  * BLOCK_LEN_LONG * invSampleRate);

        if (tgt_bin > BLOCK_LEN_LONG) tgt_bin = BLOCK_LEN_LONG;
        if (bw_bin >= tgt_bin || bw_bin < MIN_PATCH_BINS * 2)
            return;

        apply_sbr_window(freqBuff, bw_bin, tgt_bin, rand);
    }
}

/* Calculate SBR target bandwidth based on bitrate and fill-ratio. */
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

    /* Stage 2: bitrate-tier extension amount. */
    if (bitRate < 12000u)
        ext_percent = 15u;
    else if (bitRate < 24000u)
        ext_percent = 25u;
    else
        ext_percent = 40u;

    extended  = baseBW + (baseBW * ext_percent / 100u);
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
