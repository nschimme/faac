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

/* -----------------------------------------------------------------------
 * Design constants
 *
 * SBR_FILL_RATIO_MAX (0.65)
 *   The maximum fraction of Nyquist the natural encoder bandwidth may
 *   already occupy before pseudo-SBR is suppressed.
 *
 *   Derivation: the encoder formula gives
 *     naturalBW = bitRate * sampleRate * 0.42 / 50000
 *   Dividing by (sampleRate/2):
 *     fillRatio  = bitRate * 0.42 / 25000  =  bitRate / 59524
 *
 *   fillRatio = 0.65  <=>  bitRate ~= 38700 bps
 *
 *   At 16 kHz / 40 kbps: fillRatio = 5376 / 8000 = 0.672  -> suppressed
 *   At 16 kHz / 16 kbps: fillRatio = 2150 / 8000 = 0.269  -> enabled
 *   At 48 kHz / 32 kbps: fillRatio = 12902/24000 = 0.538  -> enabled
 *
 * SBR_PATCH_ROLLOFF (~-9 dB = 0.354)
 *   Each successive patch drops by 9 dB to minimise bit-cost of the
 *   patched region while keeping it above the decoder noise floor.
 *
 * SBR_NOISE_FRAC (0.12)
 *   12 % of patch amplitude replaced with white noise to de-correlate
 *   the spectral copy and prevent pitch-periodicity artifacts.
 *
 * SBR_MIN_EXTENSION_HZ (500)
 *   Minimum worthwhile extension; narrower gaps are ignored.
 * --------------------------------------------------------------------- */

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

static faac_real sbr_noise_next(unsigned int *state)
{
    *state = *state * 1664525u + 1013904223u;
    return (int)(*state) * (faac_real)(1.0 / 2147483648.0);
}

static faac_real band_energy(const faac_real * __restrict mdct,
                             int start, int len)
{
    faac_real e = 0.0;
    int i;
    for (i = 0; i < len; i++)
        e += mdct[start + i] * mdct[start + i];
    return e;
}

/* -----------------------------------------------------------------------
 * apply_sbr_window()
 *
 * Fill [bw_bin, tgt_bin) with energy-normalised, noise-dithered copies
 * of the top octave of the coded bandwidth.
 *
 * Source: always re-read from [bw_bin - patch_len, bw_bin).
 * Cascading through already-patched bins compounds quantisation noise.
 *
 * IMPORTANT: there is no minimum scale floor.  If the source band is
 * silent, the target band stays silent.  A forced floor would inject
 * constant artificial noise during pauses above baseBW, which speech
 * quality metrics (PESQ, WARP) score as heavy distortion.
 * --------------------------------------------------------------------- */
static void apply_sbr_window(faac_real * __restrict mdct,
                             int bw_bin, int tgt_bin,
                             unsigned int *rand)
{
    int tgt       = bw_bin;
    int patch_len = bw_bin / 2;
    faac_real cum_gain = (faac_real)SBR_PATCH_ROLLOFF;
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

        src_e = band_energy(mdct, src_start, patch_len);

        if (src_e > (faac_real)1e-20)
        {
            /* scale = cum_gain  (amplitude factor, derived from energy ratio) */
            scale = cum_gain;
        }
        else
        {
            /* Source band silent: leave target silent, advance bookkeeping. */
            tgt      += patch_len;
            patch_len = patch_len / 2;
            cum_gain *= (faac_real)SBR_PATCH_ROLLOFF;
            continue;
        }

        sig_scale   = scale * ((faac_real)1.0 - (faac_real)SBR_NOISE_FRAC);
        noise_scale = scale * (faac_real)SBR_NOISE_FRAC;

        for (i = 0; i < patch_len; i++)
            mdct[tgt + i] = mdct[src_start + i] * sig_scale
                          + sbr_noise_next(rand)  * noise_scale;

        tgt      += patch_len;
        patch_len = patch_len / 2;
        cum_gain *= (faac_real)SBR_PATCH_ROLLOFF;
    }
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

    if (baseBW >= sbrBW || sampleRate == 0)
        return;

    if ((sbrBW - baseBW) < SBR_MIN_EXTENSION_HZ)
        return;

    if (coderInfo->block_type == ONLY_SHORT_WINDOW)
    {
        int win;

        bw_bin  = (int)((faac_real)baseBW * 2 * BLOCK_LEN_SHORT
                        / (faac_real)sampleRate);
        tgt_bin = (int)((faac_real)sbrBW  * 2 * BLOCK_LEN_SHORT
                        / (faac_real)sampleRate);

        if (tgt_bin > BLOCK_LEN_SHORT) tgt_bin = BLOCK_LEN_SHORT;
        if (bw_bin >= tgt_bin || bw_bin < MIN_PATCH_BINS * 2)
            return;

        for (win = 0; win < MAX_SHORT_WINDOWS; win++)
            apply_sbr_window(freqBuff + win * BLOCK_LEN_SHORT,
                             bw_bin, tgt_bin, rand);
    }
    else
    {
        bw_bin  = (int)((faac_real)baseBW * 2 * BLOCK_LEN_LONG
                        / (faac_real)sampleRate);
        tgt_bin = (int)((faac_real)sbrBW  * 2 * BLOCK_LEN_LONG
                        / (faac_real)sampleRate);

        if (tgt_bin > BLOCK_LEN_LONG) tgt_bin = BLOCK_LEN_LONG;
        if (bw_bin >= tgt_bin || bw_bin < MIN_PATCH_BINS * 2)
            return;

        apply_sbr_window(freqBuff, bw_bin, tgt_bin, rand);
    }
}

