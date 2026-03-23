/****************************************************************************
    ABR rate control functions

    Copyright (C) 2001 Menno Bakker

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
****************************************************************************/

#ifndef ABR_H
#define ABR_H

#include "faac_real.h"

typedef struct {
    faac_real quality_base;      /* Slow-loop baseline; drifts to correct bitrate  */
    int       bit_reservoir;     /* Current reservoir level in bits                 */
    int       bit_reservoir_max; /* Maximum reservoir capacity in bits              */
    int       desbits;           /* Target bits/frame, all channels combined        */
} abr_t;

void AbrInit(abr_t *abr, int bitRate, int sampleRate, int numChannels, faac_real initial_quality);
void AbrUpdate(abr_t *abr, int frameBytes, faac_real *quality, int maxqual);

#endif
