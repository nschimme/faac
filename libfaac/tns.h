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
 * Long blocks only -- the caller must not pass an ONLY_SHORT_WINDOW channel. */
void TnsEncode(CoderInfo *coderInfo, float *spec);

/* Probe-only (ladder step 1): standard Levinson step-up from `order`
 * reflection coefficients `k[1..order]` to the direct-form AR polynomial
 * `a[0..order]` (a[0]=1), FAAC's own internal convention -- exposed so
 * step1.c can turn a reference's already-quantized (dequantized-to-float)
 * TNS coefficients into a filter usable by the encoder's own filtering
 * convention, without duplicating the recursion and risking a sign or
 * indexing mismatch against tns.c's own (tested, decoder-compatible) one. */
void TnsFinalizeFilterProbe(int order, const float *k, float *a);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* TNS_H */
