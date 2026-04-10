/****************************************************************************
    Huffman coding
****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include "coder.h"
#include "huffdata.h"
#include "huff2.h"
#include "bitstream.h"
#include "quantize.h"
#include "util.h"

static inline int escape(int x, int *code)
{
    int preflen = 0, base;
    if (UNLIKELY(x > MAX_HUFF_ESC_VAL)) x = MAX_HUFF_ESC_VAL;
    if (x >= 32) {
#ifdef __GNUC__
        preflen = 31 - __builtin_clz((unsigned)x) - 4;
#else
        int tmp = x >> 5; while (tmp) { tmp >>= 1; preflen++; }
#endif
    }
    base = 1 << (preflen + 4);
    *code = (((1 << preflen) - 1) << (preflen + 5)) | (x - base);
    return (preflen << 1) + 5;
}

static inline void StageCodeword(int data, int len, CoderInfo *coder, int *datacnt, int *bits)
{
    if (coder) {
        coder->s[*datacnt].data = data;
        coder->s[(*datacnt)++].len = len;
    }
    *bits += len;
}

static int huffcode(const int * __restrict qs, int len, int bnum, CoderInfo * __restrict coder)
{
    static hcode16_t * const hmap[12] = {0, book01, book02, book03, book04,
      book05, book06, book07, book08, book09, book10, book11};
    hcode16_t *book = hmap[bnum];
    int bits = 0, ofs, datacnt = 0;
    if (coder) {
        datacnt = coder->datacnt;
        if (UNLIKELY((size_t)len * 2 >= (size_t)(DATASIZE - datacnt))) return -1;
    }
    for (ofs = 0; ofs < len; ) {
        const int *qp = qs + ofs;
        int data = 0, blen = 0, idx = 0;
        if (bnum <= 4) {
            if (bnum <= 2) {
                idx = 27 * qp[0] + 9 * qp[1] + 3 * qp[2] + qp[3] + 40;
                data = book[idx].data; blen = book[idx].len;
            } else {
                int q0 = qp[0], q1 = qp[1], q2 = qp[2], q3 = qp[3];
                idx = 27 * abs(q0) + 9 * abs(q1) + 3 * abs(q2) + abs(q3);
                data = book[idx].data; blen = book[idx].len;
                if (q0) { blen++; data = (data << 1) | (q0 < 0); }
                if (q1) { blen++; data = (data << 1) | (q1 < 0); }
                if (q2) { blen++; data = (data << 1) | (q2 < 0); }
                if (q3) { blen++; data = (data << 1) | (q3 < 0); }
            }
            ofs += 4;
        } else {
            int q0 = qp[0], q1 = qp[1];
            if (bnum <= 6) {
                idx = 9 * q0 + q1 + 40;
                data = book[idx].data; blen = book[idx].len;
            } else {
                int a0 = abs(q0), a1 = abs(q1), it;
                if (bnum <= 8) it = 8 * a0 + a1;
                else if (bnum <= 10) it = 13 * a0 + a1;
                else it = 17 * (a0 > 16 ? 16 : a0) + (a1 > 16 ? 16 : a1);
                data = book[it].data; blen = book[it].len;
                if (q0) { blen++; data = (data << 1) | (q0 < 0); }
                if (q1) { blen++; data = (data << 1) | (q1 < 0); }
            }
            ofs += 2;
        }
        StageCodeword(data, blen, coder, &datacnt, &bits);
        if (UNLIKELY(bnum == HCB_ESC)) {
            int a0 = abs(qp[0]), a1 = abs(qp[1]);
            if (UNLIKELY(a0 >= 16)) {
                int ed, el = escape(a0, &ed);
                StageCodeword(ed, el, coder, &datacnt, &bits);
            }
            if (UNLIKELY(a1 >= 16)) {
                int ed, el = escape(a1, &ed);
                StageCodeword(ed, el, coder, &datacnt, &bits);
            }
        }
    }
    if (coder) coder->datacnt = datacnt;
    return bits;
}

int huffbook(CoderInfo * __restrict coder, const int * __restrict qs, int len)
{
    int cnt, maxq = 0, bookmin, lenmin;
    for (cnt = 0; cnt < len; cnt++) {
        int q = abs(qs[cnt]); if (maxq < q) maxq = q;
    }
#define BOOKMIN(n) bookmin=n; lenmin=huffcode(qs,len,bookmin,0); if(huffcode(qs,len,bookmin+1,0)<lenmin) bookmin++;
    if (maxq < 1) { bookmin = HCB_ZERO; lenmin = 0; }
    else if (maxq < 2) { BOOKMIN(1); }
    else if (maxq < 3) { BOOKMIN(3); }
    else if (maxq < 5) { BOOKMIN(5); }
    else if (maxq < 8) { BOOKMIN(7); }
    else if (maxq < 13) { BOOKMIN(9); }
    else bookmin = HCB_ESC;
    if (bookmin > HCB_ZERO) huffcode(qs, len, bookmin, coder);
    coder->book[coder->bandcnt] = bookmin;
    return 0;
}

int writebooks(CoderInfo *coder, BitStream *stream, int write)
{
    int bits = 0, maxcnt, cntbits, group, bb = 4;
    if (coder->block_type == ONLY_SHORT_WINDOW) { maxcnt = 7; cntbits = 3; }
    else { maxcnt = 31; cntbits = 5; }
    for (group = 0; group < coder->groups.n; group++) {
        int band = group * coder->sfbn, maxband = band + coder->sfbn;
        while (band < maxband) {
            int book = coder->book[band++], bookcnt = 1;
            if (write) PutBit(stream, book, bb);
            bits += bb;
            if (band < maxband) {
                while (book == coder->book[band]) {
                    band++; bookcnt++;
                    if (band >= maxband) break;
                }
            }
            while (bookcnt >= maxcnt) {
                if (write) PutBit(stream, maxcnt, cntbits);
                bits += cntbits; bookcnt -= maxcnt;
            }
            if (write) PutBit(stream, bookcnt, cntbits);
            bits += cntbits;
        }
    }
    return bits;
}

int writesf(CoderInfo *coder, BitStream *stream, int write)
{
    int cnt, bits = 0, lastsf = coder->global_gain, lastis = 0, lastpns = coder->global_gain - PNS_SF_OFFSET, initpns = 1;
    for (cnt = 0; cnt < coder->bandcnt; cnt++) {
        int book = coder->book[cnt];
        if (book == HCB_INTENSITY || book == HCB_INTENSITY2) {
            int diff = ClampSfDiff(coder->sf[cnt] - lastis);
            int bl = book12[SF_DELTA + diff].len;
            if (write) PutBit(stream, book12[SF_DELTA + diff].data, bl);
            bits += bl; lastis += diff;
        } else if (book == HCB_PNS) {
            if (initpns) {
                int diff = coder->sf[cnt] - lastpns;
                if (write) PutBit(stream, diff + 256, 9);
                bits += 9; lastpns += diff; initpns = 0;
            } else {
                int diff = ClampSfDiff(coder->sf[cnt] - lastpns);
                int bl = book12[SF_DELTA + diff].len;
                if (write) PutBit(stream, book12[SF_DELTA + diff].data, bl);
                bits += bl; lastpns += diff;
            }
        } else if (book != HCB_ZERO && book != HCB_NONE) {
            int diff = ClampSfDiff(coder->sf[cnt] - lastsf);
            int bl = book12[SF_DELTA + diff].len;
            if (write) PutBit(stream, book12[SF_DELTA + diff].data, bl);
            bits += bl; lastsf += diff;
        }
    }
    return bits;
}
