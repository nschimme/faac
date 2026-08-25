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
 * instead of leaking out as pre-echo.
 *
 * Long windows get one filter per frame, fit directly on that channel's
 * spectrum. Short (eight-short) windows get one filter per scalefactor
 * window GROUP: the group's windows are pooled (concatenated, after the
 * same per-band RMS normalization idea as the long path) into a single
 * signal that one low-order filter is fit against, then that one filter is
 * applied to -- and transmitted for -- each window in the group. This
 * keeps the filter cheap (low order, one fit per group instead of per
 * window) and avoids a filter that only really fits one window's transient
 * while distorting its group-mates.
 */

#ifndef TNS_H
#define TNS_H

#include "coder.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Latch the per-channel band limits from the sample rate's TNS tool table. */
void TnsInit(faacEncStruct* hEncoder);

/* Analyse one element -- an SCE (nch=1) or a CPE (nch=2) -- fitting and
 * applying an independent TNS filter to each of its nch channels. A CPE's
 * two channels may end up with different filters (or one filtered, one
 * not): each channel's tns_data is transmitted separately, and decoders
 * invert M/S before inverting TNS per channel, so independent per-channel
 * filters are spec-compliant (see tns.c).
 *
 * groups is only consulted for blockType == ONLY_SHORT_WINDOW: groups[c] is
 * channel c's scalefactor-window grouping (from BlocGroup, quantize.c),
 * which can legitimately differ between a CPE's two channels since each
 * channel groups on its own spectrum. Ignored (may be NULL) for long
 * blocks. */
void TnsEncodeElement(TnsInfo** tnsInfos, float** specs, int nch,
                      int numBands, enum WINDOW_TYPE blockType,
                      int* sfbOffsetTable, WindowGroups** groups);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* TNS_H */
