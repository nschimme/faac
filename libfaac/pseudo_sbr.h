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

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef PSEUDO_SBR_H
#define PSEUDO_SBR_H

#include "coder.h"
#include "faac_real.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Bitrate thresholds for adaptive expansion */
#define SBR_LOW_BR_LIMIT  24000
#define SBR_MID_BR_LIMIT  48000

/* Sample rate limit for forcing SBR (Speech optimization) */
#define SBR_SPEECH_SAMPLERATE_MAX 16000

/* Core bandwidth cap for speech at 16kHz to force SBR lift */
#define SBR_SPEECH_CORE_BW_CAP 7600

/* Tonality Gating constants */
#define SBR_TONAL_THRESH 0.12f
#define SBR_TONAL_ATTEN 0.05f

/* Gain constants */
#define SBR_GAIN_ROLLOFF 0.42f /* -7.5dB per patch */
#define SBR_HOLE_NOISE 0.0001f

void ApplyPseudoSBR(CoderInfo *coder, faac_real *freq, int sampleRate, unsigned long bitRatePerChannel, SR_INFO *srInfo);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* PSEUDO_SBR_H */
