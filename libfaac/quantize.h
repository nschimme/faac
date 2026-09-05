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

#ifndef QUANTIZE_H
#define QUANTIZE_H

#include "coder.h"

typedef struct
{
    float quality;
    int max_cbl;
    int max_cbs;
    int max_l;
    int pnslevel;
} AACQuantCfg;

/* Rounding bias for the x^(3/4) quantization: 0.4054f minimizes average
 * quantization error for a uniform distribution (ISO 14496-3 §8.3.5). */
#define MAGIC_NUMBER 0.4054f

/* Quality is a masking-target multiplier in percent of DEFQUAL. The floor is
 * how small a frame the rate controller can make: at 8 kbps/channel stereo LC
 * a floor of 10 pinned 82% of frames and still landed 62% over target
 * (25.98 kbps for -b 16 on a 32 kHz orchestral clip), because a tenth of the
 * masking target still codes more lines than 512 bits will hold. At 1 the same
 * encode lands at 16.07 kbps with 7% of frames on the floor. Nothing in the
 * quantizer assumes a floor: below the target a band goes HCB_ZERO, which is
 * exactly what such a budget asks for. */
enum {
    DEFQUAL = 100,
    MAXQUAL = 5000,
    MAXQUALADTS = MAXQUAL,
    MINQUAL = 1,
};

void ResetCoderSections(CoderInfo *coderInfo);
int BlocQuant(CoderInfo *coderInfo, float *xr, AACQuantCfg *aacquantCfg);
void CalcBW(unsigned *bw, int rate, SR_INFO *sr, AACQuantCfg *aacquantCfg);
void BlocGroup(CoderInfo *coderInfo, float *xr, CoderInfo *ci_r, float *xr_r, AACQuantCfg *cfg);
void QuantizeInit(void);

#endif
