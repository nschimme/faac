/****************************************************************************
    Huffman encoding implementation

    Copyright (C) 2001 Menno Bakker
    Copyright (C) 2026 Nils Schimmelmann

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.
****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coder.h"
#include "huffdata.h"
#include "huff2.h"
#include "bitstream.h"

static int escape(int val, int *code) {
    if (val < 16) return 0;
    int n = 0;
    while (val >= (1 << (n + 5))) n++;
    if (n > 12) return 0;
    int suffix_len = n + 4;
    int suffix = val - (1 << suffix_len);
    *code = (((1 << n) - 1) << (suffix_len + 1)) | suffix;
    return 2 * n + 5;
}

int huffcode(int *qs, int len, int bnum, CoderInfo *coder) {
    static hcode16_t * const hmap[12] = {
        NULL, book01, book02, book03, book04, book05, book06, book07, book08, book09, book10, book11
    };
    if (bnum < 1 || bnum > 11) return (coder ? -1 : 1000000);
    hcode16_t *book = hmap[bnum];
    int bits = 0;
    int datacnt = coder ? coder->datacnt : 0;

    for (int ofs = 0; ofs < len; ) {
        int step = (bnum <= 4) ? 4 : 2;
        int *qp = qs + ofs;
        if (bnum == 11) {
            int x0 = abs(qp[0]);
            int x1 = abs(qp[1]);
            int idx = 17 * (x0 > 16 ? 16 : x0) + (x1 > 16 ? 16 : x1);
            int blen = book[idx].len;
            int data = book[idx].data;
            if (qp[0]) { blen++; data = (data << 1) | (qp[0] < 0); }
            if (qp[1]) { blen++; data = (data << 1) | (qp[1] < 0); }
            if (coder) {
                if (datacnt >= DATASIZE) return -1;
                coder->s[datacnt].data = data;
                coder->s[datacnt++].len = blen;
            }
            bits += blen;
            if (x0 >= 16) {
                int ed = 0, el = escape(x0, &ed);
                bits += el;
                if (coder) {
                    if (datacnt >= DATASIZE) return -1;
                    coder->s[datacnt].data = ed;
                    coder->s[datacnt++].len = el;
                }
            }
            if (x1 >= 16) {
                int ed = 0, el = escape(x1, &ed);
                bits += el;
                if (coder) {
                    if (datacnt >= DATASIZE) return -1;
                    coder->s[datacnt].data = ed;
                    coder->s[datacnt++].len = el;
                }
            }
        } else {
            int idx;
            if (bnum <= 2) idx = 27 * qp[0] + 9 * qp[1] + 3 * qp[2] + qp[3] + 40;
            else if (bnum <= 4) idx = 27 * abs(qp[0]) + 9 * abs(qp[1]) + 3 * abs(qp[2]) + abs(qp[3]);
            else if (bnum <= 6) idx = 9 * qp[0] + qp[1] + 40;
            else if (bnum <= 8) idx = 8 * abs(qp[0]) + abs(qp[1]);
            else idx = 13 * abs(qp[0]) + abs(qp[1]);
            int blen = book[idx].len;
            int data = book[idx].data;
            if (bnum == 3 || bnum == 4 || bnum >= 7) {
                for (int i = 0; i < step; i++) {
                    if (qp[i]) {
                        blen++;
                        if (coder) data = (data << 1) | (qp[i] < 0);
                    }
                }
            }
            if (coder) {
                if (datacnt >= DATASIZE) return -1;
                coder->s[datacnt].data = data;
                coder->s[datacnt++].len = blen;
            }
            bits += blen;
        }
        ofs += step;
    }
    if (coder) coder->datacnt = datacnt;
    return bits;
}

int huff_count_bits(int *qs, int len, int *best_book) {
    int i, mq = 0;
    for (i = 0; i < len; i++) {
        int v = abs(qs[i]);
        if (v > mq) mq = v;
    }
    if (mq == 0) {
        if (best_book) *best_book = 0;
        return 0;
    }
    int bb = 11, bl = huffcode(qs, len, 11, NULL);
    if (mq < 2) {
        int l = huffcode(qs, len, 1, NULL); if (l < bl) { bl = l; bb = 1; }
        l = huffcode(qs, len, 2, NULL); if (l < bl) { bl = l; bb = 2; }
    } else if (mq < 3) {
        int l = huffcode(qs, len, 3, NULL); if (l < bl) { bl = l; bb = 3; }
        l = huffcode(qs, len, 4, NULL); if (l < bl) { bl = l; bb = 4; }
    } else if (mq < 5) {
        int l = huffcode(qs, len, 5, NULL); if (l < bl) { bl = l; bb = 5; }
        l = huffcode(qs, len, 6, NULL); if (l < bl) { bl = l; bb = 6; }
    } else if (mq < 8) {
        int l = huffcode(qs, len, 7, NULL); if (l < bl) { bl = l; bb = 7; }
        l = huffcode(qs, len, 8, NULL); if (l < bl) { bl = l; bb = 8; }
    } else if (mq < 13) {
        int l = huffcode(qs, len, 9, NULL); if (l < bl) { bl = l; bb = 9; }
        l = huffcode(qs, len, 10, NULL); if (l < bl) { bl = l; bb = 10; }
    }
    if (best_book) *best_book = bb;
    return bl;
}

int huffbook(CoderInfo *ci, int *qs, int len) {
    int bb;
    huff_count_bits(qs, len, &bb);
    int saved = ci->datacnt;
    if (bb > 0 && huffcode(qs, len, bb, ci) < 0) {
        ci->datacnt = saved;
        bb = 0;
    }
    ci->book[ci->bandcnt++] = bb;
    return 0;
}

int writebooks(CoderInfo *ci, BitStream *s, int write) {
    int bits = 0;
    int max_sect_len = (ci->block_type == ONLY_SHORT_WINDOW ? 7 : 31);
    int sect_len_bits = (ci->block_type == ONLY_SHORT_WINDOW ? 3 : 5);
    for (int g = 0; g < ci->groups.n; g++) {
        int b = g * ci->sfbn;
        int maxb = b + ci->sfbn;
        while (b < maxb) {
            int bk = ci->book[b++];
            int n = 1;
            while (b < maxb && ci->book[b] == bk) { b++; n++; }
            if (write) PutBit(s, bk, 4);
            bits += 4;
            while (n >= max_sect_len) {
                if (write) PutBit(s, max_sect_len, sect_len_bits);
                bits += sect_len_bits;
                n -= max_sect_len;
            }
            if (write) PutBit(s, n, sect_len_bits);
            bits += sect_len_bits;
        }
    }
    return bits;
}

int writesf(CoderInfo *ci, BitStream *s, int write) {
    int bits = 0;
    int lastsf = ci->global_gain;
    int lastis = 0;
    int lastpns = ci->global_gain - 90;
    int initpns = 1;
    for (int i = 0; i < ci->bandcnt; i++) {
        int bk = ci->book[i];
        int diff, len;
        if (bk == HCB_INTENSITY || bk == HCB_INTENSITY2) {
            diff = ci->sf[i] - lastis;
            if (diff < -60) diff = -60;
            if (diff > 60) diff = 60;
            len = book12[60 + diff].len;
            bits += len;
            lastis += diff;
            if (write) PutBit(s, book12[60 + diff].data, len);
        } else if (bk == HCB_PNS) {
            diff = ci->sf[i] - lastpns;
            if (initpns) {
                initpns = 0;
                bits += 9;
                lastpns += diff;
                if (write) PutBit(s, diff + 256, 9);
            } else {
                if (diff > 60) diff = 60;
                if (diff < -60) diff = -60;
                len = book12[60 + diff].len;
                bits += len;
                lastpns += diff;
                if (write) PutBit(s, book12[60 + diff].data, len);
            }
        } else if (bk != HCB_ZERO && bk != HCB_NONE) {
            diff = ci->sf[i] - lastsf;
            if (diff > 60) diff = 60;
            if (diff < -60) diff = -60;
            len = book12[60 + diff].len;
            bits += len;
            lastsf += diff;
            if (write) PutBit(s, book12[60 + diff].data, len);
        }
    }
    return bits;
}
