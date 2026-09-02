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
#include <math.h>

/* Legal range for a transient's leading border in time-slots. ISO 14496-3 §4.6.18
 * requires bs_var_bord_0 in [0, 2], so the leading border is at slot 0, 2, or 4.
 * The attack slot Tb must fall inside [SBR_BORDER_MIN, SBR_BORDER_MAX]. */
#define SBR_BORDER_MIN 2
#define SBR_BORDER_MAX 12

/* Form a three-envelope transient grid bracketing time slot Tb (an even slot in
 * [SBR_BORDER_MIN, SBR_BORDER_MAX]).
 *
 * If Tb is near the start of the frame (Tb <= 4) the grid uses VARFIX:
 *   tEnv = { Tb - 2, Tb, Tb + 2, 16 }, bsPointer = e_t + 1
 * so envelope e_t = 1 spans [Tb, Tb + 2) and bs_pointer names e_t as the attack.
 *
 * If Tb is near the tail (Tb >= 6) the grid uses FIXVAR:
 *   tEnv = { 0, Tb - 2, Tb, Tb + 2 }, bsPointer = 4 - e_t
 * which the decoder rebuilds by walking backwards from the trailing border
 * (16 + bs_var_bord_1 = Tb + 2), placing envelope e_t at [Tb, Tb + 2) and naming
 * it via bs_pointer = numEnvelopes + 1 - e_t. */
static void sbr_grid_transient(SignalAnalysis *sa, int Tb)
{
    int e_t;    /* index of the envelope holding the transient */

    sa->numEnvelopes = 3;
    sa->tEnv[0] = 0;
    sa->tEnv[3] = SBR_NUM_TIME_SLOTS;

    if (Tb <= 8) {
        /* Decoder: t_env[i+1] = t_env[i] + 2*bs_rel_bord + 2, e_t = 1 */
        sa->frameClass = SBR_FRAME_CLASS_VARFIX;
        sa->tEnv[1] = Tb;
        sa->tEnv[2] = Tb + 2;
        e_t = 1;
        sa->bsPointer = e_t + 1;
    } else {
        /* Decoder: t_env[n-1-i] = t_env[n-i] - 2*bs_rel_bord - 2, walking back */
        sa->frameClass = SBR_FRAME_CLASS_FIXVAR;
        if (Tb <= SBR_BORDER_MAX - 2) {
            sa->tEnv[1] = Tb;
            sa->tEnv[2] = Tb + 2;
            e_t = 1;
        } else {
            sa->tEnv[1] = Tb - 2;
            sa->tEnv[2] = Tb;
            e_t = 2;
        }
        sa->bsPointer = sa->numEnvelopes + 1 - e_t;
    }
}

/* Which envelope a QMF slot falls in. Slots ahead of tEnv[0] belong to the
 * previous frame's trailing envelope; fold them into envelope 0 rather than
 * drop their energy. */
static inline int sbr_env_of_slot(int numEnvelopes, const int *envStart, int slot)
{
    return (numEnvelopes > 1 && slot >= envStart[1]) + (numEnvelopes > 2 && slot >= envStart[2]);
}

/* Multi-pass signal analysis: transient detection, temporal grid selection,
 * and subband energy accumulation. */
