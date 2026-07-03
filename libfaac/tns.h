/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2024 FAAC Project
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
 * Temporal Noise Shaping (TNS): a predictive filter along the frequency axis
 * that reshapes quantization noise in time so it hides behind transients
 * instead of leaking out as pre-echo. Long-window only here; short windows
 * already have the temporal resolution to not need it.
 */

#ifndef TNS_H
#define TNS_H

#include "faac_real.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct faacEncStruct;
typedef struct faacEncStruct faacEncStruct;

/* Latch the per-channel band limits and gain threshold; also resolves whether
 * TNS runs at all, since it is gated off above TNS_MAX_BITRATE. */
void TnsInit(faacEncStruct* hEncoder);

/* Analyse one channel and, if it pays off, whiten `spec` in place. tdEnvelope
 * is the block switcher's sub-block energy: it both gates the tool (needs a
 * real temporal event to hide noise behind) and picks the filter direction. */
void TnsEncode(TnsInfo* tnsInfo, int numBands,
               enum WINDOW_TYPE blockType, int* sfbOffsetTable,
               faac_real* spec,
               const float* tdEnvelope, int tdEnvelopeLen);

/* Serialize this channel's tns_data() syntax element. writeFlag==0 only tallies
 * the bit cost (the rate loop needs the size before committing the frame).
 * Returns the number of bits written/counted. */
int TnsWriteBitstream(CoderInfo* coderInfo, BitStream* bitStream,
                      int writeFlag);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* TNS_H */
