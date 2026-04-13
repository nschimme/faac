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
#include "frame.h"
#include "include/faaccfg.h"

/*
 * SBR Parameters (Perceptually Tuned)
 */

/* Gain rolloff (0.70) provides robust HF presence for 48kHz source material. */
#define SBR_GAIN_ROLLOFF 0.70f

/* Stealth hole filling (0.0001f) maintains spectral texture. */
#define SBR_HOLE_NOISE   0.0001f

/*
 * Bit Allocator Bias (0.40x)
 * Optimal priority balance ensuring HF bands are audible while protecting core bits.
 */
#define SBR_QUAL_BIAS    0.40f

void ApplyPseudoSBR(CoderInfo *coder, faac_real *freq, unsigned long bitRatePerChannel, SR_INFO *srInfo, int useSbr);

#endif /* PSEUDO_SBR_H */
