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
#include "filtbank.h"   /* BLOCK_LEN_LONG, BLOCK_LEN_SHORT, MAX_SHORT_WINDOWS */
#include "faac_real.h"
#include "util.h"       /* min() */

/* -----------------------------------------------------------------------
 * Tuning constants
 * --------------------------------------------------------------------- */

/* Maximum number of spectral patches per window. */
#define MAX_SBR_PATCHES     4

/* Minimum useful patch width in MDCT bins.
 * Patches narrower than this are skipped.                               */
#define MIN_PATCH_BINS      16

/* Amplitude gain applied to each successive patch (-3 dB per step).
 * Less rolloff allows more HF energy to be preserved, potentially
 * increasing the perceived brightness and MOS score.                    */
#ifdef FAAC_PRECISION_SINGLE
#  define SBR_PATCH_ROLLOFF  0.707f  /* 10^(-3/20) ≈ 0.707 */
#else
#  define SBR_PATCH_ROLLOFF  0.707
#endif

/* Fraction of patch amplitude replaced with band-limited white noise.
 * Breaks the pitch-periodicity of a direct spectral copy, giving a more
 * natural, diffuse HF texture ("breathiness").                          */
#ifdef FAAC_PRECISION_SINGLE
#  define SBR_NOISE_FRAC     0.15f
#else
#  define SBR_NOISE_FRAC     0.15
#endif

/* Minimum bandwidth extension worth applying (Hz).
 * If the achievable extension is smaller than this, skip pseudo-SBR.    */
#define SBR_MIN_EXTENSION_HZ  1500u

/* -----------------------------------------------------------------------
 * Private helpers
 * --------------------------------------------------------------------- */

/*
 * Simple Galois LFSR / LCG hybrid – fast, no libm, no shared state.
 * Returns a value in (-1, 1).
 */
static faac_real sbr_noise_next(unsigned int *state)
{
    *state = *state * 1664525u + 1013904223u;
    return (int)(*state) * (faac_real)(1.0 / 2147483648.0);
}

/* Energy of mdct[start .. start+len-1]. */
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
 * Core patching routine for a single MDCT window.
 *
 *   mdct    – coefficient array for this window [0 .. frame_len-1]
 *   bw_bin  – first bin at/above the encoder's natural bandwidth limit
 *             (= source ceiling; bins [0..bw_bin-1] contain real audio)
 *   tgt_bin – target bin ceiling; fill [bw_bin .. tgt_bin-1] with SBR
 *             content (must be ≤ frame_len)
 *   rand    – per-encoder LCG state, advanced in place
 *
 * Algorithm
 * ---------
 * Each patch copies the "top half" of the coded bandwidth to the next
 * higher octave, applying:
 *   1. An energy-normalised amplitude scale so that each patch carries
 *      less energy than its predecessor, matching the expected
 *      high-frequency spectral slope of broadband audio.
 *   2. A small fraction (SBR_NOISE_FRAC) of band-limited white noise
 *      mixed with the copied signal.  This breaks the temporal
 *      periodicity of a naked spectral copy, which would otherwise
 *      create an audible "pitched" artifact in the HF.
 *
 * Source selection: patches always read from [bw_bin - patch_len, bw_bin),
 * i.e., the topmost coded octave.  Reusing the same source region (rather
 * than cascading through previously patched bins) prevents compounding
 * quantisation noise across patches.
 * --------------------------------------------------------------------- */
