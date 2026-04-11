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

void ApplyPseudoSBR(CoderInfo *coder, faac_real *freq, int sampleRate, unsigned long bitRatePerChannel)
{
    int core_bins;
    int sbr_bins;
    float expansion_fraction;
    int i;
    int patch_size;
    int dest_idx;
    float sfm;

    if (coder->block_type != ONLY_LONG_WINDOW)
        return;

    core_bins = coder->sfb_offset[coder->sfbn];
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

    /* Tonality Gating: Check core tonality to prevent metallic artifacts */
    sfm = CalcSFM(freq, core_bins / 2, core_bins);

    /* Spectral Folding (Translation): Copy low-frequency tile to high-frequency region.
       Inspiration from HE-AAC and Opus CELT. Translation preserves harmonic structure
       much better than mirroring for speech.
    */
    patch_size = sbr_bins;
    int patch_offset = core_bins / 2;
    if (patch_offset + patch_size > core_bins) {
        patch_offset = core_bins - patch_size;
    }
    if (patch_offset < 0) patch_offset = 0;

    for (i = 0; i < patch_size; i++) {
        dest_idx = core_bins + i;
        if (dest_idx >= FRAME_LEN) break;

        /* Translation logic: freq[f] = freq[f - offset] */
        freq[dest_idx] = freq[patch_offset + i] * SBR_GAIN_ROLLOFF;

        /* Tonality Gating: attenuate tonal components to prevent metallic ringing */
        if (sfm < SBR_TONAL_THRESH) {
            freq[dest_idx] *= SBR_TONAL_ATTEN;
        }
    }

    /* 8-bin cross-fade at the transition for smoother spectral joining */
    for (i = 0; i < 8 && (core_bins + i < FRAME_LEN); i++) {
        float fade = (float)(i + 1) / 9.0f;
        freq[core_bins + i] *= fade;
    }

    /* Stealth Hole Filling: Inject noise floor into zeroed core bins to maintain texture */
    for (i = 0; i < core_bins; i++) {
        if (FAAC_FABS(freq[i]) < 1e-9f) {
            freq[i] = (i % 2 ? 1.0f : -1.0f) * SBR_HOLE_NOISE;
        }
    }

    /* Tonality-aware scaling for SBR: preserve texture by not over-attenuating
       if the core is noisy (high SFM).
    */

    /* Update sfb_offset to include SBR bins.
       We extend the core by adding one more scalefactor band if possible.
       Safety check: sfbn must not exceed NSFB_LONG or MAX_SCFAC_BANDS.
    */
    if (coder->sfbn < NSFB_LONG && coder->sfbn < MAX_SCFAC_BANDS) {
        coder->sfb_offset[coder->sfbn + 1] = core_bins + sbr_bins;
        coder->sfbn++;
    }
}
