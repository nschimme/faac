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

#ifndef SBR_ANALYSIS_H
#define SBR_ANALYSIS_H

#include <stdbool.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "coder.h"      /* FRAME_LEN */

#ifdef __cplusplus
extern "C" {
#endif

/* These size the arrays below, so they get exactly one definition each and no
 * "#ifndef ... fallback" form. A fallback would be silently authoritative here:
 * sbr.h includes this header before its own constants are defined, so whatever
 * this file settled on is what SignalAnalysis was already laid out with -- and a
 * divergence would mis-size a buffer rather than just disagree about a number. */
#define SBR_QMF_BANDS_64     64
#define SBR_QMF_OVL_LEN_64  576
#define SBR_MAX_ENVELOPES     2

/* MAX_CHANNELS is the exception: it is a build option (meson max-channels), so
 * config.h -- included above -- owns it whenever there is one, and the value
 * here is only the no-config.h fallback. Defining it unconditionally would lay
 * SignalAnalysis out differently from every other translation unit. */
#ifndef MAX_CHANNELS
#define MAX_CHANNELS         64
#endif

struct SBRInfo;

typedef struct SignalAnalysisChannel {
    int       transientSlot;
    float transientStrength;
} SignalAnalysisChannel;

typedef struct SignalAnalysis {
    int numSlots;
    int sampled;

    /* Frame envelope grid configuration. Synchronized across all channels. */
    int frameClass;
    int numEnvelopes;
    int tEnv[SBR_MAX_ENVELOPES + 1];
    int bsPointer;
    int envSampled[SBR_MAX_ENVELOPES];

    SignalAnalysisChannel ch[MAX_CHANNELS];

    /* Per-envelope QMF band energy, binned over the grid above; only the first
       numEnvelopes rows are written. */
    float bandE[MAX_CHANNELS][SBR_MAX_ENVELOPES][SBR_QMF_BANDS_64];

    /* HE-AAC v2 only: per-band Re{L * conj(R)} and Im{L * conj(R)} */
    float bandCrossE[SBR_MAX_ENVELOPES][SBR_QMF_BANDS_64];
    float bandCrossIm[SBR_MAX_ENVELOPES][SBR_QMF_BANDS_64];
} SignalAnalysis;

void SbrAnalyze(SignalAnalysis *sa, float *fullPtrs[], int nch, const bool *isLfe, int numSamples, struct SBRInfo *sbr);

#ifdef __cplusplus
}
#endif

#endif
