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

/**
 * ApplyPseudoSBR - Minimal Spectral Folding for AAC-LC
 *
 * Implements a strictly additive high-frequency reconstruction by folding
 * mid-core spectrum tiles. Optimized for 48kHz content at 24-64kbps.
 */
void ApplyPseudoSBR(CoderInfo *coder, faac_real *freq, unsigned long bitRatePerChannel, SR_INFO *srInfo, int useSbr)
{
    int core_bins, sbr_bins, i, dest_idx, patch_offset;
    const float expansion_fraction = 0.50f; /* 1.5x bandwidth extension */

    if (coder->block_type != ONLY_LONG_WINDOW)
        return;

    /* SBR Activation Logic */
    if (useSbr == SBR_OFF)
        return;

    if (useSbr == SBR_AUTO) {
        /* Enable for per-channel bitrates <= 48kbps (96kbps total stereo). */
        if (bitRatePerChannel > 48000 || srInfo->sampling_rate < 32000)
            return;
    }

    core_bins = coder->sfb_offset[coder->sfbn];
    coder->sbr_start_sfb = coder->sfbn;

    if (core_bins >= FRAME_LEN)
        return;

    sbr_bins = (int)(core_bins * expansion_fraction);
    if (core_bins + sbr_bins > FRAME_LEN)
        sbr_bins = FRAME_LEN - core_bins;

    if (sbr_bins <= 0)
        return;

    /* Harmonic Folding (2:1 mapping) */
    patch_offset = core_bins / 2;
    if (patch_offset + sbr_bins > core_bins) {
        patch_offset = core_bins - sbr_bins;
    }
    if (patch_offset < 0) patch_offset = 0;

    /* Match upper core energy density for natural level scaling. */
    float upper_en = 1e-12f, patch_en = 1e-12f;
    int alen = (sbr_bins < 64) ? sbr_bins : 64;
    for (i = 0; i < alen; i++) {
        upper_en += FAAC_FABS(freq[core_bins - 1 - i]);
        patch_en += FAAC_FABS(freq[patch_offset + i]);
    }

    float final_gain = SBR_GAIN_ROLLOFF * (upper_en / patch_en);
    if (final_gain > 1.2f) final_gain = 1.2f;
    if (final_gain < 0.3f) final_gain = 0.3f;

    for (i = 0; i < sbr_bins; i++) {
        dest_idx = core_bins + i;
        if (UNLIKELY(dest_idx >= FRAME_LEN)) break;
        freq[dest_idx] = freq[patch_offset + i] * final_gain;
        if (UNLIKELY(FAAC_FABS(freq[dest_idx]) < 1e-12f)) {
            freq[dest_idx] = (i & 1 ? SBR_HOLE_NOISE : -SBR_HOLE_NOISE);
        }
    }

    /* 16-bin linear cross-fade smoothing */
    for (i = 0; i < 16 && (core_bins + i < FRAME_LEN); i++) {
        freq[core_bins + i] *= (float)(i + 1) * 0.0588f;
    }

    /* Metadata Update */
    int target_bins = core_bins + sbr_bins;
    while (coder->sfbn < srInfo->num_cb_long && coder->sfb_offset[coder->sfbn] < target_bins) {
        int next_offset = coder->sfb_offset[coder->sfbn] + srInfo->cb_width_long[coder->sfbn];
        if (next_offset > FRAME_LEN) next_offset = FRAME_LEN;
        coder->sfb_offset[coder->sfbn + 1] = next_offset;
        coder->sfbn++;
    }
}
