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

/* Three-envelope transient grid bracketing even slot Tb in [SBR_BORDER_MIN,
 * SBR_BORDER_MAX]:
 *   Tb <= 8:  VARFIX, tEnv = {Tb-2, Tb, Tb+2, 16},  bsPointer = e_t + 1
 *   Tb >  8:  FIXVAR, tEnv = {0, Tb-2, Tb, Tb+2},   bsPointer = numEnvelopes+1-e_t
 * In both, envelope e_t spans [Tb, Tb+2) and bsPointer is how the decoder
 * names it back. */
static void sbr_grid_transient(SignalAnalysis *sa, int Tb)
{
    int e_t;    /* index of the envelope holding the transient */

    sa->numEnvelopes = 3;
    sa->tEnv[0] = 0;
    sa->tEnv[3] = SBR_NUM_TIME_SLOTS;

    if (Tb <= 8) {
        /* Decoder: e_a = ptr - 1 (§4.6.18.3.6) => ptr = e_t + 1. */
        sa->frameClass = SBR_FRAME_CLASS_VARFIX;
        sa->tEnv[1] = Tb;
        sa->tEnv[2] = Tb + 2;
        e_t = 1;
        sa->bsPointer = e_t + 1;
    } else {
        /* Decoder: e_a = numEnvelopes + 1 - ptr (§4.6.18.3.6). At the last
         * legal border there's no room for a trailing envelope, so e_t
         * becomes numEnvelopes instead of 1. */
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

/* Which envelope a QMF slot falls in; slots before tEnv[0] fold into
 * envelope 0 rather than dropping their energy. */
static inline int sbr_env_of_slot(int numEnvelopes, const int *envStart, int slot)
{
    int e = 0;
    while (e + 1 < numEnvelopes && slot >= envStart[e + 1]) e++;
    return e;
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
        /* Attack detection: rise over a running average (carried across
         * frames) fires on the onset, not just the loudest slot. */
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

            /* Cold start: seed from the signal instead of firing on a rise over zero. */
            if (ema <= 0.0f) {
                ema = stot;
            } else if (stot > bestRise * ema) {
                /* Multiply first to keep the divide off the common (non-rising) path. */
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
        /* Shift the pass-1 attack slot onto the QMF's view of it (SBR_QMF_DELAY_SLOTS). */
        int Ts = (num_slots > 0)
                 ? (frameSlot + SBR_QMF_DELAY_SLOTS) * SBR_NUM_TIME_SLOTS / num_slots : 0;
        /* Round down onto the even grid so the attack sits inside the transient
         * envelope, not the tail of the one before it. */
        int Tb = clamp_int(Ts & ~1, SBR_BORDER_MIN, SBR_BORDER_MAX);
        sbr_grid_transient(sa, Tb);
    } else {
        /* Steady frame: FIXFIX with the rate's envelope count; uniform borders,
         * so only the count is transmitted. */
        int nEnv = sbr ? sbr->numEnvNonTransient : 1;
        if (nEnv < 1) nEnv = 1;
        while (nEnv > 1 && nEnv > num_slots) nEnv >>= 1;
        sa->numEnvelopes = nEnv;
        sa->frameClass = SBR_FRAME_CLASS_FIXFIX;
        for (int e = 0; e <= nEnv; e++)
            sa->tEnv[e] = e * SBR_NUM_TIME_SLOTS / nEnv;
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

    /* Pass 2: subband analysis, accumulating QMF band energy per envelope.
     * Only [kx, k2) feeds the quantizer, so skip bands below kx; only the SBR
     * element's own channels are quantized, so a 5.1 core doesn't pay for
     * four channels of QMF analysis whose result is dropped. */
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