static void apply_sbr_window(faac_real * __restrict mdct,
                             int bw_bin, int tgt_bin,
                             unsigned int *rand)
{
    int tgt           = bw_bin;
    int patch_len     = bw_bin / 2;   /* width of first patch: one octave */
    faac_real cum_gain = (faac_real)SBR_PATCH_ROLLOFF;   /* gain for patch 0 */
    int p;

    for (p = 0; p < MAX_SBR_PATCHES; p++)
    {
        int remaining, src_start;
        faac_real src_e, tgt_e_desired, scale;
        faac_real sig_scale, noise_scale;
        int i;

        if (patch_len < MIN_PATCH_BINS)
            break;

        remaining = tgt_bin - tgt;
        if (remaining < MIN_PATCH_BINS)
            break;

        if (patch_len > remaining)
            patch_len = remaining;

        /* Source: top [patch_len] bins of the coded bandwidth.
         * Clamp so we never read below bin 0.                            */
        src_start = bw_bin - patch_len;
        if (src_start < 0)
        {
            patch_len += src_start;   /* shrink to fit */
            src_start  = 0;
            if (patch_len < MIN_PATCH_BINS)
                break;
        }

        /* Energy of source region.                                       */
        src_e = band_energy(mdct, src_start, patch_len);

        if (src_e > (faac_real)1e-20)
        {
            /*
             * Desired target energy = src_e * cum_gain^2
             * (cum_gain is already the amplitude factor, so squared gives
             *  the energy ratio.)
             */
            tgt_e_desired = src_e * cum_gain * cum_gain;
            scale = FAAC_SQRT(tgt_e_desired / src_e);   /* = cum_gain */
        }
        else
        {
            scale = (faac_real)0.0;
        }

        sig_scale   = scale * ((faac_real)1.0 - (faac_real)SBR_NOISE_FRAC);
        noise_scale = scale * (faac_real)SBR_NOISE_FRAC;

        for (i = 0; i < patch_len; i++)
            mdct[tgt + i] = mdct[src_start + i] * sig_scale
                          + sbr_noise_next(rand)  * noise_scale;

        tgt      += patch_len;
        patch_len = patch_len / 2;          /* halve for next patch      */
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

        /* Each short window covers the full [0, sampleRate/2] range in
         * BLOCK_LEN_SHORT bins.                                          */
        bw_bin  = (int)((faac_real)baseBW * 2 * BLOCK_LEN_SHORT
                        / (faac_real)sampleRate);
        tgt_bin = (int)((faac_real)sbrBW  * 2 * BLOCK_LEN_SHORT
                        / (faac_real)sampleRate);

        if (tgt_bin > BLOCK_LEN_SHORT)
            tgt_bin = BLOCK_LEN_SHORT;
        if (bw_bin >= tgt_bin || bw_bin < MIN_PATCH_BINS * 2)
            return;

        for (win = 0; win < MAX_SHORT_WINDOWS; win++)
            apply_sbr_window(freqBuff + win * BLOCK_LEN_SHORT,
                             bw_bin, tgt_bin, rand);
    }
    else
    {
        /* Long / start / stop windows: one 1024-bin spectrum.           */
        bw_bin  = (int)((faac_real)baseBW * 2 * BLOCK_LEN_LONG
                        / (faac_real)sampleRate);
        tgt_bin = (int)((faac_real)sbrBW  * 2 * BLOCK_LEN_LONG
                        / (faac_real)sampleRate);

        if (tgt_bin > BLOCK_LEN_LONG)
            tgt_bin = BLOCK_LEN_LONG;
        if (bw_bin >= tgt_bin || bw_bin < MIN_PATCH_BINS * 2)
            return;

        apply_sbr_window(freqBuff, bw_bin, tgt_bin, rand);
    }
}

unsigned int PseudoSBRTargetBW(unsigned int sampleRate, unsigned int baseBW)
{
    /* Extend by at most 50 % of the base bandwidth. */
    unsigned int extended  = baseBW + baseBW / 2;

    /* Hard ceiling: 90 % of Nyquist, leaving room for the AAC anti-alias
     * region and ensuring we don't try to encode content the filterbank
     * cannot reproduce cleanly.                                          */
    unsigned int nyquist90 = sampleRate * 9u / 20u;

    if (extended > nyquist90)
        extended = nyquist90;

    /* Only return an extension if the gain is meaningful.               */
    if (extended <= baseBW + SBR_MIN_EXTENSION_HZ)
        return baseBW;

    return extended;
}
