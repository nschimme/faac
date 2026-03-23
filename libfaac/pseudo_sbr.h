/*
 * FAAC - Freeware Advanced Audio Coder
 * Pseudo Spectral Band Replication (encoder-side)
 *
 * Copyright (C) 2026  Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef PSEUDO_SBR_H
#define PSEUDO_SBR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "coder.h"
#include "faac_real.h"

/*
 * PseudoSBR()
 *   Apply encoder-side spectral extension to one channel.
 *   Call after FilterBank() + AACstereo(), before BlocQuant().
 *   Skip LFE channels.
 */
void PseudoSBR(CoderInfo       *coderInfo,
               faac_real       *freqBuff,
               unsigned int     sampleRate,
               unsigned int     baseBW,
               unsigned int     sbrBW,
               unsigned int    *rand);

/*
 * PseudoSBRTargetBW()
 *   Return the SBR ceiling bandwidth.  Uses fill-ratio gating; the
 *   `bitRate` parameter is retained for API stability but is unused.
 *   Returns baseBW when no beneficial extension is possible.
 */
unsigned int PseudoSBRTargetBW(unsigned int sampleRate,
                                unsigned int baseBW,
                                unsigned int bitRate);

/*
 * PseudoSBRShouldEnable()
 *   Returns 1 if pseudo-SBR is expected to improve quality given the
 *   encoder's natural bandwidth and sample rate.
 *
 *   Use this in faacEncSetConfiguration() for the auto-enable decision
 *   instead of a raw bitrate threshold.  `naturalBW` is the value of
 *   hEncoder->config.bandWidth BEFORE any SBR expansion.
 */
int PseudoSBRShouldEnable(unsigned int sampleRate, unsigned int naturalBW);

#ifdef __cplusplus
}
#endif

#endif /* PSEUDO_SBR_H */
