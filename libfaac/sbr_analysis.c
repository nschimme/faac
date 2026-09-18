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
void SbrAnalyze(SignalAnalysis *sa, float *fullPtrs[], int nch, const bool *isLfe, int numSamples, struct SBRInfo *sbr)
{
    int num_slots = numSamples / SBR_QMF_BANDS_64;
    int sampled = (num_slots - 1) / FAAC_SBR_DECIMATION + 1;
    float workspace[SBR_QMF_OVL_LEN_64 + 2 * FRAME_LEN];

    sa->valid = 1;
    sa->numSlots = num_slots;
    sa->sampled = sampled;

    /* Pass 1: Downsampled time-domain transient detection (2x stride).
     * Identifies the temporal position and strength of transients across
     * all channels while cutting transient-path MAC operations by 50%.
     * Preserves exact discrete QMF time-slot boundary alignment. */
    for (int ch = 0; ch < nch; ch++) {
        float smax = 0.0f, ssum = 0.0f;
        int smax_idx = 0;
        float slot_hp_eng[128]; /* high-pass energy per slot (max slots = 2*1024/64 = 32) */

        sa->ch[ch].wantShort = 0;
        float val_in = sa->ch[ch].lastVal;
        const float * restrict p_in = fullPtrs[ch];
        for (int slot = 0; slot < num_slots; slot++) {
            float stot = 0.0f;
            float hp_stot = 0.0f;
            for (int n = 0; n < SBR_QMF_BANDS_64; n += 4) {
                float v0 = p_in[0], v2 = p_in[2];
                stot += v0 * v0 + v2 * v2;
                float d0 = v0 - val_in, d1 = v2 - v0;
                hp_stot += d0 * d0 + d1 * d1;
                val_in = p_in[3];
                p_in += 4;
            }
            /* Scale energy estimations by 2.0f to match 1x energy baseline. */
            stot *= 2.0f;
            hp_stot *= 2.0f;

            if (slot < 128) slot_hp_eng[slot] = hp_stot;

            if (stot > smax) {
                smax = stot;
                smax_idx = slot;
            }
            ssum += stot;
        }
        sa->ch[ch].lastVal = val_in;

        sa->ch[ch].transientStrength = smax * (float)num_slots / (ssum + SBR_ENERGY_FLOOR);

        /* Pass 1b: Spectral Flatness / Tonality Estimation for Dynamic Inverse Filtering.
         * High source tonality + low high-band energy -> INVF_HIGH to remove tonal chirping.
         * High source tonality + preserved high-band harmonic structure -> INVF_OFF/LOW.
         * Low source tonality (noise/transients) -> INVF_LOW/MID. */
        float avg_slot_eng = ssum / (float)(num_slots + 1e-6f);
        if (sa->ch[ch].transientStrength > 5.0f) {
            sa->invfMode[ch] = 3; /* INVF_HIGH on strong transients to prevent chirping */
        } else if (avg_slot_eng < 1e-8f) {
            sa->invfMode[ch] = 0; /* INVF_OFF on silence */
        } else {
            float peak_ratio = smax / (avg_slot_eng + SBR_ENERGY_FLOOR);
            if (peak_ratio > 3.0f) {
                sa->invfMode[ch] = 2; /* INVF_MID */
            } else if (peak_ratio > 1.8f) {
                sa->invfMode[ch] = 1; /* INVF_LOW */
            } else {
                sa->invfMode[ch] = 0; /* INVF_OFF */
            }
        }
        sa->ch[ch].transientSlot = smax_idx;

        /* Evaluate relative energy jumps to inform block switching. */
        float last_hp_eng = 0.0f;
        int have_last = 0;
        for (int slot = 0; slot < num_slots; slot++) {
            if (slot >= 128) break;
            float hp_eng = slot_hp_eng[slot];
            if (have_last) {
                float toteng = (hp_eng < last_hp_eng) ? hp_eng : last_hp_eng;
                float volchg = (hp_eng > last_hp_eng) ? (hp_eng - last_hp_eng) : (last_hp_eng - hp_eng);
                /* PSY_TD_THRESH = 0.5 */
                if (volchg > (0.5f * toteng)) {
                    sa->ch[ch].wantShort = 1;
                    break;
                }
            }
            last_hp_eng = hp_eng;
            have_last = 1;
        }
    }

    /* Choose the temporal grid based on the strongest transient. Synchronizes
     * envelope borders across all channels to maintain spatial imaging. The
     * LFE carries no SBR, so it gets no vote. */
    float frameStrength = 0.0f;
    int frameSlot = 0;
    for (int ch = 0; ch < nch; ch++) {
        if (isLfe[ch]) continue;
        if (sa->ch[ch].transientStrength > frameStrength) {
            frameStrength = sa->ch[ch].transientStrength;
            frameSlot = sa->ch[ch].transientSlot;
        }
    }

    if (frameStrength > SBR_TRANSIENT_THRESH_DEFAULT) {
        int Ts = (num_slots > 0) ? frameSlot * SBR_NUM_TIME_SLOTS / num_slots : 0; /* 0..16 */
        int rel = clamp_int((Ts - 2) / 2, 0, 3);
        int innerSbr = 2 * rel + 2;                  /* {2,4,6,8} */
        sa->numEnvelopes = 2;
        sa->frameClass = SBR_FRAME_CLASS_VARFIX;
        sa->tEnv[0] = 0;
        sa->tEnv[1] = innerSbr;
        sa->tEnv[2] = SBR_NUM_TIME_SLOTS;
        sa->bsPointer = 0;
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

    /* Pass 2: subband analysis, accumulating QMF band energy per envelope.
     * Only [kx, k2) feeds the quantizer, so skip bands below kx. */
    if (sbr) {
        int kx = sbr->kx;
        int kEnd = sbr->k2;
        for (int ch = 0; ch < nch; ch++) {
            if (isLfe[ch]) continue;
            memset(sa->bandE[ch], 0, sizeof(sa->bandE[ch]));
            memset(sa->addHarmonic[ch], 0, sizeof(sa->addHarmonic[ch]));
            sa->addHarmonicFlag[ch] = 0;

            memcpy(workspace, sbr->ch[ch].qmfOvl64, SBR_QMF_OVL_LEN_64 * sizeof(float));
            memcpy(workspace + SBR_QMF_OVL_LEN_64, fullPtrs[ch], numSamples * sizeof(float));

            /* Track max peak energy per band to detect isolated strong sinusoids */
            float maxBandSlotE[SBR_QMF_BANDS_64];
            memset(maxBandSlotE, 0, sizeof(maxBandSlotE));

            for (int slot = 0; slot < num_slots; slot++) {
#if FAAC_SBR_DECIMATION > 1
                if (slot % FAAC_SBR_DECIMATION == 0)
#endif
                {
                    float slotEnergy[SBR_QMF_BANDS_64];
                    SbrQmfAnalysis(sbr, workspace + slot * SBR_QMF_BANDS_64, slotEnergy, kx, kEnd);

                    int e = sbr_env_of_slot(sa->numEnvelopes, envStart, slot);

                    float * restrict bE = sa->bandE[ch][e];
                    for (int k = kx; k < kEnd; k++) {
                        bE[k] += slotEnergy[k];
                        if (slotEnergy[k] > maxBandSlotE[k])
                            maxBandSlotE[k] = slotEnergy[k];
                    }
                }
            }

            /* Estimate noise floor level per envelope and missing harmonics */
            for (int e = 0; e < sa->numEnvelopes; e++) {
                int e_slots = sa->envSampled[e];
                if (e_slots < 1) e_slots = 1;
                float tot_high_e = 0.0f;
                int n_high_bands = kEnd - kx;
                for (int k = kx; k < kEnd; k++) {
                    tot_high_e += sa->bandE[ch][e][k];
                }
                float avg_high_e = tot_high_e / (float)(e_slots * (n_high_bands > 0 ? n_high_bands : 1) + 1e-6f);
                /* Map noise floor level dynamically: higher avg energy -> noise level lower (less synthetic noise),
                 * lower avg energy -> noise level higher (synthetic noise floor injected). Default baseline is 12. */
                int noise_lvl = 12;
                if (avg_high_e > 1e-4f) noise_lvl = 6;
                else if (avg_high_e > 1e-6f) noise_lvl = 10;
                else if (avg_high_e < 1e-10f) noise_lvl = 16;
                sa->noiseFloor[ch][e] = clamp_int(noise_lvl, 0, 31);
            }

            /* Detect isolated strong sinusoids in target high-frequency bands for bs_add_harmonic */
            for (int b = 0; b < sbr->numBands; b++) {
                int k_lo = sbr->bandEdges[b];
                int k_hi = sbr->bandEdges[b+1];
                for (int k = k_lo; k < k_hi; k++) {
                    float total_e = sa->bandE[ch][0][k] + (sa->numEnvelopes > 1 ? sa->bandE[ch][1][k] : 0.0f);
                    float avg_e = total_e / (float)num_slots;
                    if (maxBandSlotE[k] > 8.0f * (avg_e + SBR_ENERGY_FLOOR) && maxBandSlotE[k] > 1e-4f) {
                        sa->addHarmonic[ch][b] = 1;
                        sa->addHarmonicFlag[ch] = 1;
                        break;
                    }
                }
            }
        }
    }
}
