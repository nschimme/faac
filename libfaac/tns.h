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

/*
 * Temporal Noise Shaping (TNS): a predictive filter along the frequency axis
 * that reshapes quantization noise in time so it hides behind transients
 * instead of leaking out as pre-echo. Long-window only here; short windows
 * already have the temporal resolution to not need it.
 */

#ifndef TNS_H
#define TNS_H

#include "coder.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Latch the per-channel band limits from the sample rate's TNS tool table. */
void TnsInit(faacEncStruct* hEncoder);

/* Analyse one channel and, if it pays off, whiten `spec` in place.
 * tdEnvelope is the block-switcher's per-sub-block high-pass energy
 * timeline for this frame (tdEnvelopeLen entries, see PsyGetCurEnvelope):
 * used both to skip the LPC work on frames with no real attack to mask,
 * and to pick which half of the block the filter should push its
 * quantization noise towards. */
void TnsEncode(TnsInfo* tnsInfo, int numBands,
               enum WINDOW_TYPE blockType, int* sfbOffsetTable,
               float* spec, const float* tdEnvelope, int tdEnvelopeLen);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* TNS_H */
