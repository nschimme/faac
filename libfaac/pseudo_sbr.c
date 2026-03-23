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
#  define SBR_PATCH_ROLLOFF  0.50f
#else
#  define SBR_PATCH_ROLLOFF  0.50
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
                            unsigned int *rand)
{
    int tgt       = bw_bin;
    faac_real cum_gain = (faac_real)1.0;
    unsigned int local_rand = *rand;
    int p;

    for (p = 0; p < MAX_SBR_PATCHES; p++)
    {
        int remaining, src_start, i, patch_len;
        faac_real src_e, tgt_rms_goal, sig_scale, noise_scale;
        float sfm, adaptive_noise_frac;

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

        /* 1. Calculate target goal */
        tgt_rms_goal = sqrtf((float)src_e / patch_len) * cum_gain;

        /* 2. Determine noise fraction via SFM */
        sfm = compute_sfm(mdct + src_start, patch_len);
        adaptive_noise_frac = 0.04f + sfm * 0.20f;

        sig_scale   = (faac_real)1.0 - (faac_real)adaptive_noise_frac;
        noise_scale = (faac_real)adaptive_noise_frac * NOISE_SCALE;

        /* 3. Fill patch */
        {
            faac_real * __restrict p_tgt = mdct + tgt;
            const faac_real * __restrict p_src = mdct + src_start;

            for (i = 0; i < patch_len; i++) {
                local_rand = local_rand * 1664525u + 1013904223u;
                int32_t noise_samp = (int32_t)(local_rand >> 1) - 0x20000000;
                p_tgt[i] = p_src[i] * sig_scale + (faac_real)noise_samp * noise_scale;
            }
        }

        /* 4. Energy normalization correction */
        {
            faac_real written_e = band_energy(mdct + tgt, patch_len);
            if (written_e > (faac_real)1e-20) {
                faac_real correction = tgt_rms_goal / sqrtf((float)written_e / patch_len);
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

/* Calculate SBR target bandwidth based on bitrate and fill-ratio. */
unsigned int PseudoSBRTargetBW(unsigned int sampleRate,
                                unsigned int baseBW,
                                unsigned int bitRate)
{
    unsigned int tgt_bw;
    unsigned int nyquist90;

    if (sampleRate == 0 || baseBW == 0)
        return baseBW;

    /* Relative fill-ratio gate is handled by PseudoSBRShouldEnable. */

    /* Stage 2: bitrate-tier extension amount. */
    if      (bitRate < 16000u)  tgt_bw = 7000u;
    else if (bitRate < 32000u)  tgt_bw = 10000u;
    else if (bitRate < 80000u)  tgt_bw = 14000u;
    else                        tgt_bw = 16000u;

    nyquist90 = sampleRate * 9u / 20u;

    if (tgt_bw > nyquist90)
        tgt_bw = nyquist90;

    if (tgt_bw <= baseBW + SBR_MIN_EXTENSION_HZ)
        return baseBW;

    return tgt_bw;
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
