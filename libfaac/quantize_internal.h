/****************************************************************************
    Quantizer internal functions

    Copyright (C) 2001 Menno Bakker

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
****************************************************************************/

#ifndef QUANTIZE_INTERNAL_H
#define QUANTIZE_INTERNAL_H

#include "quantize.h"

void CoderInit(CoderInfo *coderInfo);
int BlocQuant(CoderInfo *coderInfo, faac_real *xr, AACQuantCfg *aacquantCfg);
void CalcBW(unsigned *bw, int rate, SR_INFO *sr, AACQuantCfg *aacquantCfg);
void BlocGroup(faac_real *xr, CoderInfo *coderInfo, AACQuantCfg *aacquantCfg);
void BlocStat(void);
void QuantizeInit(void);

#endif
