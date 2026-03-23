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

/**
 * Calculates the frequency cutoff (bandwidth) for the psychoacoustic model.
 *
 * This piecewise linear model implements 'Bitrate-Aware Damping.' It prevents
 * spectral starvation by aggressively narrowing the bandwidth at mid-tier
 * bitrates (32kbps), ensuring high-quality quantization in the audible range
 * over low-quality extension in the ultrasonic range.
 */
unsigned int CalcBandwidth(unsigned long bitRate, unsigned long sampleRate)
{
    const unsigned int nyquist = sampleRate / 2;
    unsigned int bw;

    if (!bitRate) return nyquist;

    if (bitRate <= 16000) {
        /* Segment 1: Telephony (4kHz to 6kHz) */
        bw = 4000 + (bitRate / 8);
    }
    else if (bitRate <= 32000) {
        /* Segment 2: Low-tier music transition (6kHz to 11kHz)
         * Derived through systematic sweep for optimal music_low MOS.
         */
        bw = 6000 + ((bitRate - 16000) * 5 / 16);
    }
    else if (bitRate <= 64000) {
        /* Segment 3: Mid-tier expansion (11kHz to 18.5kHz)
         * Aggressively scales to hit 18.5kHz behavior for music_std.
         */
        bw = 11000 + ((bitRate - 32000) * 15 / 64);
    }
    else if (bitRate <= 128000) {
        /* Segment 4: High-fidelity refinement (18.5kHz to 20kHz)
         * Reaches transparency ceiling at 128kbps/ch.
         */
        bw = 18500 + ((bitRate - 64000) * 3 / 128);
    }
    else {
        /* Segment 5: Transparency plateau */
        bw = 20000;
    }

    /* Safety clamp to Shannon-Nyquist limit */
    return (bw > nyquist) ? nyquist : bw;
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
