#ifndef HUFF2_H
#define HUFF2_H
#include "coder.h"
#include "bitstream.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { HCB_NONE = -1, HCB_ZERO = 0, HCB_ESC = 11, HCB_PNS = 13, HCB_INTENSITY = 14, HCB_INTENSITY2 = 15 };
int huffcode(int *qs, int len, int bnum, CoderInfo *coder);
int huff_count_bits(int *qs, int len, int *best_book);
int huffbook(CoderInfo *coder, int *qs, int len);
int writebooks(CoderInfo *coder, BitStream *stream, int write);
int writesf(CoderInfo *coder, BitStream *stream, int write);
#ifdef __cplusplus
}
#endif
#endif
