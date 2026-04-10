/****************************************************************************
    Shared AAC encoding constants and utility functions

    Copyright (C) 2017 Krzysztof Nikiel
    Copyright (C) 2024 Jules

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

#ifndef AAC_CONSTANTS_H
#define AAC_CONSTANTS_H

/* Huffman Codebooks */
enum {
    HCB_ZERO = 0,
    HCB_ESC = 11,
    HCB_PNS = 13,
    HCB_INTENSITY2 = 14,
    HCB_INTENSITY = 15,
    HCB_NONE
};

/* Quality and Quantization Constants */
enum {
    DEFQUAL = 100,
    MAXQUAL = 5000,
    MAXQUALADTS = MAXQUAL,
    MINQUAL = 10,

    /* Scalefactor Management */

    /* Baseline scalefactor value used in bitstream */
    SF_OFFSET = 100,
    /* Minimum allowable scalefactor to prevent underflow */
    SF_MIN = 10,
    /* PNS predictor initialization offset (starts at floor) */
    PNS_SF_OFFSET = SF_OFFSET - SF_MIN,
    /* Max allowed difference between successive scalefactors (AAC spec).
     * NOTE: Changing SF_DELTA requires verifying that book12 remains valid
     * for the new range as per the AAC specification. */
    SF_DELTA = 60,
};

/**
 * Restrict scalefactor delta to the spec-defined ±SF_DELTA range.
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

#endif /* AAC_CONSTANTS_H */
