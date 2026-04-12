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

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <math.h>
#include <string.h>
#include "pseudo_sbr.h"
#include "util.h"
#include "huff2.h"

static float CalcSFM(faac_real *freq, int start, int end)
{
    double am = 0.0, gm = 0.0;
    int n = end - start;
    int i;

    if (n <= 0) return 1.0f;

    for (i = start; i < end; i++) {
        double val = (double)FAAC_FABS(freq[i]) + 1e-12;
        am += val;
        gm += log(val);
    }
    am /= n;
    gm = exp(gm / n);

    return (float)(gm / (am + 1e-12));
}

void ApplyPseudoSBR(CoderInfo *coder, faac_real *freq, int sampleRate, unsigned long bitRatePerChannel, SR_INFO *srInfo)
{
    int core_bins;
    int sbr_bins;
    float expansion_fraction;
    int i;
    int dest_idx;
    float sfm;
    int patch_offset;

    if (coder->block_type != ONLY_LONG_WINDOW)
        return;

    core_bins = coder->sfb_offset[coder->sfbn];
    coder->sbr_start_sfb = coder->sfbn;
    if (core_bins >= FRAME_LEN)
        return;

    /* Adaptive expansion fraction based on bitrate */
    if (bitRatePerChannel < SBR_LOW_BR_LIMIT)
        expansion_fraction = 0.50f;
    else if (bitRatePerChannel < SBR_MID_BR_LIMIT)
        expansion_fraction = 0.35f;
    else
        expansion_fraction = 0.25f;

    sbr_bins = (int)(core_bins * expansion_fraction);
    if (core_bins + sbr_bins > FRAME_LEN)
        sbr_bins = FRAME_LEN - core_bins;

    if (sbr_bins <= 0)
        return;

    /* Tonality Gating: check core for tonality */
    sfm = CalcSFM(freq, core_bins / 2, core_bins);

    /* 1. Spectral Folding (Translation): copy low-freq tile to high-freq.
       Better for preserving harmonic structures in speech.
    */
    patch_offset = core_bins / 2;
    if (patch_offset + sbr_bins > core_bins) {
        patch_offset = core_bins - sbr_bins;
    }
    if (patch_offset < 0) patch_offset = 0;

    /* Iteration 15/19: Adaptive Slope and Energy Matching.
       Measure energy trend of the upper core to ensure a seamless transition.
    */
    float upper_core_en = 0.0f;
    float mid_core_en = 0.0f;
    int upper_start = coder->sfb_offset[coder->sfbn > 1 ? coder->sfbn - 1 : 0];
    int mid_start = coder->sfb_offset[coder->sfbn > 2 ? coder->sfbn - 2 : 0];

    for (i = upper_start; i < core_bins; i++) upper_core_en += FAAC_FABS(freq[i]);
    for (i = mid_start; i < upper_start; i++) mid_core_en += FAAC_FABS(freq[i]);

    float slope_adj = 1.0f;
    if (mid_core_en > 1e-6f) {
        slope_adj = upper_core_en / (mid_core_en + 1e-12f);
        if (slope_adj > 1.5f) slope_adj = 1.5f;
        if (slope_adj < 0.5f) slope_adj = 0.5f;
    }

    float patch_en = 0.0f;
    for (i = 0; i < (sbr_bins < 64 ? sbr_bins : 64); i++) {
        patch_en += FAAC_FABS(freq[patch_offset + i]);
    }

    float energy_norm = 1.0f;
    if (patch_en > 1e-6f) {
        energy_norm = upper_core_en / (patch_en + 1e-12f);
        if (energy_norm > 2.0f) energy_norm = 2.0f;
        if (energy_norm < 0.5f) energy_norm = 0.5f;
    }

    for (i = 0; i < sbr_bins; i++) {
        dest_idx = core_bins + i;
        if (dest_idx >= FRAME_LEN) break;

        /* Combined gain from Iteration 12, 15, and 19 */
        float gain = SBR_GAIN_ROLLOFF * slope_adj * energy_norm;
        if (sampleRate <= SBR_SPEECH_SAMPLERATE_MAX) {
            gain *= 1.4f; /* Boost fricatives */
        }

        freq[dest_idx] = freq[patch_offset + i] * gain;

        /* Tonality Gating: attenuate if tonal to prevent metallic ringing.
           Iteration 16: Boost noise-like components to improve texture.
        */
        if (sfm < SBR_TONAL_THRESH) {
            float atten = (sampleRate <= SBR_SPEECH_SAMPLERATE_MAX) ? (SBR_TONAL_ATTEN * 1.5f) : SBR_TONAL_ATTEN;
            freq[dest_idx] *= atten;
        } else {
            freq[dest_idx] *= 1.2f;
        }

        /* Inject very low noise floor to maintain texture in SBR region */
        if (FAAC_FABS(freq[dest_idx]) < 1e-12f) {
            freq[dest_idx] = (i % 2 ? 1.0f : -1.0f) * SBR_HOLE_NOISE;
        }
    }

    /* 2. Transition Smoothing (16-bin cross-fade) */
    for (i = 0; i < 16 && (core_bins + i < FRAME_LEN); i++) {
        float fade = (float)(i + 1) / 17.0f;
        freq[core_bins + i] *= fade;
    }

    /* 3. Minimal Noise Filling in Core disabled to save bits. */

    /* 4. Extend SFBs up to Nyquist following standard definitions */
    int target_bins = core_bins + sbr_bins;
    while (coder->sfbn < srInfo->num_cb_long && coder->sfb_offset[coder->sfbn] < target_bins) {
        int next_offset = coder->sfb_offset[coder->sfbn] + srInfo->cb_width_long[coder->sfbn];
        if (next_offset > FRAME_LEN) next_offset = FRAME_LEN;
        coder->sfb_offset[coder->sfbn + 1] = next_offset;
        coder->sfbn++;
    }
}
