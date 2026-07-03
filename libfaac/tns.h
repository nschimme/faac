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

/**
 * Temporal Noise Shaping (TNS)
 *
 * This implementation provides a clean-room LGPL version of the AAC TNS tool.
 * TNS is used to control the temporal shape of quantization noise by applying
 * a predictive filter across the frequency axis. This is particularly effective
 * for transient signals where it helps to suppress audible pre-echoes by
 * leveraging the temporal masking properties of the human auditory system.
 *
 * Performance optimizations include:
 * - Efficient pointer arithmetic for better autovectorization.
 * - Minimal stack-based memory usage.
 * - Single-pass algorithm structures where applicable.
 * - Focus on long-window TNS (MPEG-4 AAC-LC), as short-window TNS was
 *   found to provide marginal benefit for the added complexity.
 */

#ifndef TNS_H
#define TNS_H

#include "faac_real.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct faacEncStruct;
typedef struct faacEncStruct faacEncStruct;

/**
 * Initialize TNS parameters for the given encoder instance.
 * Sets up bitrate-dependent thresholds and frequency band limits.
 */
void TnsInit(faacEncStruct* hEncoder);

/**
 * Perform TNS analysis and filtering on a single channel's spectral data.
 *
 * TNS analysis is performed in-place on the spectral coefficients (`spec`).
 * If a significant prediction gain is found, the spectrum is filtered using
 * an FIR analysis filter. The produced filter parameters are stored in
 * `tnsInfo` for bitstream encoding.
 *
 * @param tnsInfo Channel-specific TNS state.
 * @param numBands Total number of scalefactor bands.
 * @param blockType Current window type (LONG, SHORT, etc.).
 * @param sfbOffsetTable Table of spectral line offsets for each SFB.
 * @param spec Spectral coefficients (modified in-place).
 * @param tdEnvelope Time-domain energy envelope for direction heuristics.
 * @param tdEnvelopeLen Number of sub-blocks in the envelope.
 */
void TnsEncode(TnsInfo* tnsInfo, int numBands,
               enum WINDOW_TYPE blockType, int* sfbOffsetTable,
               faac_real* spec,
               const float* tdEnvelope, int tdEnvelopeLen);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* TNS_H */
