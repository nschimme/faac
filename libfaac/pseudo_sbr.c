/****************************************************************************
    Encoder-side Pseudo Spectral Band Replication for AAC-LC

    Copyright (C) 2026 Nils Schimmelmann

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
****************************************************************************/

#include <math.h>
#include <string.h>
#include <stdint.h>

#include "pseudo_sbr.h"
#include "coder.h"
#include "faac_real.h"
#include "huff2.h"

#define MAX_SBR_PATCHES   4
#define SBR_PATCH_ROLLOFF 0.354f
#define SBR_NOISE_FRAC    0.12f
#define SBR_COMFORT_NOISE 0.005f
#define SBR_XFADE_BINS    4
#define MIN_PATCH_BINS    8

static float lcg_float(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return (float)(*state >> 16) / 65536.0f - 0.5f;
}

static int bw_to_bin(unsigned int bw_hz, unsigned long sampleRate)
{
    return (int)((unsigned long long)bw_hz * 2ULL * 1024 / sampleRate);
}

int PseudoSBRShouldEnable(unsigned int naturalBW, unsigned long sampleRate,
                           unsigned long bitRateCh)
{
    unsigned int nyquist;
    unsigned int thresh;

    if (!bitRateCh || !sampleRate)
        return 0;

    nyquist = (unsigned int)(sampleRate / 2);
    if (!nyquist)
        return 0;

    if      (bitRateCh < 24000) thresh = 90u;
    else if (bitRateCh < 48000) thresh = 65u;
    else                        thresh = 40u;

    return (naturalBW * 100u / nyquist) < thresh;
}

unsigned int PseudoSBRTargetBW(unsigned int naturalBW, unsigned long sampleRate,
                                unsigned long bitRateCh)
{
    unsigned int nyquist = (unsigned int)(sampleRate / 2);
    float expansion;

    if (!nyquist || naturalBW >= nyquist)
        return naturalBW;

    if      (bitRateCh < 12000) expansion = 0.15f;
    else if (bitRateCh < 24000) expansion = 0.25f;
    else if (bitRateCh < 48000) expansion = 0.40f;
    else                        return naturalBW;

    unsigned int target = (unsigned int)((float)naturalBW * (1.0f + expansion));
    unsigned int cap = nyquist * 90u / 100u;
    return (target < cap) ? target : cap;
}

void PseudoSBR(CoderInfo *ci, faac_real *freq, unsigned long sampleRate,
               unsigned int baseBW, unsigned int targetBW, unsigned long bitRateCh,
               const int *cb_width_long, int num_cb_long, uint32_t *randState)
{
    int bwb, tgb, src_bwb, dst, patch, i, sb, off;
    float cumulative_gain;

    if (ci->block_type != ONLY_LONG_WINDOW)
        return;

    bwb = bw_to_bin(baseBW, sampleRate);
    tgb = bw_to_bin(targetBW, sampleRate);
    if (num_cb_long > 0) {
        int max_bin = 0;
        for (i = 0; i < num_cb_long && i < NSFB_LONG; i++)
            max_bin += cb_width_long[i];
        if (tgb > max_bin)
            tgb = max_bin;
    }
    if (tgb <= bwb + MIN_PATCH_BINS || bwb <= MIN_PATCH_BINS)
        return;

    src_bwb = bwb;
    dst = bwb;
    cumulative_gain = 1.0f;
    for (patch = 0; patch < MAX_SBR_PATCHES && dst < tgb; patch++) {
        int remaining = tgb - dst;
        int src_size = (src_bwb / 2 < remaining) ? (src_bwb / 2) : remaining;
        float src_energy = 0.0f;
        float noise_scale, boost_g;

        if (src_size < MIN_PATCH_BINS)
            break;

        for (i = 0; i < src_size; i++)
            src_energy += (float)(freq[src_bwb - 1 - i] * freq[src_bwb - 1 - i]);

        noise_scale = (src_energy > 1e-15f) ? SBR_NOISE_FRAC * sqrtf(src_energy / (float)src_size) : 0.0f;
        boost_g = (cumulative_gain < 0.1f) ? 0.1f : cumulative_gain;

        for (i = 0; i < src_size; i++) {
            float s = (float)freq[src_bwb - 1 - i] * boost_g;
            float n = lcg_float(randState) * noise_scale;
            float c = lcg_float(randState) * SBR_COMFORT_NOISE;
            float val = s + n + c;

            if (patch == 0 && i < SBR_XFADE_BINS) {
                float alpha = (float)(i + 1) / (float)(SBR_XFADE_BINS + 1);
                val = val * alpha + (float)freq[dst + i] * (1.0f - alpha);
            }
            freq[dst + i] = (faac_real)val;
        }
        dst += src_size;
        cumulative_gain *= SBR_PATCH_ROLLOFF;
    }
    if (dst < tgb)
        memset(freq + dst, 0, (size_t)(tgb - dst) * sizeof(faac_real));

    sb = ci->sfbn;
    off = ci->sfb_offset[sb];
    while (sb < num_cb_long && sb < NSFB_LONG && sb < MAX_SCFAC_BANDS && off < tgb) {
        ci->sfb_offset[sb + 1] = off + cb_width_long[sb];
        ci->book[sb] = HCB_ZERO;
        ci->sf[sb] = 0;
        off = ci->sfb_offset[sb + 1];
        sb++;
    }
    ci->sfbn = sb;
}
