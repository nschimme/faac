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
 * Temporal Noise Shaping (TNS): predictive filtering along the frequency
 * axis that reshapes quantization noise in time.
 */

#ifndef TNS_H
#define TNS_H

#include "coder.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Latch the per-channel band limits from the sample rate's TNS tool table. */
void TnsInit(faacEncStruct* hEncoder);

/* Analyse a channel and filter windows that pass the fit gates. */
void TnsEncode(CoderInfo *coderInfo, float *spec);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* TNS_H */
