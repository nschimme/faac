/****************************************************************************
    Huffman coding

    Copyright (C) 2017 Krzysztof Nikiel

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

#ifndef HUFF2_H
#define HUFF2_H

#include "bitstream.h"
#include "quantize.h"

/* Huffman Codebooks */
enum {
    HCB_ZERO = 0,
    HCB_ESC = 11,
    HCB_PNS = 13,
    HCB_INTENSITY2 = 14,
    HCB_INTENSITY = 15,
    HCB_NONE
};

/* Maximum value representable by Huffman Book 11 escape sequences.
 * Values >= 8192 would cause bitstream overflow/sync loss. */
#define MAX_HUFF_ESC_VAL 8191

/* Scalefactor Management */
enum {
    /* Baseline scalefactor value used in bitstream */
    SF_OFFSET = 100,
    /* Minimum allowable scalefactor to prevent underflow */
    SF_MIN = 10,
    /* PNS predictor initialization offset (starts at floor) */
    SF_PNS_OFFSET = SF_OFFSET - SF_MIN,
    /* Max allowed difference between successive scalefactors (AAC spec) */
    SF_DELTA = 60,
};

/* The maximum absolute value for xr before quantization that stays within
 * MAX_HUFF_ESC_VAL. Calculated based on the inverse of the AAC quantization
 * formula: x_quant = (x * sfacfix)^0.75 + MAGIC_NUMBER.
 * Derived as: (MAX_HUFF_ESC_VAL + 1 - MAGIC_NUMBER)^(4/3). */
#define MAX_QUANT_LIMIT (FAAC_POW((faac_real)MAX_HUFF_ESC_VAL + (faac_real)1.0 - (faac_real)MAGIC_NUMBER, (faac_real)4.0/(faac_real)3.0))

/**
 * Restrict scalefactor delta to the spec-defined +/- SF_DELTA range.
 * This ensures the delta remains valid for Book 12 Huffman encoding.
 */
static inline int clamp_sf_diff(int diff)
{
    if (diff > SF_DELTA)
        return SF_DELTA;
    if (diff < -SF_DELTA)
        return -SF_DELTA;
    return diff;
}

int huffbook(CoderInfo *coderInfo,
             int *qs /* quantized spectrum */,
             int len);
int writebooks(CoderInfo *coder, BitStream *stream, int writeFlag);
int writesf(CoderInfo *coder, BitStream *bitStream, int writeFlag);

#endif /* HUFF2_H */
