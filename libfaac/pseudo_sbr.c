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
#  define SBR_NOISE_FRAC     0.12f
#else
#  define SBR_PATCH_ROLLOFF  0.50
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

static faac_real compute_sfm(const faac_real * __restrict mdct, int len)
{
    faac_real am = 0.0, gm = 0.0;
    int i;

    for (i = 0; i < len; i++) {
        faac_real e = mdct[i] * mdct[i];
        am += e;
        gm += logf(max(e, 1e-10f));
    }

    am /= len;
    gm = expf(gm / len);

    return (am > 1e-10f) ? (gm / am) : 0.0f;
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
                            unsigned int bitRate,
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

        if (src_e > (faac_real)1e-20)
        {
            faac_real sfm = compute_sfm(mdct + src_start, patch_len);
            /* Conservative adaptive noise: less noise for tonal bands, max ~0.15 for noisy bands */
            faac_real adaptive_noise_frac = 0.02f + sfm * 0.12f;
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
                local_rand = local_rand * 1664525u + 1013904223u;
                p_tgt[i] = p_src[i] * sig_scale + (int32_t)(local_rand >> 1) * noise_scale;
            }

            /* Normalize extended patch energy to match target fraction of source energy.
             * Clamped correction prevents extreme gain fluctuations. */
            written_e = band_energy(p_tgt, patch_len);
            if (written_e > (faac_real)1e-20)
            {
                faac_real norm_fac = (faac_real)1.0;
                /* More aggressive normalization for low bitrates to protect core quality */
                if (bitRate < 12000u) norm_fac = (faac_real)0.03;
                else if (bitRate < 24000u) norm_fac = (faac_real)0.08;
                else if (bitRate < 32000u) norm_fac = (faac_real)0.20;
                else if (bitRate < 48000u) norm_fac = (faac_real)0.45;

                correction = sqrtf((src_e * scale * scale * norm_fac) / written_e);
                if (correction > (faac_real)1.5) correction = (faac_real)1.5;
                if (correction < (faac_real)0.1) correction = (faac_real)0.1;
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

/* Calculate SBR target bandwidth based on bitrate and fill-ratio. */
unsigned int PseudoSBRTargetBW(unsigned int sampleRate,
                                unsigned int baseBW,
                                unsigned int bitRate)
{
    unsigned int extended, nyquist90;
    unsigned int targetBW, growth_cap;

    if (sampleRate == 0 || baseBW == 0)
        return baseBW;

    /* Stage 2: bitrate-tier absolute target bandwidths with conservative growth caps.
     * Prevents core bit starvation at low bitrates while aiming for specific targets. */
    if (bitRate <= 12000u) {
        targetBW   = 7000u;
        growth_cap = 300u;
    } else if (bitRate <= 24000u) {
        /* Interpolate targets and caps */
        targetBW   = 7000u + (bitRate - 12000u) * (10000u - 7000u) / (24000u - 12000u);
        growth_cap = 300u  + (bitRate - 12000u) * (1500u  - 300u)  / (24000u - 12000u);
    } else if (bitRate <= 64000u) {
        targetBW   = 10000u + (bitRate - 24000u) * (14000u - 10000u) / (64000u - 24000u);
        growth_cap = 1500u  + (bitRate - 24000u) * (4000u  - 1500u)  / (64000u - 24000u);
    } else {
        targetBW   = 14000u + (bitRate - 64000u) * 2000u / 64000u;
        growth_cap = 4000u  + (bitRate - 64000u) * 2000u / 64000u;
    }

    extended = max(baseBW, targetBW);
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
