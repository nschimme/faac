#ifndef HUFF2_H
#define HUFF2_H

#include "bitstream.h"

enum {
    HCB_ZERO = 0,
    HCB_ESC = 11,
    HCB_PNS = 13,
    HCB_INTENSITY2 = 14,
    HCB_INTENSITY = 15,
    HCB_NONE
};

int huffbook(CoderInfo * __restrict coderInfo, const int * __restrict qs, int len);
int writebooks(CoderInfo *coder, BitStream *stream, int writeFlag);
int writesf(CoderInfo *coder, BitStream *bitStream, int writeFlag);

#endif
