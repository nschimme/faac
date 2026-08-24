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

/* Analyse one element -- an SCE (nch=1) or a CPE (nch=2) -- jointly across
 * its nch channels and, if it pays off, whiten all of them in place with the
 * SAME filter. A CPE MUST go through here as a single nch=2 call, never two
 * separate nch=1 calls: TNS runs before AACstereo's M/S/IS mixing, and only
 * one shared filter commutes with that mix (see tns.c). */
void TnsEncodeElement(TnsInfo** tnsInfos, float** specs, int nch,
                      int numBands, enum WINDOW_TYPE blockType,
                      int* sfbOffsetTable);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* TNS_H */
