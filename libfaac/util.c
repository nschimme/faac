/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2001 Menno Bakker
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
 *
 */

#include <math.h>

#include "util.h"
#include "coder.h"  // FRAME_LEN

/* Returns the sample rate index */
int GetSRIndex(unsigned int sampleRate)
{
    if (92017 <= sampleRate) return 0;
    if (75132 <= sampleRate) return 1;
    if (55426 <= sampleRate) return 2;
    if (46009 <= sampleRate) return 3;
    if (37566 <= sampleRate) return 4;
    if (27713 <= sampleRate) return 5;
    if (23004 <= sampleRate) return 6;
    if (18783 <= sampleRate) return 7;
    if (13856 <= sampleRate) return 8;
    if (11502 <= sampleRate) return 9;
    if (9391 <= sampleRate) return 10;

    return 11;
}

/* Returns the maximum bitrate for that sampling frequency */
unsigned int MaxBitrate(unsigned long sampleRate)
{
    /* max ADTS frame size 8k */
    return 0x2000 * 8 * (faac_real)sampleRate/(faac_real)FRAME_LEN;
}

/* Returns the minimum bitrate per channel for that sampling frequency */
unsigned int MinBitrate()
{
    return 8000;
}

/* Calculate bit_allocation based on PE */
unsigned int BitAllocation(faac_real pe, int short_block)
{
    faac_real pew1;
    faac_real pew2;
    faac_real bit_allocation;

    if (short_block) {
        pew1 = 0.6;
        pew2 = 24.0;
    } else {
        pew1 = 0.3;
        pew2 = 6.0;
    }
    bit_allocation = pew1 * pe + pew2 * FAAC_SQRT(pe);
    bit_allocation = min(max(0.0, bit_allocation), 6144.0);

    return (unsigned int)(bit_allocation+0.5);
}

/* Returns the maximum bit reservoir size */
unsigned int MaxBitresSize(unsigned long bitRate, unsigned long sampleRate)
{
    return 6144 - (unsigned int)((faac_real)bitRate/(faac_real)sampleRate*(faac_real)FRAME_LEN);
}

static faac_real lerp(faac_real x, faac_real x0, faac_real y0, faac_real x1, faac_real y1)
{
    if (x <= x0) return y0;
    if (x >= x1) return y1;
    return y0 + (y1 - y0) * (x - x0) / (x1 - x0);
}

faac_real calc_noisefloor(unsigned long bitRatePerChannel)
{
    /*
     * Higher bitrates can afford a lower noise floor (more precision).
     * Lower bitrates need a higher noise floor to suppress quantization noise in silence.
     * Derived from parameter sweep (NF: 0.8 @ 16k, 0.6 @ 32k, 0.5 @ 40k, 0.1 @ 64k).
     */
    if (bitRatePerChannel <= 16000) return 0.8;
    if (bitRatePerChannel <= 32000) return lerp(bitRatePerChannel, 16000, 0.8, 32000, 0.6);
    if (bitRatePerChannel <= 40000) return lerp(bitRatePerChannel, 32000, 0.6, 40000, 0.5);
    if (bitRatePerChannel <= 64000) return lerp(bitRatePerChannel, 40000, 0.5, 64000, 0.1);
    return 0.1;
}

faac_real calc_powm(unsigned long bitRatePerChannel)
{
    /*
     * POWM controls the non-linearity of the masking curve.
     * Derived from parameter sweep (PM: 0.2 @ 16k, 0.3 @ 32k, 0.4 @ 40k, 0.1 @ 64k).
     */
    if (bitRatePerChannel <= 16000) return 0.2;
    if (bitRatePerChannel <= 32000) return lerp(bitRatePerChannel, 16000, 0.2, 32000, 0.3);
    if (bitRatePerChannel <= 40000) return lerp(bitRatePerChannel, 32000, 0.3, 40000, 0.4);
    if (bitRatePerChannel <= 64000) return lerp(bitRatePerChannel, 40000, 0.4, 64000, 0.1);
    return 0.1;
}
