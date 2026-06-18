/****************************************************************************
    Huffman coding optimized for Ingenic T31/T40
    Optimized by AI Agent 2024
****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "coder.h"
#include "huffdata.h"
#include "huff2.h"
#include "bitstream.h"
#include "huff_lut.h"

#ifdef _MSC_VER
#include <intrin.h>
static inline int clz(unsigned int x) { unsigned long i; return _BitScanReverse(&i, x) ? 31 - (int)i : 32; }
#else
#define clz(x) __builtin_clz(x)
#endif

static inline int escape_len(int x) {
    int preflen = 0, base = 32;
    while (base <= x) { base <<= 1; preflen++; }
    return (preflen << 1) + 5;
}

static int huff_bits_lut(const int *qs, int len, int bnum) {
    int bits = 0, i, cnt;
    const uint8_t *blut = huff_len_flat + huff_offsets_flat[bnum - 1];
    switch(bnum) {
    case 1: case 2:
        for(i=0; i<len; i+=4) bits += blut[27*qs[i] + 9*qs[i+1] + 3*qs[i+2] + qs[i+3] + 40];
        break;
    case 3: case 4:
        for(i=0; i<len; i+=4) {
            bits += blut[27*abs(qs[i]) + 9*abs(qs[i+1]) + 3*abs(qs[i+2]) + abs(qs[i+3])];
            for(cnt=0; cnt<4; cnt++) if(qs[i+cnt]) bits++;
        }
        break;
    case 5: case 6:
        for(i=0; i<len; i+=2) bits += blut[9*qs[i] + qs[i+1] + 40];
        break;
    case 7: case 8:
        for(i=0; i<len; i+=2) {
            bits += blut[(abs(qs[i]) << 3) + abs(qs[i+1])];
            for(cnt=0; cnt<2; cnt++) if(qs[i+cnt]) bits++;
        }
        break;
    case 9: case 10:
        for(i=0; i<len; i+=2) {
            bits += blut[abs(qs[i])*13 + abs(qs[i+1])];
            for(cnt=0; cnt<2; cnt++) if(qs[i+cnt]) bits++;
        }
        break;
    case 11:
        for(i=0; i<len; i+=2) {
            int v0=abs(qs[i]), v1=abs(qs[i+1]);
            bits += blut[17*((v0>16)?16:v0) + ((v1>16)?16:v1)];
            for(cnt=0; cnt<2; cnt++) if(qs[i+cnt]) bits++;
            if(v0>=16) bits += escape_len(v0);
            if(v1>=16) bits += escape_len(v1);
        }
        break;
    }
    return bits;
}

static int escape(int x, int *code)
{
    int preflen = 0, base = 32;
    if (x > MAX_HUFF_ESC_VAL) return 0;
    *code = 0;
    while (base <= x) { base <<= 1; *code <<= 1; *code |= 1; preflen++; }
    base >>= 1;
    *code <<= 1;
    *code <<= (preflen + 4);
    *code |= (x - base);
    return (preflen << 1) + 5;
}

static int huffcode(int *qs, int len, int bnum, CoderInfo *coder)
{
    static hcode16_t * const hmap[12] = {0, book01, book02, book03, book04,
      book05, book06, book07, book08, book09, book10, book11};
    hcode16_t *book = hmap[bnum];
    int cnt, bits = 0, blen, ofs, *qp, data, idx;
    int datacnt = coder ? coder->datacnt : 0;

    switch (bnum) {
    case 1: case 2:
        for(ofs = 0; ofs < len; ofs += 4) {
            qp = qs+ofs; idx = 27*qp[0] + 9*qp[1] + 3*qp[2] + qp[3] + 40;
            blen = book[idx].len;
            if (coder) { coder->s[datacnt].data = book[idx].data; coder->s[datacnt++].len = blen; }
            bits += blen;
        }
        break;
    case 3: case 4:
        for(ofs = 0; ofs < len; ofs += 4) {
            qp = qs+ofs; idx = 27*abs(qp[0]) + 9*abs(qp[1]) + 3*abs(qp[2]) + abs(qp[3]);
            blen = book[idx].len;
            if (coder) {
                data = book[idx].data;
                for(cnt = 0; cnt < 4; cnt++) if(qp[cnt]) { blen++; data <<= 1; if (qp[cnt] < 0) data |= 1; }
                coder->s[datacnt].data = data; coder->s[datacnt++].len = blen;
            } else { for(cnt = 0; cnt < 4; cnt++) if(qp[cnt]) blen++; }
            bits += blen;
        }
        break;
    case 5: case 6:
        for(ofs = 0; ofs < len; ofs += 2) {
            qp = qs+ofs; idx = 9*qp[0] + qp[1] + 40;
            blen = book[idx].len;
            if (coder) { coder->s[datacnt].data = book[idx].data; coder->s[datacnt++].len = blen; }
            bits += blen;
        }
        break;
    case 7: case 8:
        for(ofs = 0; ofs < len; ofs += 2) {
            qp = qs+ofs; idx = 8*abs(qp[0]) + abs(qp[1]);
            blen = book[idx].len;
            if (coder) {
                data = book[idx].data;
                for(cnt = 0; cnt < 2; cnt++) if(qp[cnt]) { blen++; data <<= 1; if (qp[cnt] < 0) data |= 1; }
                coder->s[datacnt].data = data; coder->s[datacnt++].len = blen;
            } else { for(cnt = 0; cnt < 2; cnt++) if(qp[cnt]) blen++; }
            bits += blen;
        }
        break;
    case 9: case 10:
        for(ofs = 0; ofs < len; ofs += 2) {
            qp = qs+ofs; idx = 13*abs(qp[0]) + abs(qp[1]);
            blen = book[idx].len;
            if (coder) {
                data = book[idx].data;
                for(cnt = 0; cnt < 2; cnt++) if(qp[cnt]) { blen++; data <<= 1; if (qp[cnt] < 0) data |= 1; }
                coder->s[datacnt].data = data; coder->s[datacnt++].len = blen;
            } else { for(cnt = 0; cnt < 2; cnt++) if(qp[cnt]) blen++; }
            bits += blen;
        }
        break;
    case HCB_ESC:
        for(ofs = 0; ofs < len; ofs += 2) {
            int x0, x1, v0, v1;
            qp = qs+ofs; x0 = abs(qp[0]); x1 = abs(qp[1]);
            v0 = (x0 > 16) ? 16 : x0; v1 = (x1 > 16) ? 16 : x1;
            idx = 17 * v0 + v1; blen = book[idx].len;
            if (coder) {
                data = book[idx].data;
                for(cnt = 0; cnt < 2; cnt++) if(qp[cnt]) { blen++; data <<= 1; if (qp[cnt] < 0) data |= 1; }
                coder->s[datacnt].data = data; coder->s[datacnt++].len = blen; bits += blen;
                if (x0 >= 16) { data = 0; blen = escape(x0, &data); coder->s[datacnt].data = data; coder->s[datacnt++].len = blen; bits += blen; }
                if (x1 >= 16) { data = 0; blen = escape(x1, &data); coder->s[datacnt].data = data; coder->s[datacnt++].len = blen; bits += blen; }
            } else {
                for(cnt = 0; cnt < 2; cnt++) if(qp[cnt]) blen++; bits += blen;
                if (x0 >= 16) { int d; bits += escape(x0, &d); } if (x1 >= 16) { int d; bits += escape(x1, &d); }
            }
        }
        break;
    default: return -1;
    }
    if (coder) coder->datacnt = datacnt;
    return bits;
}

int huffbook(CoderInfo *coder, int *qs, int len, int prev_book)
{
    int cnt, maxq = 0, book_best, len_best;
    static const int huff_max_vals[] = {0, 1, 1, 2, 2, 4, 4, 7, 7, 12, 12, 8191};
    for (cnt = 0; cnt < len; cnt++) { int q = abs(qs[cnt]); if (q > maxq) maxq = q; }

    book_best = HCB_ZERO;
    len_best = 1000000;
    if (maxq > 0) {
        int start_book = (maxq < 2) ? 1 : (maxq < 3) ? 3 : (maxq < 5) ? 5 : (maxq < 8) ? 7 : (maxq < 13) ? 9 : 11;
        for (int b = start_book; b <= 11; b++) {
            if (maxq <= huff_max_vals[b]) {
                int l = huff_bits_lut(qs, len, b);
                if (l < len_best) { len_best = l; book_best = b; }
            }
        }
        if (prev_book > HCB_ZERO && prev_book <= HCB_ESC && prev_book != book_best) {
            if (maxq <= huff_max_vals[prev_book]) {
                int len_prev = huff_bits_lut(qs, len, prev_book);
                if (len_prev <= len_best + 4) book_best = prev_book;
            }
        }
    }
    if (book_best > HCB_ZERO) huffcode(qs, len, book_best, coder);
    coder->book[coder->bandcnt] = book_best;
    return 0;
}

int writebooks(CoderInfo *coder, BitStream *stream, int write)
{
    int bits = 0, maxcnt, cntbits, group, bookbits = 4;
    if (coder->block_type == ONLY_SHORT_WINDOW){ maxcnt = 7; cntbits = 3; } else { maxcnt = 31; cntbits = 5; }
    for (group = 0; group < coder->groups.n; group++) {
        int band = group * coder->sfbn, maxband = band + coder->sfbn;
        while (band < maxband) {
            int book = coder->book[band++], bookcnt = 1;
            if (write) PutBit(stream, book, bookbits);
            bits += bookbits;
            while (band < maxband && coder->book[band] == book) { band++; bookcnt++; }
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
    int cnt, bits = 0, lastsf = coder->global_gain, lastis = 0, lastpns = coder->global_gain - SF_PNS_OFFSET, initpns = 1;
    for (cnt = 0; cnt < coder->bandcnt; cnt++) {
        int book = coder->book[cnt];
        if (book == HCB_INTENSITY || book == HCB_INTENSITY2) {
            int diff = clamp_sf_diff(coder->sf[cnt] - lastis);
            bits += book12[SF_DELTA + diff].len; lastis += diff;
            if (write) PutBit(stream, book12[SF_DELTA + diff].data, book12[SF_DELTA + diff].len);
        } else if (book == HCB_PNS) {
            int diff = coder->sf[cnt] - lastpns;
            if (initpns) { initpns = 0; bits += 9; lastpns += diff; if (write) PutBit(stream, diff + 256, 9); continue; }
            diff = clamp_sf_diff(diff); bits += book12[SF_DELTA + diff].len; lastpns += diff;
            if (write) PutBit(stream, book12[SF_DELTA + diff].data, book12[SF_DELTA + diff].len);
        } else if (book != HCB_ZERO && book != HCB_NONE) {
            int diff = clamp_sf_diff(coder->sf[cnt] - lastsf);
            bits += book12[SF_DELTA + diff].len; lastsf += diff;
            if (write) PutBit(stream, book12[SF_DELTA + diff].data, book12[SF_DELTA + diff].len);
        }
    }
    return bits;
}
