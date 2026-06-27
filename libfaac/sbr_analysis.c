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

    for (int ch = 0; ch < nch; ch++) {
        faac_real smax = (faac_real)0.0, ssum = (faac_real)0.0;
        int smax_idx = 0;
        faac_real slot_hp_eng[128]; /* high-pass energy per slot (max slots = 2*1024/64 = 32) */

        /* First pass: Time-domain energy to find transient position AND high-pass energy. */
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

        /* Prepare for single QMF pass and tonality/energy accumulation. */
        faac_real sumE[SBR_QMF_BANDS_64], sumE2[SBR_QMF_BANDS_64];
        memset(sumE, 0, sizeof(sumE));
        memset(sumE2, 0, sizeof(sumE2));
        memset(sa->ch[ch].bandHalfE, 0, sizeof(sa->ch[ch].bandHalfE));

        if (sbr) {
            memcpy(workspace, sbr->ch[ch].qmfOvl64, SBR_QMF_OVL_LEN_64 * sizeof(faac_real));
            memcpy(workspace + SBR_QMF_OVL_LEN_64, fullPtrs[ch], numSamples * sizeof(faac_real));

            for (int slot = 0; slot < num_slots; slot++) {
#if FAAC_SBR_DECIMATION > 1
                if (slot % FAAC_SBR_DECIMATION == 0)
#endif
                {
                    faac_real slotEnergy[SBR_QMF_BANDS_64];
                    qmf_analysis_64_slot_energy_fft(sbr, workspace + slot * SBR_QMF_BANDS_64, slotEnergy, 0, SBR_QMF_BANDS_64);

                    /* Split energy for SBR FIXFIX grid at midpoint. */
                    int h = (slot >= (num_slots / 2)) ? 1 : 0;

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
            for (int k = kx; k < SBR_QMF_BANDS_64; k++) {
                faac_real e_hf = sumE[k];
                faac_real e_lf = sumE[k - kx];
                faac_real ratio = e_hf / (e_lf + SBR_ENERGY_FLOOR);
                sa->ch[ch].bandTonality[k] = (ratio > (faac_real)1.0) ? (faac_real)1.0 : ratio;
            }
        }

    }
}