void SbrAnalyze(SignalAnalysis *sa, float *fullPtrs[], int nch, int numSamples, struct SBRInfo *sbr)
{
    int num_slots = numSamples / SBR_QMF_BANDS_64;
    int sampled = (num_slots - 1) / FAAC_SBR_DECIMATION + 1;
    float workspace[SBR_QMF_OVL_LEN_64 + 2 * FRAME_LEN];

    sa->valid = 1;
    sa->numSlots = num_slots;
    sa->sampled = sampled;

    /* Pass 1: Time-domain transient detection. Identifies the temporal position
     * and strength of transients across all channels. */
    for (int ch = 0; ch < nch; ch++) {
        /* Attack detection: score each slot against an exponential average of
         * the slots before it, carried across frame boundaries. A rise over the
         * recent past fires on a step onset -- where a frame-wide peak-to-mean
         * ratio sees no dominant slot and stays silent -- and lands on the onset
         * rather than the loudest slot, which is where the border belongs. */
        float ema = sa->ch[ch].slotEma;
        float bestRise = 0.0f;
        int bestSlot = 0;

        sa->ch[ch].wantShort = 0;
        float val_in = sa->ch[ch].lastVal;
        float last_hp_eng = 0.0f;
        const float * restrict p_in = fullPtrs[ch];

        for (int slot = 0; slot < num_slots; slot++) {
            float stot = 0.0f;
            float hp_stot = 0.0f;
            for (int n = 0; n < SBR_QMF_BANDS_64; n += 4) {
                float v0 = p_in[0], v1 = p_in[1], v2 = p_in[2], v3 = p_in[3];
                stot += v0 * v0 + v1 * v1 + v2 * v2 + v3 * v3;
                float d0 = v0 - val_in, d1 = v1 - v0, d2 = v2 - v1, d3 = v3 - v2;
                hp_stot += d0 * d0 + d1 * d1 + d2 * d2 + d3 * d3;
                val_in = v3; p_in += 4;
            }

            if (slot > 0 && !sa->ch[ch].wantShort) {
                float toteng = (hp_stot < last_hp_eng) ? hp_stot : last_hp_eng;
                float volchg = fabsf(hp_stot - last_hp_eng);
                if (volchg > 0.5f * toteng)
                    sa->ch[ch].wantShort = 1;
            }
            last_hp_eng = hp_stot;

            /* Cold start (and the first frame after a reset): seed the average
             * from the signal rather than let a rise over zero fire. */
            if (ema <= 0.0f) {
                ema = stot;
            } else if (stot > bestRise * ema) {
                /* Guarded to keep the divide off the common path: a slot only
                 * beats the incumbent if stot/ema > bestRise, and steady or
                 * decaying material (silence above all) fails that on the
                 * multiply alone. */
                float rise = stot / (ema + SBR_ENERGY_FLOOR);
                if (rise > bestRise) {
                    bestRise = rise;
                    bestSlot = slot;
                }
            }
            ema += SBR_ATTACK_EMA_ALPHA * (stot - ema);
        }
        sa->ch[ch].lastVal = val_in;
        sa->ch[ch].slotEma = ema;

        sa->ch[ch].transientStrength = bestRise;
        sa->ch[ch].transientSlot = bestSlot;
    }

    /* Choose the temporal grid based on the strongest transient. Synchronizes
     * envelope borders across all channels to maintain spatial imaging. */
    float frameStrength = 0.0f;
    int frameSlot = 0;
    for (int ch = 0; ch < nch; ch++) {
        if (sa->ch[ch].transientStrength > frameStrength) {
            frameStrength = sa->ch[ch].transientStrength;
            frameSlot = sa->ch[ch].transientSlot;
        }
    }

    if (frameStrength > SBR_TRANSIENT_THRESH_DEFAULT) {
        /* Convert the pass-1 attack slot into a transmitted time slot, shifting
         * it onto the QMF's view of the same event (see SBR_QMF_DELAY_SLOTS). */
        int Ts = (num_slots > 0)
                 ? (frameSlot + SBR_QMF_DELAY_SLOTS) * SBR_NUM_TIME_SLOTS / num_slots : 0;
        /* Round the border down onto the even grid: the attack then sits inside
         * the transient envelope rather than at the tail of the one before. */
        int Tb = clamp_int(Ts & ~1, SBR_BORDER_MIN, SBR_BORDER_MAX);
        sbr_grid_transient(sa, Tb);
    } else {
        sa->numEnvelopes = 1;
        sa->frameClass = SBR_FRAME_CLASS_FIXFIX;
        sa->tEnv[0] = 0;
        sa->tEnv[1] = SBR_NUM_TIME_SLOTS;
        sa->bsPointer = 0;
    }

    /* Envelope borders in QMF slots, for binning the per-slot energies below. */
    int envStart[SBR_MAX_ENVELOPES + 1];
    for (int e = 0; e <= sa->numEnvelopes; e++)
        envStart[e] = sa->tEnv[e] * num_slots / SBR_NUM_TIME_SLOTS;

    /* Count slots per envelope for power normalization. */
    for (int e = 0; e < sa->numEnvelopes; e++) sa->envSampled[e] = 0;
    for (int slot = 0; slot < num_slots; slot++) {
#if FAAC_SBR_DECIMATION > 1
        if (slot % FAAC_SBR_DECIMATION != 0) continue;
#endif
        sa->envSampled[sbr_env_of_slot(sa->numEnvelopes, envStart, slot)]++;
    }
    for (int e = 0; e < sa->numEnvelopes; e++)
        if (sa->envSampled[e] < 1) sa->envSampled[e] = 1;

    /* Pass 2: Subband analysis. Accumulates energy across QMF bands within
     * the selected temporal envelopes. */
    /* Only [kx, k2) feeds the envelope quantizer; bands below kx are core-coded
     * and never read, so skip their post-FFT extraction and accumulation. */
    /* Only the SBR element's own channels are quantized, so a 5.1 core would
     * otherwise pay for four channels of QMF analysis whose result is dropped. */
    int kx = sbr ? sbr->kx : 0;
    int kEnd = sbr ? sbr->k2 : SBR_QMF_BANDS_64;
    int nch_coded = (nch < SBR_MAX_CODED_CHANNELS) ? nch : SBR_MAX_CODED_CHANNELS;
    for (int ch = 0; ch < nch_coded; ch++) {
        memset(sa->bandE[ch], 0, sizeof(sa->bandE[ch]));

        if (sbr) {
            memcpy(workspace, sbr->ch[ch].qmfOvl64, SBR_QMF_OVL_LEN_64 * sizeof(float));
            memcpy(workspace + SBR_QMF_OVL_LEN_64, fullPtrs[ch], numSamples * sizeof(float));

            for (int slot = 0; slot < num_slots; slot++) {
#if FAAC_SBR_DECIMATION > 1
                if (slot % FAAC_SBR_DECIMATION == 0)
#endif
                {
                    float slotEnergy[SBR_QMF_BANDS_64];
                    SbrQmfAnalysis(sbr, workspace + slot * SBR_QMF_BANDS_64 + SBR_QMF_READ_OFFSET,
                                   slotEnergy, kx, kEnd);

                    int e = sbr_env_of_slot(sa->numEnvelopes, envStart, slot);

                    float * restrict bE = sa->bandE[ch][e];
                    for (int k = kx; k < kEnd; k++)
                        bE[k] += slotEnergy[k];
                }
            }
        }
    }
}
