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

#ifndef SIGNAL_ANALYSIS_H
#define SIGNAL_ANALYSIS_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "faac_real.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SBR_QMF_BANDS_64
#define SBR_QMF_BANDS_64 64
#endif

/* Forward declaration for SBRInfo */
struct SBRInfo;

/* signal_analysis.h — per-frame, per-channel full-rate analysis. */
typedef struct SignalAnalysisChannel {
    int   transientSlot;      /* QMF slot index of peak attack (−1 = none)  */
    faac_real transientStrength; /* peak/mean slot-power ratio (today's tratio) */
    faac_real bandTonality[SBR_QMF_BANDS_64]; /* 0=noise-like .. 1=tonal     */
    faac_real bandHalfE[2][SBR_QMF_BANDS_64]; /* Accumulated QMF energy per envelope */
} SignalAnalysisChannel;

typedef struct SignalAnalysis {
    int valid;                /* set only on the HE path                     */
    int numSlots;
    int sampled;              /* decimation-aware slot count (for Phase 1 byte-identity) */
    SignalAnalysisChannel ch[MAX_CHANNELS];
} SignalAnalysis;

void AnalyzeSignal(SignalAnalysis *sa, faac_real *fullPtrs[], int nch, int numSamples, struct SBRInfo *sbr);

#ifdef __cplusplus
}
#endif

#endif
