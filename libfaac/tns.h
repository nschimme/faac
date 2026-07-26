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
 */

#ifndef TNS_H
#define TNS_H


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "coder.h"

struct faacEncStruct;

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
