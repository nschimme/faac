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
 */

#include "util.h"
#include "coder.h"  // FRAME_LEN

#ifdef _MSC_VER
#include <intrin.h>
#endif

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

/* Highest bitrate PER CHANNEL the ISO/IEC 14496-3 per-frame payload allows at
 * this sampling frequency. Per channel, like MinBitratePerCh -- the unit is in
 * the name because callers previously disagreed about it. */
unsigned int MaxBitratePerCh(unsigned long sampleRate)
{
    /* ISO/IEC 14496-3 maximum frame payload: 6144 bits per channel */
    return AAC_MAX_BITS_PER_CH * sampleRate / FRAME_LEN;
}

/* Lowest bitrate PER CHANNEL worth attempting; below this the encoder cannot
 * honour the request anyway. This is a judgement of ours, not a bound the
 * format sets -- unlike MaxBitratePerCh, which is ISO/IEC 14496-3's 6144 bits
 * per channel per frame. It is also flat where the real floor is not: per-frame
 * overhead scales with the frame rate, so 96 kHz cannot reach 8000 bps/ch
 * (it bottoms out near 14600) while 8 kHz comfortably goes below it. Callers
 * that need the true floor should measure the output -- encode_engine.c reports
 * a delivered rate that misses the request. */
unsigned int MinBitratePerCh(void)
{
    return 8000;
}

/* Portable CLZ (returns 32 for x==0); lets escape() get a magnitude's bit-length
 * in O(1) instead of the old shift-until-fits loop. */
int CountLeadingZeros(unsigned int x)
{
    if (x == 0) return 32;
#ifdef _MSC_VER
    unsigned long leading_zero;
    _BitScanReverse(&leading_zero, x);
    return 31 - leading_zero;
#elif defined(__GNUC__) || defined(__clang__)
    return __builtin_clz(x);
#else
    int n = 0;
    if (x <= 0x0000FFFF) { n += 16; x <<= 16; }
    if (x <= 0x00FFFFFF) { n += 8; x <<= 8; }
    if (x <= 0x0FFFFFFF) { n += 4; x <<= 4; }
    if (x <= 0x3FFFFFFF) { n += 2; x <<= 2; }
    if (x <= 0x7FFFFFFF) { n += 1; x <<= 1; }
    return n;
#endif
}

