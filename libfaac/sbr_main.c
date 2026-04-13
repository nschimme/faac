/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Jules
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
 *
 */

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "sbr_main.h"
#include "coder.h"
#include "util.h"

sbr_info_t *sbr_encode_init(int num_channels, unsigned long sample_rate)
{
    sbr_info_t *sbr = (sbr_info_t *)AllocMemory(sizeof(sbr_info_t));
    if (sbr) {
        memset(sbr, 0, sizeof(sbr_info_t));
        sbr->num_channels = num_channels;
        sbr->sample_rate = sample_rate;
        sbr_qmf_analysis_init(&sbr->qmf[0]);
        if (num_channels > 1) {
            sbr_qmf_analysis_init(&sbr->qmf[1]);
        }
    }
    return sbr;
}

void sbr_encode_close(sbr_info_t *sbr)
{
    if (sbr) {
        FreeMemory(sbr);
    }
}

void sbr_encode_frame(sbr_info_t *sbr, faac_real *input[], int block_type[])
{
    int ch, l, k, env;
    faac_real X[32][64][2];
    faac_real X_transposed[32][64][2];

    sbr->frame_count++;

    /* Header every ~500ms */
    sbr->bs_data.sbr_header_present = (sbr->frame_count % 22 == 0);

    for (ch = 0; ch < sbr->num_channels; ch++) {
        /* 1. QMF Analysis */
        sbr_qmf_analysis(&sbr->qmf[ch], input[ch], X);

        /* 2. Window Synchronization & Grid Selection (Phase 3.1) */
        int sbr_grid = (block_type[ch] == ONLY_SHORT_WINDOW) ? 4 : 1;
        sbr->bs_data.sbr_grid = sbr_grid;
        sbr->bs_data.num_env = sbr_grid;

        /* 3. Spectral Transposition (Blind Shift) (Phase 3.2) */
        memset(X_transposed, 0, sizeof(X_transposed));
        /* SBR processes 32 subband slots for 2048 input samples */
        for (l = 0; l < 32; l++) {
            for (k = 32; k < 64; k++) {
                int source_k = k - 24; /* Simple blind shift from lower bands */

                X_transposed[l][k][0] = X[l][source_k][0];
                X_transposed[l][k][1] = X[l][source_k][1];

                /* Sign-flipping for aliasing mitigation on odd-numbered patches */
                if ((k - source_k) % 2 != 0) {
                    X_transposed[l][k][0] = -X_transposed[l][k][0];
                    X_transposed[l][k][1] = -X_transposed[l][k][1];
                }
            }
        }

        /* 4. Envelope Scaling (RMS Only) (Phase 3.3) */
        /* Use 16 bands for SBR to stay within bit budget */
        int num_bands = 16;
        sbr->bs_data.num_bands = num_bands;
        int env_len = 32 / sbr_grid;
        int band_width = 32 / num_bands;

        for (env = 0; env < sbr_grid; env++) {
            int l_start = env * env_len;
            int l_end = (env + 1) * env_len;
            for (k = 0; k < num_bands; k++) {
                faac_real p_orig = 0;
                faac_real p_trans = 0;
                int start_k = 32 + k * band_width;

                for (l = l_start; l < l_end; l++) {
                    int b;
                    for (b = 0; b < band_width; b++) {
                        p_orig += X[l][start_k + b][0] * X[l][start_k + b][0] + X[l][start_k + b][1] * X[l][start_k + b][1];
                        p_trans += X_transposed[l][start_k + b][0] * X_transposed[l][start_k + b][0] + X_transposed[l][start_k + b][1] * X_transposed[l][start_k + b][1];
                    }
                }

                faac_real scale = 0;
                if (p_trans > 0) {
                    scale = sqrt(p_orig / p_trans);
                }

                /* Quantize to 1.5dB steps: 20 * log10(scale) / 1.5 */
                /* Scale values are often around 1.0. log10(1.0) = 0. */
                int val = (int)(20.0f * log10(scale + 1e-6f) / 1.5f + 24); /* Standard HE-AAC env offset is ~24-32 */
                if (val < 0) val = 0;
                if (val > 63) val = 63;
                sbr->bs_data.env_data[ch][env][k] = val;
            }
        }
    }
}
