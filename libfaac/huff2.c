#include <stdio.h>
#include <stdlib.h>
#include "coder.h"
#include "huffdata.h"
#include "huff2.h"
#include "bitstream.h"
static int escape(int x, int *code) {
    int preflen = 0, base = 32; if (x >= 8192) return 0; *code = 0;
    while (base <= x) { base <<= 1; *code <<= 1; *code |= 1; preflen++; }
    base >>= 1; *code <<= 1; *code <<= (preflen + 4); *code |= (x - base); return (preflen << 1) + 5;
}
#define arrlen(a) (sizeof(a)/sizeof(*(a)))
int huffcode(int *qs, int len, int bnum, CoderInfo *coder) {
    static hcode16_t * const hmap[12] = {0, book01, book02, book03, book04, book05, book06, book07, book08, book09, book10, book11};
    if (bnum < 1 || bnum > 11) return (coder ? -1 : 1000000);
    hcode16_t *book = hmap[bnum]; int bits = 0, datacnt = coder ? coder->datacnt : 0;
    for (int ofs = 0; ofs < len; ) {
        int step = (bnum <= 4) ? 4 : 2; int *qp = qs + ofs;
        if (bnum == 11) {
            int x0 = abs(qp[0]), x1 = abs(qp[1]); int idx = 17 * (x0 > 16 ? 16 : x0) + (x1 > 16 ? 16 : x1);
            int blen = book[idx].len; int data = book[idx].data;
            for (int i = 0; i < 2; i++) if (qp[i]) { blen++; if (coder) data = (data << 1) | (qp[i] < 0); }
            if (coder) { if (datacnt >= DATASIZE) return -1; coder->s[datacnt].data = data; coder->s[datacnt++].len = blen; } bits += blen;
            if (x0 >= 16) { int ed, el = escape(x0, &ed); bits += el; if (coder) { if (datacnt >= DATASIZE) return -1; coder->s[datacnt].data = ed; coder->s[datacnt++].len = el; } }
            if (x1 >= 16) { int ed, el = escape(x1, &ed); bits += el; if (coder) { if (datacnt >= DATASIZE) return -1; coder->s[datacnt].data = ed; coder->s[datacnt++].len = el; } }
        } else {
            int idx; if (bnum <= 2) idx = 27 * qp[0] + 9 * qp[1] + 3 * qp[2] + qp[3] + 40;
            else if (bnum <= 4) idx = 27 * abs(qp[0]) + 9 * abs(qp[1]) + 3 * abs(qp[2]) + abs(qp[3]);
            else if (bnum <= 6) idx = 9 * qp[0] + qp[1] + 40;
            else if (bnum <= 8) idx = 8 * abs(qp[0]) + abs(qp[1]);
            else idx = 13 * abs(qp[0]) + abs(qp[1]);
            int blen = book[idx].len; int data = book[idx].data;
            if (bnum == 3 || bnum == 4 || bnum >= 7) { for (int i = 0; i < step; i++) if (qp[i]) { blen++; if (coder) data = (data << 1) | (qp[i] < 0); } }
            if (coder) { if (datacnt >= DATASIZE) return -1; coder->s[datacnt].data = data; coder->s[datacnt++].len = blen; } bits += blen;
        }
        ofs += step;
    }
    if (coder) coder->datacnt = datacnt; return bits;
}
int huffbook(CoderInfo *ci, int *qs, int len) {
    int mq = 0; for (int i = 0; i < len; i++) if (abs(qs[i]) > mq) mq = abs(qs[i]);
    int b = 0, l; if (mq < 1) b = 0; else if (mq < 2) { b = 1; l = huffcode(qs, len, 1, 0); if (huffcode(qs, len, 2, 0) < l) b = 2; }
    else if (mq < 3) { b = 3; l = huffcode(qs, len, 3, 0); if (huffcode(qs, len, 4, 0) < l) b = 4; }
    else if (mq < 5) { b = 5; l = huffcode(qs, len, 5, 0); if (huffcode(qs, len, 6, 0) < l) b = 6; }
    else if (mq < 8) { b = 7; l = huffcode(qs, len, 7, 0); if (huffcode(qs, len, 8, 0) < l) b = 8; }
    else if (mq < 13) { b = 9; l = huffcode(qs, len, 9, 0); if (huffcode(qs, len, 10, 0) < l) b = 10; }
    else b = 11;
    int saved = ci->datacnt; if (b > 0 && huffcode(qs, len, b, ci) < 0) { ci->datacnt = saved; b = 0; }
    ci->book[ci->bandcnt++] = b; return 0;
}
int writebooks(CoderInfo *ci, BitStream *s, int write) {
    int bits = 0, maxc = (ci->block_type == 2 ? 7 : 31), cntb = (ci->block_type == 2 ? 3 : 5);
    for (int g = 0; g < ci->groups.n; g++) {
        int b = g * ci->sfbn, maxb = b + ci->sfbn;
        while (b < maxb) { int bk = ci->book[b++], n = 1; if (write) PutBit(s, bk, 4); bits += 4;
            while (b < maxb && ci->book[b] == bk) { b++; n++; }
            while (n >= maxc) { if (write) PutBit(s, maxc, cntb); bits += cntb; n -= maxc; }
            if (write) PutBit(s, n, cntb); bits += cntb; }
    } return bits;
}
int writesf(CoderInfo *ci, BitStream *s, int write) {
    int bits = 0, lastsf = ci->global_gain, lastis = 0, lastpns = ci->global_gain - 90, initpns = 1;
    for (int i = 0; i < ci->bandcnt; i++) {
        int bk = ci->book[i], diff, len;
        if (bk == 14 || bk == 15) { diff = ci->sf[i] - lastis; if (diff < -60) diff = -60; if (diff > 60) diff = 60; len = book12[60 + diff].len; bits += len; lastis += diff; if (write) PutBit(s, book12[60 + diff].data, len); }
        else if (bk == 13) { diff = ci->sf[i] - lastpns; if (initpns) { initpns = 0; bits += 9; lastpns += diff; if (write) PutBit(s, diff + 256, 9); }
            else { if (diff > 60) diff = 60; if (diff < -60) diff = -60; len = book12[60 + diff].len; bits += len; lastpns += diff; if (write) PutBit(s, book12[60 + diff].data, len); } }
        else if (bk) { diff = ci->sf[i] - lastsf; if (diff > 60) diff = 60; if (diff < -60) diff = -60; len = book12[60 + diff].len; bits += len; lastsf += diff; if (write) PutBit(s, book12[60 + diff].data, len); }
    } return bits;
}
