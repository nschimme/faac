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
void AnalyzeSignal(struct SignalAnalysis *sa, faac_real *fullPtrs[], int nch, int numSamples, struct SBRInfo *sbr)
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

                    /* Phase 4: Envelope border alignment (quantized to match bitstream). */
                    int border = get_sbr_quantized_border(num_slots, smax_idx);
                    int h = (slot >= border) ? 1 : 0;

                    for (int k = 0; k < SBR_QMF_BANDS_64; k++) {
                        faac_real energy = slotEnergy[k];
                        sa->ch[ch].bandHalfE[h][k] += energy;
                        sumE[k] += energy;
                        sumE2[k] += energy * energy;
                    }
                }
            }
        }

        /* Phase 2: Compute tonality from accumulated metrics. */
        for (int k = 0; k < SBR_QMF_BANDS_64; k++) {
            if (sumE2[k] > SBR_ENERGY_FLOOR) {
                sa->ch[ch].bandTonality[k] = (sumE[k] * sumE[k]) / ((faac_real)sampled * sumE2[k]);
            } else {
                sa->ch[ch].bandTonality[k] = (faac_real)0.0;
            }
        }
    }
}
