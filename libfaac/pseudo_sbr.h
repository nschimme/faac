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

/* Apply SBR extension to one channel. */
void PseudoSBR(CoderInfo       *coderInfo,
               faac_real       *freqBuff,
               unsigned int     sampleRate,
               unsigned int     baseBW,
               unsigned int     sbrBW,
               unsigned int    *rand);

/* Calculate SBR target bandwidth. */
unsigned int PseudoSBRTargetBW(unsigned int sampleRate,
                                unsigned int baseBW,
                                unsigned int bitRate);

/* Returns 1 if SBR should be enabled. */
int PseudoSBRShouldEnable(unsigned int sampleRate, unsigned int naturalBW);

#ifdef __cplusplus
}
#endif

#endif /* PSEUDO_SBR_H */
