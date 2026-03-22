/*
 * FAAC - Freeware Advanced Audio Coder
 * Pseudo Spectral Band Replication (encoder-side)
 *
 * Fills the MDCT spectrum above the bitrate-derived bandwidth by
 * patching and attenuating the upper coded bandwidth into successive
 * higher-frequency target regions.  Compatible with any AAC decoder;
 * no HE-AAC signalling required.
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
 *
 * Apply encoder-side spectral extension to one channel.
 *
 *   coderInfo  – block-type / group info (block_type must already be set)
 *   freqBuff   – MDCT coefficient buffer for this channel
 *   sampleRate – audio sample rate, Hz
 *   baseBW     – original encoder bandwidth, Hz  (source ceiling)
 *   sbrBW      – target SBR bandwidth, Hz        (fill up to here)
 *   rand       – per-encoder LCG state (advanced in place)
 *
 * Must be called after FilterBank() and before BlocGroup() / BlocQuant().
 * Skip LFE channels.
 */
void PseudoSBR(CoderInfo       *coderInfo,
               faac_real       *freqBuff,
               unsigned int     sampleRate,
               unsigned int     baseBW,
               unsigned int     sbrBW,
               unsigned int    *rand);

/*
 * PseudoSBRTargetBW()
 *
 * Given the bitrate-derived bandwidth, return the SBR target bandwidth.
 * Extends by at most 50 % of baseBW, capped at 90 % of Nyquist.
 * Returns baseBW if no meaningful extension is possible.
 */
unsigned int PseudoSBRTargetBW(unsigned int sampleRate, unsigned int baseBW);

#ifdef __cplusplus
}
#endif

#endif /* PSEUDO_SBR_H */
