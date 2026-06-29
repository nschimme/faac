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
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "sbr_analysis.h"
#include "sbr.h"
#include "util.h"
#include <string.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Single-rate (rare) Pass-2 energy accumulation: the 64-band QMF is folded to 32
 * bands by summing adjacent pairs. Out of line so the dual-rate/LC loop in
 * AnalyzeSignal compiles exactly as it did before single-rate existed. */
static void analyze_energy_single_rate(struct SBRInfo *sbr, const faac_real *workspace,
                                       int num_slots, int split, int numEnvelopes,
                                       faac_real bandHalfE[2][SBR_QMF_BANDS_64],
                                       faac_real *sumE, faac_real *sumE2)
{
    for (int slot = 0; slot < num_slots; slot++) {
#if FAAC_SBR_DECIMATION > 1
        if (slot % FAAC_SBR_DECIMATION != 0) continue;
#endif
        faac_real slotEnergy[SBR_QMF_BANDS_64];
        qmf_analysis_64_slot_energy_fft(sbr, workspace + slot * SBR_QMF_BANDS_64, slotEnergy, 0, SBR_QMF_BANDS_64);
        int h = (numEnvelopes > 1 && slot >= split) ? 1 : 0;
        for (int k = 0; k < 32; k++) {
            faac_real energy = slotEnergy[2 * k] + slotEnergy[2 * k + 1];
            bandHalfE[h][k] += energy;
            sumE[k] += energy;
            sumE2[k] += energy * energy;
        }
    }
}

/* AnalyzeSignal: compute transient position, strength, per-band tonality,
 * and accumulate envelope energies over the full-rate signal in ONE pass. */
