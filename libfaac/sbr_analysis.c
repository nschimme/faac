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
 */

#include "sbr.h"
#include "sbr_analysis.h"
#include "sbr_internal.h"
#include "util.h"
#include <string.h>

/* Envelope border in QMF slots, for binning the per-slot energies. The first
 * envelope also takes slots before tEnv[0] and the last the ones after its
 * end, so no slot's energy is dropped. Computed on demand: an array of them
 * is vectorized into more code than the whole analysis it serves. */
static inline int env_border(const SignalAnalysis *sa, int e, int num_slots)
{
    if (e == 0) return 0;
    if (e == sa->numEnvelopes) return num_slots;
    return sa->tEnv[e] * num_slots / SBR_NUM_TIME_SLOTS;
}

/* Multi-pass signal analysis: transient detection, temporal grid selection,
 * and subband energy accumulation. */
void SbrAnalyze(SignalAnalysis *sa, float *fullPtrs[], int nch, const bool *isLfe, int numSamples, struct SBRInfo *sbr)
{
    int num_slots = numSamples / SBR_QMF_BANDS_64;
    float workspace[SBR_QMF_HIST_LEN + 2 * FRAME_LEN];

    float frameStrength = 0.0f;

    /* The strongest non-LFE transient selects the shared envelope grid. */
    for (int ch = 0; ch < nch; ch++) {
        if (isLfe[ch]) continue;
        float smax = 0.0f, ssum = 0.0f;
        for (int slot = 0; slot < num_slots; slot++) {
            /* The analysed frame starts SBR_ANALYSIS_DELAY back, in the saved history. */
            int pos = slot * SBR_QMF_BANDS_64 - SBR_ANALYSIS_DELAY;
            const float * restrict p_in = (pos < 0) ? sbr->ch[ch].qmfOvl64 + SBR_QMF_HIST_LEN + pos
                                                    : fullPtrs[ch] + pos;
            float stot = 0.0f;
            for (int n = 0; n < SBR_QMF_BANDS_64; n += 4) {
                float v0 = p_in[0], v1 = p_in[1], v2 = p_in[2], v3 = p_in[3];
                stot += v0 * v0 + v1 * v1 + v2 * v2 + v3 * v3;
                p_in += 4;
            }

            if (stot > smax) {
                smax = stot;
            }
            ssum += stot;
        }

        float strength = smax * (float)num_slots / (ssum + SBR_ENERGY_FLOOR);
        if (strength > frameStrength) {
            frameStrength = strength;
        }
    }

    if (frameStrength > (sbr->rich ? SBR_TRANSIENT_THRESH_RICH : SBR_TRANSIENT_THRESH_DEFAULT)) {
        const int innerSbr = SBR_NUM_TIME_SLOTS / 2;
        sa->numEnvelopes = 2;
        sa->frameClass = SBR_FRAME_CLASS_VARFIX;
        sa->tEnv[0] = 0;
        sa->tEnv[1] = innerSbr;
        sa->tEnv[2] = SBR_NUM_TIME_SLOTS;
    } else {
        int ne = sbr->numEnvFixFix;
        sa->numEnvelopes = ne;
        sa->frameClass = SBR_FRAME_CLASS_FIXFIX;
        for (int e = 0; e <= ne; e++)
            sa->tEnv[e] = e * SBR_NUM_TIME_SLOTS / ne;
    }

    /* Slots analysed per envelope, for power normalization: the multiples of
     * the decimation factor below each border. */
    for (int e = 0; e < sa->numEnvelopes; e++) {
        int n = (env_border(sa, e + 1, num_slots) + FAAC_SBR_DECIMATION - 1) / FAAC_SBR_DECIMATION
              - (env_border(sa, e, num_slots) + FAAC_SBR_DECIMATION - 1) / FAAC_SBR_DECIMATION;
        sa->envSampled[e] = n < 1 ? 1 : n;
    }

    /* Pass 2: subband analysis, accumulating QMF band energy per envelope.
     * Only [kx, k2) feeds the quantizer, so skip bands below kx. */
    if (sbr) {
        int kx = sbr->kx;
        int kEnd = sbr->k2;
        for (int ch = 0; ch < nch; ch++) {
            if (isLfe[ch]) continue;
            memset(sa->ch[ch].bandE, 0, (size_t)sa->numEnvelopes * sizeof(sa->ch[ch].bandE[0]));

            memcpy(workspace, sbr->ch[ch].qmfOvl64, SBR_QMF_HIST_LEN * sizeof(float));
            memcpy(workspace + SBR_QMF_HIST_LEN, fullPtrs[ch], numSamples * sizeof(float));

            for (int e = 0; e < sa->numEnvelopes; e++) {
                int end = env_border(sa, e + 1, num_slots);
                for (int slot = env_border(sa, e, num_slots); slot < end; slot++) {
#if FAAC_SBR_DECIMATION > 1
                    if (slot % FAAC_SBR_DECIMATION != 0) continue;
#endif
                    SbrQmfAnalysis(sbr, workspace + slot * SBR_QMF_BANDS_64, sa->ch[ch].bandE[e], kx, kEnd);
                }
            }
        }
    }
}
