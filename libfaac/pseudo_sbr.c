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

/**
 * Calculates the Spectral Flatness Measure (SFM).
 */
static float CalcSFM(faac_real *freq, int start, int end)
{
    double am = 0.0, gm = 0.0;
    int n = end - start;
    int i;

    if (n <= 0) return 0.0f;

    for (i = start; i < end; i++) {
        double val = (double)FAAC_FABS(freq[i]) + 1e-12;
        am += val;
        gm += log(val);
    }
    am /= n;
    gm = exp(gm / n);

    return (float)(gm / (am + 1e-12));
}

/**
 * Applies Pseudo-SBR (Spectral Band Replication) at the encoder side.
 * Reconstructs high frequencies by folding (translating) the core spectrum.
 */
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
    if (core_bins >= FRAME_LEN)
        return;

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

    sfm = CalcSFM(freq, core_bins / 2, core_bins);

    /* 1. Spectral Folding (Translation): Better for speech harmonics */
    patch_offset = core_bins / 2;
    if (patch_offset + sbr_bins > core_bins) {
        patch_offset = core_bins - sbr_bins;
    }
    if (patch_offset < 0) patch_offset = 0;

    for (i = 0; i < sbr_bins; i++) {
        dest_idx = core_bins + i;
        if (dest_idx >= FRAME_LEN) break;

        freq[dest_idx] = freq[patch_offset + i] * SBR_GAIN_ROLLOFF;

        if (sfm < SBR_TONAL_THRESH) {
            float atten = (sampleRate <= SBR_SPEECH_SAMPLERATE_MAX) ? (SBR_TONAL_ATTEN * 0.5f) : SBR_TONAL_ATTEN;
            freq[dest_idx] *= atten;
        }
    }

    /* 2. Transition Smoothing */
    for (i = 0; i < 16 && (core_bins + i < FRAME_LEN); i++) {
        float fade = (float)(i + 1) / 17.0f;
        freq[core_bins + i] *= fade;
    }

    /**
     * 3. Update Scale Factor Bands (SFBs)
     * Use standard band definitions for bitstream compatibility.
     */
    int target_bins = core_bins + sbr_bins;
    while (coder->sfbn < srInfo->num_cb_long && coder->sfb_offset[coder->sfbn] < target_bins) {
        int next_offset = coder->sfb_offset[coder->sfbn] + srInfo->cb_width_long[coder->sfbn];
        if (next_offset > FRAME_LEN) next_offset = FRAME_LEN;
        coder->sfb_offset[coder->sfbn + 1] = next_offset;
        coder->sfbn++;
    }
}
