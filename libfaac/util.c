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

/* Calculate bit_allocation based on PE and target bitrate */
unsigned int BitAllocation(faac_real pe, int short_block, int numChannels, int bitRatePerChannel, int bitResLevel)
{
    faac_real pew1;
    faac_real pew2;
    faac_real bit_allocation;
    faac_real max_bits = 6144.0 * numChannels;
    faac_real bitrate_fac;

    /* Bitrate-aware and level-aware factor */
    bitrate_fac = (faac_real)bitResLevel / 5.0;

    if (bitRatePerChannel < 32000)
        bitrate_fac *= 0.8;
    else if (bitRatePerChannel > 64000)
        bitrate_fac *= 1.2;

    if (short_block) {
        pew1 = 1.2 * bitrate_fac; // Increased bit allocation for short blocks (transients)
        pew2 = 40.0 * bitrate_fac;
    } else {
        pew1 = 0.4 * bitrate_fac;
        pew2 = 8.0 * bitrate_fac;
    }
    /* Standard linear + square-root PE-to-bits mapping model */
    bit_allocation = pew1 * pe + pew2 * FAAC_SQRT(pe);
    bit_allocation = min(max(0.0, bit_allocation), max_bits);

    return (unsigned int)(bit_allocation+0.5);
}

/* Returns the maximum bit reservoir size */
unsigned int MaxBitresSize(unsigned long bitRate, unsigned long sampleRate)
{
    return 6144 - (unsigned int)((faac_real)bitRate/(faac_real)sampleRate*(faac_real)FRAME_LEN);
}
