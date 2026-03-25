/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2001 Menno Bakker
 */

#include <math.h>
#include "util.h"
#include "coder.h"

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

unsigned int MaxBitrate(unsigned long sampleRate)
{
    return 0x2000 * 8 * (faac_real)sampleRate/(faac_real)FRAME_LEN;
}

unsigned int MinBitrate()
{
    return 8000;
}

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
     * Noisefloor refinement:
     * - Low bitrates (<= 16k): 0.8 to suppress noise in near-silence.
     * - Transitioning to legacy default 0.4 at 32k-48k to prevent regressions.
     * - Higher bitrates (> 64k): 0.1 for maximum transparency.
     */
    if (bitRatePerChannel <= 16000) return 0.8;
    if (bitRatePerChannel <= 32000) return lerp(bitRatePerChannel, 16000, 0.8, 32000, 0.5);
    if (bitRatePerChannel <= 48000) return 0.4;
    if (bitRatePerChannel <= 64000) return lerp(bitRatePerChannel, 48000, 0.4, 64000, 0.1);
    return 0.1;
}

faac_real calc_powm(unsigned long bitRatePerChannel)
{
    /*
     * POWM refinement:
     * - Low bitrates (<= 16k): 0.2 optimized for speech masking.
     * - Legacy default 0.4 for mid-tier (32k-48k).
     * - High fidelity (64k+): 0.2 (more conservative masking).
     */
    if (bitRatePerChannel <= 16000) return 0.2;
    if (bitRatePerChannel <= 32000) return lerp(bitRatePerChannel, 16000, 0.2, 32000, 0.4);
    if (bitRatePerChannel <= 48000) return 0.4;
    if (bitRatePerChannel <= 64000) return lerp(bitRatePerChannel, 48000, 0.4, 64000, 0.2);
    return 0.2;
}
