/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2001 Menno Bakker
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

/*
 * Temporal Noise Shaping (TNS) for MPEG-4 AAC-LC.
 *
 * This is a clean-room LGPL implementation of the AAC TNS tool. It applies a
 * linear-predictive (LPC) filter across the frequency axis of a long block to
 * flatten the temporal envelope of the quantization noise, reducing pre-echo on
 * transient signals. The design follows the AAC specification (ISO/IEC 14496-3)
 * and the structure of the LGPL TNS encoder in FFmpeg; no code is derived from
 * the MPEG reference software.
 */

#ifndef TNS_H
#define TNS_H

#include "faac_real.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void TnsInit(faacEncStruct* hEncoder);

/* Analyse and (in place) filter the long-window spectrum in `spec`.
 *
 * `tdEnvelope` / `tdEnvelopeLen` are the current frame's per-sub-block
 * time-domain energy envelope (from the block switcher, see PsyGetCurEnvelope);
 * they drive the forward/backward filter-direction decision. May be NULL, in
 * which case forward filtering is used.
 *
 * TNS must run before spectral grouping/stereo/quantization: those stages see
 * the filtered spectrum, and the quantizer reads the produced filter band
 * ranges to inhibit PNS substitution.
 */
void TnsEncode(TnsInfo* tnsInfo, int numBands,
               enum WINDOW_TYPE blockType, int* sfbOffsetTable,
               faac_real* spec,
               const float* tdEnvelope, int tdEnvelopeLen);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* TNS_H */
