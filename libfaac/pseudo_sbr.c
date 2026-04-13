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

    /* SBR is most effective at lower bitrates. */
    if (bitRatePerChannel > 64000)
        return;

    /* SBR provides negligible benefit for low sample rate speech. */
    if (sampleRate <= 16000)
        return;


    core_bins = coder->sfb_offset[coder->sfbn];
    coder->sbr_start_sfb = coder->sfbn;

    /* Limit SBR to cases where core is not already full-band */
    if (core_bins >= 1000)
        return;

    if (core_bins >= FRAME_LEN)
        return;

    /* Expansion fraction. */
    expansion_fraction = 0.50f;

    sbr_bins = (int)(core_bins * expansion_fraction);
    if (core_bins + sbr_bins > FRAME_LEN)
        sbr_bins = FRAME_LEN - core_bins;

    if (sbr_bins <= 0)
        return;

    /* 1. Combined analysis pass.
       Calculates SFM, upper core energy, and mid core energy.
    */
    double am = 0.0, gm = 0.0;
    float upper_core_en = 0.0f;
    float mid_core_en = 0.0f;
    int sfm_start = core_bins / 2;
    int upper_start = coder->sfb_offset[coder->sfbn > 1 ? coder->sfbn - 1 : 0];
    int mid_start = coder->sfb_offset[coder->sfbn > 2 ? coder->sfbn - 2 : 0];
    int analysis_len = core_bins - sfm_start;

    if (analysis_len > 0) {
        for (i = sfm_start; i < core_bins; i++) {
            float val = FAAC_FABS(freq[i]);
            double dval = (double)val + 1e-12;
            am += dval;
            gm += log(dval);
            if (i >= upper_start) upper_core_en += val;
            else if (i >= mid_start) mid_core_en += val;
        }
        am /= analysis_len;
        gm = exp(gm / analysis_len);
        sfm = (float)(gm / (am + 1e-12));
    } else {
        sfm = 1.0f;
    }

    /* 2. Determine patch offset. */
    patch_offset = core_bins / 2;
    if (patch_offset + sbr_bins > core_bins) {
        patch_offset = core_bins - sbr_bins;
    }
    if (patch_offset < 0) patch_offset = 0;

    float patch_en = 0.0f;
    int patch_check_len = (sbr_bins < 64 ? sbr_bins : 64);
    for (i = 0; i < patch_check_len; i++) {
        patch_en += FAAC_FABS(freq[patch_offset + i]);
    }

    /* 3. Calculate optimized combined gain factors. */
    float slope_adj = 1.0f;
    if (mid_core_en > 1e-6f) {
        slope_adj = upper_core_en / (mid_core_en + 1e-12f);
        if (slope_adj > 1.5f) slope_adj = 1.5f;
        if (slope_adj < 0.5f) slope_adj = 0.5f;
    }

    float energy_norm = 1.0f;
    if (patch_en > 1e-6f) {
        energy_norm = upper_core_en / (patch_en + 1e-12f);
        if (energy_norm > 1.5f) energy_norm = 1.5f;
        if (energy_norm < 0.7f) energy_norm = 0.7f;
    }

    float final_gain = SBR_GAIN_ROLLOFF * slope_adj * energy_norm;

    if (sfm < SBR_TONAL_THRESH) {
        final_gain *= SBR_TONAL_ATTEN;
    }

    /* 4. Optimized SBR folding and noise injection loop. */
    for (i = 0; i < sbr_bins; i++) {
        dest_idx = core_bins + i;
        if (UNLIKELY(dest_idx >= FRAME_LEN)) break;

        freq[dest_idx] = freq[patch_offset + i] * final_gain;

        /* Minimal hole filling to maintain spectral texture. */
        if (UNLIKELY(FAAC_FABS(freq[dest_idx]) < 1e-12f)) {
            freq[dest_idx] = (i % 2 ? 1.0f : -1.0f) * SBR_HOLE_NOISE;
        }
    }

    /* 5. Transition Smoothing (16-bin cross-fade). */
    for (i = 0; i < 16 && (core_bins + i < FRAME_LEN); i++) {
        float fade = (float)(i + 1) / 17.0f;
        freq[core_bins + i] *= fade;
    }

    /* 6. Metadata: Extend SFBs to cover the replicated region. */
    int target_bins = core_bins + sbr_bins;
    while (coder->sfbn < srInfo->num_cb_long && coder->sfb_offset[coder->sfbn] < target_bins) {
        int next_offset = coder->sfb_offset[coder->sfbn] + srInfo->cb_width_long[coder->sfbn];
        if (next_offset > FRAME_LEN) next_offset = FRAME_LEN;
        coder->sfb_offset[coder->sfbn + 1] = next_offset;
        coder->sfbn++;
    }
}
