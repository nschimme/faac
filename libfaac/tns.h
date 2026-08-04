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

/* Analyse one channel and, if it pays off, whiten `spec` in place. */
/* `direction` (0 = toward higher frequencies, 1 = toward lower) is a caller
 * input because TnsEncode cannot derive it: the autocorrelation is invariant
 * under sequence reversal, so both directions fit identically and the choice
 * has to come from the time domain.
 *
 * Pinned at 0. Deriving it from the temporal envelope is the obvious move and
 * measures -0.0114 MOS: the LPC gate admits spectrally predictable, i.e.
 * decaying or stationary, frames, so on the frames TNS actually fires the
 * envelope is never rising and an attack-position heuristic has nothing to
 * say. Direction cannot be chosen usefully until admission is
 * transient-driven. FAAC_TNS_DIR forces either value. */
void TnsEncode(TnsInfo* tnsInfo, int numBands,
               enum WINDOW_TYPE blockType, int* sfbOffsetTable,
               float* spec, int direction);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* TNS_H */
