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

/* Huffman Codebooks */
enum {
    HCB_ZERO = 0,
    HCB_1, HCB_2, HCB_3, HCB_4, HCB_5,
    HCB_6, HCB_7, HCB_8, HCB_9, HCB_10,
    HCB_ESC = 11,
    HCB_DELTA = 12,
    HCB_PNS = 13,
    HCB_INTENSITY2 = 14,
    HCB_INTENSITY = 15,
    HCB_NONE
};

/* Largest Absolute Value (LAV) per book class */
enum {
    LAV_1   = 1,
    LAV_2   = 2,
    LAV_4   = 4,
    LAV_7   = 7,
    LAV_12  = 12,
    LAV_ESC = 16
};

/* Polynomial space dimensions for spectral books */
enum {
    DIM_S4 = 3,  /* HCB_1 to HCB_2: 3^4 = 81 */
    DIM_M4 = 3,  /* HCB_3 to HCB_4: 3^4 = 81 */
    DIM_S2 = 9,  /* HCB_5 to HCB_6: 9^2 = 81 */
    DIM_M2_7 = 8,  /* HCB_7 to HCB_8: 8^2 = 64 */
    DIM_M2_12 = 13, /* HCB_9 to HCB_10: 13^2 = 169 */
    DIM_ESC = 17   /* HCB_ESC: 17^2 = 289 */
};

/* Maximum value representable by HCB_ESC escape sequences.
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
    /* Max absolute scalefactor / global_gain (8-bit bitstream field) */
    SF_MAX_ABS = 255,
};

/**
 * Restrict scalefactor delta to the spec-defined +/- SF_DELTA range.
 * This ensures the delta remains valid for HCB_DELTA Huffman encoding.
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

/* Finalize the Huffman stage on the retained quantized spectrum: greedily merge
 * adjacent spectral sections, then emit the spectral symbols under the merged
 * books. Selection (huffbook) has already cached each band's cost. */
void huffman_finalize(CoderInfo *coder);
int writebooks(CoderInfo *coder, BitStream *stream, int writeFlag);
int writesf(CoderInfo *coder, BitStream *bitStream, int writeFlag);

#endif /* HUFF2_H */