void AnalyzeSignal(SignalAnalysis *sa, faac_real *fullPtrs[], int nch, int numSamples, struct SBRInfo *sbr)
{
    int num_slots = numSamples / SBR_QMF_BANDS_64;
    int sampled = (num_slots - 1) / FAAC_SBR_DECIMATION + 1;
    faac_real workspace[SBR_QMF_OVL_LEN_64 + 2 * FRAME_LEN];

    sa->valid = 1;
    sa->numSlots = num_slots;
    sa->sampled = sampled;

    /* Pass 1: time-domain transient detection for every channel. The frame grid
     * needs the strongest transient across channels before energies are binned,
     * so detection and QMF energy accumulation are now separate passes. */
    for (int ch = 0; ch < nch; ch++) {
        faac_real smax = (faac_real)0.0, ssum = (faac_real)0.0;
        int smax_idx = 0;
        faac_real slot_hp_eng[128]; /* high-pass energy per slot (max slots = 2*1024/64 = 32) */

        sa->ch[ch].wantShort = 0;
        faac_real val_in = sa->ch[ch].lastVal;
        for (int slot = 0; slot < num_slots; slot++) {
            const faac_real * restrict p_in = fullPtrs[ch] + slot * SBR_QMF_BANDS_64;
            faac_real stot = (faac_real)0.0;
            faac_real hp_stot = (faac_real)0.0;
            for (int n = 0; n < SBR_QMF_BANDS_64; n++) {
                faac_real val = *p_in++;
                stot += val * val;
                faac_real d = val - val_in;
                hp_stot += d * d;
                val_in = val;
            }
            if (slot < 128) slot_hp_eng[slot] = hp_stot;

            if (stot > smax) {
                smax = stot;
                smax_idx = slot;
            }
            ssum += stot;
        }
        sa->ch[ch].lastVal = val_in;

        sa->ch[ch].transientStrength = smax * (faac_real)sampled / (ssum + SBR_ENERGY_FLOOR);
        sa->ch[ch].transientSlot = smax_idx;

        /* Ported PsyCheckShort relative-jump logic onto HP slot energies. */
        faac_real last_hp_eng = 0.0;
        int have_last = 0;
        for (int slot = 0; slot < num_slots; slot++) {
            if (slot >= 128) break;
            faac_real hp_eng = slot_hp_eng[slot];
            if (have_last) {
                faac_real toteng = (hp_eng < last_hp_eng) ? hp_eng : last_hp_eng;
                faac_real volchg = (hp_eng > last_hp_eng) ? (hp_eng - last_hp_eng) : (last_hp_eng - hp_eng);
                /* PSY_TD_THRESH = 0.5 */
                if (volchg > ((faac_real)0.5 * toteng)) {
                    sa->ch[ch].wantShort = 1;
                    break;
                }
            }
            last_hp_eng = hp_eng;
            have_last = 1;
        }
    }

    /* Choose the frame envelope grid from the strongest transient. A 2-envelope
     * frame is signalled as VARFIX with bs_var_bord_0=0 and a single inner border
     * placed at (or just before) the transient; reachable inner borders are the
     * even slots {2,4,6,8} of the 16-slot SBR grid (2*bs_rel_bord+2). split is the
     * matching border in the analysis-slot domain that the energy pass bins at. */
    faac_real frameStrength = (faac_real)0.0;
    int frameSlot = 0;
    for (int ch = 0; ch < nch; ch++) {
        if (sa->ch[ch].transientStrength > frameStrength) {
            frameStrength = sa->ch[ch].transientStrength;
            frameSlot = sa->ch[ch].transientSlot;
        }
    }

    int split = num_slots;   /* default: single envelope spans the whole frame */
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
        split = clamp_int(innerSbr * num_slots / SBR_NUM_TIME_SLOTS, 1, num_slots - 1);
    } else {
        sa->numEnvelopes = 1;
        sa->frameClass = SBR_FRAME_CLASS_FIXFIX;
        sa->tEnv[0] = 0;
        sa->tEnv[1] = SBR_NUM_TIME_SLOTS;
        sa->bsPointer = 0;
    }

    /* Count decimated slots per envelope for energy normalisation. */
    sa->envSampled[0] = sa->envSampled[1] = 0;
    for (int slot = 0; slot < num_slots; slot++) {
#if FAAC_SBR_DECIMATION > 1
        if (slot % FAAC_SBR_DECIMATION != 0) continue;
#endif
        int h = (sa->numEnvelopes > 1 && slot >= split) ? 1 : 0;
        sa->envSampled[h]++;
    }
    if (sa->envSampled[0] < 1) sa->envSampled[0] = 1;
    if (sa->numEnvelopes > 1 && sa->envSampled[1] < 1) sa->envSampled[1] = 1;

    /* Pass 2: accumulate QMF band energy into the two envelope bins and compute
     * per-band tonality. bandHalfE[0] is [0, split), bandHalfE[1] is [split, end). */
    for (int ch = 0; ch < nch; ch++) {
        faac_real sumE[SBR_QMF_BANDS_64], sumE2[SBR_QMF_BANDS_64];
        memset(sumE, 0, sizeof(sumE));
        memset(sumE2, 0, sizeof(sumE2));
        memset(sa->ch[ch].bandHalfE, 0, sizeof(sa->ch[ch].bandHalfE));

        if (sbr) {
            memcpy(workspace, sbr->ch[ch].qmfOvl64, SBR_QMF_OVL_LEN_64 * sizeof(faac_real));
            memcpy(workspace + SBR_QMF_OVL_LEN_64, fullPtrs[ch], numSamples * sizeof(faac_real));

            if (sbr->singleRate) {
                analyze_energy_single_rate(sbr, workspace, num_slots, split, sa->numEnvelopes,
                                           sa->ch[ch].bandHalfE, sumE, sumE2);
            } else
            for (int slot = 0; slot < num_slots; slot++) {
#if FAAC_SBR_DECIMATION > 1
                if (slot % FAAC_SBR_DECIMATION == 0)
#endif
                {
                    faac_real slotEnergy[SBR_QMF_BANDS_64];
                    qmf_analysis_64_slot_energy_fft(sbr, workspace + slot * SBR_QMF_BANDS_64, slotEnergy, 0, SBR_QMF_BANDS_64);

                    int h = (sa->numEnvelopes > 1 && slot >= split) ? 1 : 0;

                    for (int k = 0; k < SBR_QMF_BANDS_64; k++) {
                        faac_real energy = slotEnergy[k];
                        sa->ch[ch].bandHalfE[h][k] += energy;
                        sumE[k] += energy;
                        sumE2[k] += energy * energy;
                    }
                }
            }
        }

        /* Phase 4: Tonality = original-vs-transposed band-energy ratio.
         * SBR reconstructs highband k from lowband (k - kx).
         * Tonality[k] = min(1.0, E_orig[k] / (E_orig[k-kx] + floor)).
         * High tonality (-> 1.0) means the HF content matches the LF patch in energy;
         * low tonality means the HF is much quieter/noisier than the patch. */
        for (int k = 0; k < SBR_QMF_BANDS_64; k++)
            sa->ch[ch].bandTonality[k] = (faac_real)0.0;

        if (sbr) {
            int kx = sbr->kx;
            int qmf_bands = sbr->singleRate ? 32 : SBR_QMF_BANDS_64;
            for (int k = kx; k < qmf_bands; k++) {
                faac_real e_hf = sumE[k];
                faac_real e_lf = sumE[k - kx];
                faac_real ratio = e_hf / (e_lf + SBR_ENERGY_FLOOR);
                sa->ch[ch].bandTonality[k] = (ratio > (faac_real)1.0) ? (faac_real)1.0 : ratio;
            }
        }
    }
}
