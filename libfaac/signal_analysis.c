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

#include "signal_analysis.h"
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

        /* First pass: Time-domain energy to find transient position. */
        for (int slot = 0; slot < num_slots; slot++) {
            const faac_real * restrict p_in = fullPtrs[ch] + slot * SBR_QMF_BANDS_64;
            faac_real stot = (faac_real)0.0;
            for (int n = 0; n < SBR_QMF_BANDS_64; n++) {
                faac_real val = *p_in++;
                stot += val * val;
            }
            if (stot > smax) {
                smax = stot;
                smax_idx = slot;
            }
            ssum += stot;
        }

        sa->ch[ch].transientStrength = smax * (faac_real)sampled / (ssum + SBR_ENERGY_FLOOR);
        sa->ch[ch].transientSlot = smax_idx;

        /* Ported PsyCheckShort relative-jump logic onto slot energies. */
        sa->ch[ch].wantShort = 0;
        faac_real last_stot = 0.0;
        int have_last = 0;
        for (int slot = 0; slot < num_slots; slot++) {
            const faac_real * restrict p_in = fullPtrs[ch] + slot * SBR_QMF_BANDS_64;
            faac_real stot = (faac_real)0.0;
            for (int n = 0; n < SBR_QMF_BANDS_64; n++) {
                faac_real val = *p_in++;
                stot += val * val;
            }
            if (have_last) {
                faac_real toteng = (stot < last_stot) ? stot : last_stot;
                faac_real volchg = (stot > last_stot) ? (stot - last_stot) : (last_stot - stot);
                /* PSY_TD_THRESH = 0.5 */
                if (volchg > ((faac_real)0.5 * toteng)) {
                    sa->ch[ch].wantShort = 1;
                    break;
                }
            }
            last_stot = stot;
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

        /* Tonality = spectral stability across slots (tonality = sumE^2 / (sampled * sumE2)).
         * Steady tones have stable per-slot energy (low variance), so sumE^2 approaches sampled * sumE2.
         * Noise has high per-slot energy variance, so sumE^2 is much lower. */
        for (int k = 0; k < SBR_QMF_BANDS_64; k++) {
            if (sumE2[k] > SBR_ENERGY_FLOOR) {
                sa->ch[ch].bandTonality[k] = (sumE[k] * sumE[k]) / ((faac_real)sampled * sumE2[k]);
            } else {
                sa->ch[ch].bandTonality[k] = (faac_real)0.0;
            }
        }

        /* HE-AAC only: bias the tonality measure using the original-vs-transposed ratio.
         * SBR reconstructs highband k from lowband (k - kx).
         * Ratio[k] = E_orig[k] / (E_orig[k-kx] + floor).
         * If Ratio[k] is low (original HF is much quieter than LF patch), then the
         * reconstructed HF will be TOO LOUD/TONAL and needs noise/whitening.
         * Final tonality = Temporal_Tonality * min(1.0, Ratio). */
        if (sbr) {
            int kx = sbr->kx;
            for (int k = kx; k < SBR_QMF_BANDS_64; k++) {
                faac_real e_hf = sumE[k];
                faac_real e_lf = sumE[k - kx];
                faac_real ratio = e_hf / (e_lf + SBR_ENERGY_FLOOR);
                if (ratio < (faac_real)1.0)
                    sa->ch[ch].bandTonality[k] *= ratio;
            }
        }

    }
}
