/****************************************************************************
    Huffman coding
****************************************************************************/

#ifndef HUFF2_H
#define HUFF2_H

#include "bitstream.h"
#include "coder.h"

/* Huffman Codebooks */
enum {
    HCB_ZERO = 0,
    HCB_ESC = 11,
    HCB_PNS = 13,
    HCB_INTENSITY2 = 14,
    HCB_INTENSITY = 15,
    HCB_NONE
};

#define MAX_HUFF_ESC_VAL 8191

/* Scalefactor Management */
enum {
    SF_OFFSET = 100,
    SF_MIN = 10,
    SF_PNS_OFFSET = SF_OFFSET - SF_MIN,
    SF_DELTA = 60,
    SF_MAX_ABS = 255,
};

static inline int clamp_sf_diff(int diff)
{
    if (diff > SF_DELTA) return SF_DELTA;
    if (diff < -SF_DELTA) return -SF_DELTA;
    return diff;
}

int huffbook(CoderInfo *coder, int *qs, int len, int prev_book);
int writebooks(CoderInfo *coder, BitStream *stream, int writeFlag);
int writesf(CoderInfo *coder, BitStream *bitStream, int writeFlag);

#endif