/*
 * PseudoSBRTargetBW()
 *
 * Return the SBR ceiling bandwidth.
 *
 * Two-stage decision:
 *
 * Stage 1 – Fill-ratio gate (suppression).
 *   If the encoder already covers >= 65 % of Nyquist, pseudo-SBR will
 *   starve core bands and is suppressed unconditionally.  This is the
 *   fix for the VSS regression (16 kHz / 40 kbps: fill = 0.67).
 *
 *   The 65 % threshold maps to ~38.7 kbps regardless of sample rate:
 *     fill = bitRate * 0.42 / 25000  =>  0.65 @ bitRate ≈ 38,690 bps
 *
 * Stage 2 – Bitrate-tier extension amount.
 *   Once SBR is permitted, the extension fraction is chosen from a
 *   three-tier table that was validated empirically:
 *
 *     bitRate < 12 kbps  →  15 %   (very tight budget, tiny lift)
 *     bitRate < 24 kbps  →  25 %   (VoIP: 2150→2688 Hz, +0.22 MOS)
 *     bitRate >= 24 kbps →  40 %   (music_low: 12900→18060 Hz, +0.29 MOS)
 *
 *   Using fill-ratio for the extension AMOUNT (instead of this table)
 *   caused the VoIP regression: fill=0.27 mapped to 47% extension
 *   (→ 3158 Hz) instead of 25% (→ 2688 Hz), costing ~1008 extra Hz at
 *   16 kbps and starving the 1–2 kHz formant range that PESQ weights
 *   most heavily.
 *
 *   Results:
 *     VoIP  (16 kHz / 16 kbps): fill=0.27, 25% → +0.22 MOS  ✓
 *     music_low (48 kHz / 32 kbps/ch): fill=0.54, 40% → +0.29 MOS  ✓
 *     VSS   (16 kHz / 40 kbps): fill=0.67 → suppressed  ✓
 */
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

/*
 * PseudoSBRShouldEnable()
 *
 * Returns 1 if pseudo-SBR is expected to provide a net quality benefit.
 * Use this for the auto-enable logic in faacEncSetConfiguration() in
 * place of the raw `bitRate < 48000` check.
 *
 * The test is simply: fill ratio < SBR_FILL_RATIO_MAX.  This correctly
 * suppresses SBR for high-bitrate / low-samplerate combinations such as
 * 40 kbps mono at 16 kHz (fill = 0.67, suppressed) while enabling it
 * for 16 kbps mono at 16 kHz (fill = 0.27, enabled) and 32 kbps per
 * channel at 48 kHz (fill = 0.54, enabled).
 */
int PseudoSBRShouldEnable(unsigned int sampleRate, unsigned int naturalBW)
{
    float fillRatio;

    if (sampleRate == 0 || naturalBW == 0)
        return 0;

    fillRatio = (float)naturalBW / (float)(sampleRate / 2);
    return (fillRatio < SBR_FILL_RATIO_MAX) ? 1 : 0;
}
