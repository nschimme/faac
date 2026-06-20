#include <stdio.h>
#include <stdlib.h>
#include "coder.h"
#include "huffdata.h"
#include "huff2.h"
#include "bitstream.h"
#include "huff_lut.h"

static int escape(int x, int *code) {
    int preflen = 0; int base = 32;
    if (x > MAX_HUFF_ESC_VAL) return 0;
    if (code) {
        *code = 0; while (base <= x) { base <<= 1; *code <<= 1; *code |= 1; preflen++; }
        base >>= 1; *code <<= 1; *code <<= (preflen + 4); *code |= (x - base);
    } else { while (base <= x) { base <<= 1; preflen++; } }
    return (preflen << 1) + 5;
}

static int huffcode_estimate(int *qs, int len, int bnum) {
    static const uint8_t * const hlen_map[12] = {NULL, book01_len, book02_len, book03_len, book04_len, book05_len, book06_len, book07_len, book08_len, book09_len, book10_len, book11_len};
    const uint8_t * __restrict hlen = hlen_map[bnum];
    int bits = 0;
    switch (bnum) {
    case 1: case 2:
        for(int ofs = 0; ofs < len; ofs += 4) bits += hlen[27 * qs[ofs] + 9 * qs[ofs+1] + 3 * qs[ofs+2] + qs[ofs+3] + 40];
        break;
    case 3: case 4:
        for(int ofs = 0; ofs < len; ofs += 4) {
            bits += hlen[27 * abs(qs[ofs]) + 9 * abs(qs[ofs+1]) + 3 * abs(qs[ofs+2]) + abs(qs[ofs+3])];
            bits += (qs[ofs] != 0) + (qs[ofs+1] != 0) + (qs[ofs+2] != 0) + (qs[ofs+3] != 0);
        } break;
    case 5: case 6:
        for(int ofs = 0; ofs < len; ofs += 2) bits += hlen[9 * qs[ofs] + qs[ofs+1] + 40];
        break;
    case 7: case 8:
        for(int ofs = 0; ofs < len; ofs += 2) {
            bits += hlen[8 * abs(qs[ofs]) + abs(qs[ofs+1])];
            bits += (qs[ofs] != 0) + (qs[ofs+1] != 0);
        } break;
    case 9: case 10:
        for(int ofs = 0; ofs < len; ofs += 2) {
            bits += hlen[13 * abs(qs[ofs]) + abs(qs[ofs+1])];
            bits += (qs[ofs] != 0) + (qs[ofs+1] != 0);
        } break;
    case HCB_ESC:
        for(int ofs = 0; ofs < len; ofs += 2) {
            int x0 = abs(qs[ofs]), x1 = abs(qs[ofs+1]);
            int sx0 = x0 > 16 ? 16 : x0, sx1 = x1 > 16 ? 16 : x1;
            bits += hlen[17 * sx0 + sx1] + (qs[ofs] != 0) + (qs[ofs+1] != 0);
            if (x0 >= 16) bits += escape(x0, NULL);
            if (x1 >= 16) bits += escape(x1, NULL);
        } break;
    default: return -1;
    }
    return bits;
}

static int huffcode_write(int *qs, int len, int bnum, CoderInfo *coder) {
    static hcode16_t * const hmap[12] = {NULL, book01, book02, book03, book04, book05, book06, book07, book08, book09, book10, book11};
    int bits = 0; int datacnt = coder->datacnt;
    hcode16_t * __restrict book = hmap[bnum];
    switch (bnum) {
    case 1: case 2:
        for(int ofs = 0; ofs < len; ofs += 4) {
            int idx = 27 * qs[ofs] + 9 * qs[ofs+1] + 3 * qs[ofs+2] + qs[ofs+3] + 40;
            coder->s[datacnt].data = book[idx].data;
            coder->s[datacnt++].len = book[idx].len;
            bits += book[idx].len;
        } break;
    case 3: case 4:
        for(int ofs = 0; ofs < len; ofs += 4) {
            int idx = 27 * abs(qs[ofs]) + 9 * abs(qs[ofs+1]) + 3 * abs(qs[ofs+2]) + abs(qs[ofs+3]);
            int data = book[idx].data; int blen = book[idx].len;
            for(int i = 0; i < 4; i++) if(qs[ofs+i]) { blen++; data <<= 1; if (qs[ofs+i] < 0) data |= 1; }
            coder->s[datacnt].data = data;
            coder->s[datacnt++].len = blen;
            bits += blen;
        } break;
    case 5: case 6:
        for(int ofs = 0; ofs < len; ofs += 2) {
            int idx = 9 * qs[ofs] + qs[ofs+1] + 40;
            coder->s[datacnt].data = book[idx].data;
            coder->s[datacnt++].len = book[idx].len;
            bits += book[idx].len;
        } break;
    case 7: case 8:
        for(int ofs = 0; ofs < len; ofs += 2) {
            int idx = 8 * abs(qs[ofs]) + abs(qs[ofs+1]);
            int data = book[idx].data; int blen = book[idx].len;
            for(int i = 0; i < 2; i++) if(qs[ofs+i]) { blen++; data <<= 1; if (qs[ofs+i] < 0) data |= 1; }
            coder->s[datacnt].data = data;
            coder->s[datacnt++].len = blen;
            bits += blen;
        } break;
    case 9: case 10:
        for(int ofs = 0; ofs < len; ofs += 2) {
            int idx = 13 * abs(qs[ofs]) + abs(qs[ofs+1]);
            int data = book[idx].data; int blen = book[idx].len;
            for(int i = 0; i < 2; i++) if(qs[ofs+i]) { blen++; data <<= 1; if (qs[ofs+i] < 0) data |= 1; }
            coder->s[datacnt].data = data;
            coder->s[datacnt++].len = blen;
            bits += blen;
        } break;
    case HCB_ESC:
        for(int ofs = 0; ofs < len; ofs += 2) {
            int q0 = qs[ofs], q1 = qs[ofs+1]; int x0 = abs(q0), x1 = abs(q1);
            int sx0 = x0 > 16 ? 16 : x0, sx1 = x1 > 16 ? 16 : x1;
            int idx = 17 * sx0 + sx1; int data = book[idx].data; int blen = book[idx].len;
            if(q0) { blen++; data <<= 1; if (q0 < 0) data |= 1; } if(q1) { blen++; data <<= 1; if (q1 < 0) data |= 1; }
            coder->s[datacnt].data = data; coder->s[datacnt++].len = blen; bits += blen;
            if (x0 >= 16) { int id = 0; int elen = escape(x0, &id); coder->s[datacnt].data = id; coder->s[datacnt++].len = elen; bits += elen; }
            if (x1 >= 16) { int id = 0; int elen = escape(x1, &id); coder->s[datacnt].data = id; coder->s[datacnt++].len = elen; bits += elen; }
        } break;
    default: return -1;
    }
    coder->datacnt = datacnt;
    return bits;
}

int huffbook(CoderInfo *coder, int *qs, int len) {
    int maxq = 0; int bookmin; int lenmin = 0; const int * __restrict p_qs = qs;
    int last_book = (coder->bandcnt > 0) ? coder->book[coder->bandcnt - 1] : HCB_NONE;
    for (int i = 0; i < len; i++) {
        int q = abs(*p_qs++);
        if (maxq < q) { maxq = q; if (maxq >= 16) { maxq = 16; break; } }
    }
#define BOOKMIN(n) bookmin=n; lenmin=huffcode_estimate(qs,len,bookmin); { int lnext = huffcode_estimate(qs,len,bookmin+1); if(lnext>=0 && lnext<lenmin) { bookmin++; lenmin=lnext; } }
    if (maxq < 1) { bookmin = HCB_ZERO; lenmin = 0; }
    else if (maxq < 2) { BOOKMIN(1); }
    else if (maxq < 3) { BOOKMIN(3); }
    else if (maxq < 5) { BOOKMIN(5); }
    else if (maxq < 8) { BOOKMIN(7); }
    else if (maxq < 13) { BOOKMIN(9); }
    else { bookmin = HCB_ESC; lenmin = huffcode_estimate(qs, len, HCB_ESC); }

    if (last_book != HCB_NONE && last_book != HCB_ZERO && last_book != HCB_PNS &&
        last_book != HCB_INTENSITY && last_book != HCB_INTENSITY2 &&
        last_book != bookmin) {
        int last_bits = huffcode_estimate(qs, len, last_book);
        if (last_bits >= 0 && last_bits <= lenmin + 7) { bookmin = last_book; }
    }
    if (bookmin > HCB_ZERO) huffcode_write(qs, len, bookmin, coder);
    coder->book[coder->bandcnt] = bookmin;
    return 0;
}

int writebooks(CoderInfo *coder, BitStream *stream, int write) {
    int bits = 0; int maxcnt, cntbits; int bookbits = 4;
    if (coder->block_type == ONLY_SHORT_WINDOW){ maxcnt = 7; cntbits = 3; } else { maxcnt = 31; cntbits = 5; }
    for (int group = 0; group < coder->groups.n; group++) {
        int band = group * coder->sfbn; int maxband = band + coder->sfbn;
        while (band < maxband) {
            int book = coder->book[band++]; int bookcnt = 1;
            if (write) { PutBit(stream, (unsigned long)book, bookbits); } bits += bookbits;
            while (band < maxband && coder->book[band] == book) { band++; bookcnt++; }
            while (bookcnt >= maxcnt) { if (write) { PutBit(stream, (unsigned long)maxcnt, cntbits); } bits += cntbits; bookcnt -= maxcnt; }
            if (write) { PutBit(stream, (unsigned long)bookcnt, cntbits); } bits += cntbits;
        }
    } return bits;
}

int writesf(CoderInfo *coder, BitStream *stream, int write) {
    int bits = 0; int initpns = 1;
    int lastsf = coder->global_gain; int lastis = 0; int lastpns = coder->global_gain - SF_PNS_OFFSET;
    for (int cnt = 0; cnt < coder->bandcnt; cnt++) {
        int book = coder->book[cnt];
        if ((book == HCB_INTENSITY) || (book== HCB_INTENSITY2)) {
            int diff = clamp_sf_diff(coder->sf[cnt] - lastis);
            bits += book12[SF_DELTA + diff].len; lastis += diff;
            if (write) PutBit(stream, book12[SF_DELTA + diff].data, book12[SF_DELTA + diff].len);
        } else if (book == HCB_PNS) {
            int diff = coder->sf[cnt] - lastpns;
            if (initpns) { initpns = 0; bits += 9; lastpns += diff; if (write) PutBit(stream, (unsigned long)(diff + 256), 9); continue; }
            diff = clamp_sf_diff(diff); bits += book12[SF_DELTA + diff].len; lastpns += diff;
            if (write) PutBit(stream, book12[SF_DELTA + diff].data, book12[SF_DELTA + diff].len);
        } else if ((book != HCB_ZERO) && (book != HCB_NONE)) {
            int diff = clamp_sf_diff(coder->sf[cnt] - lastsf);
            bits += book12[SF_DELTA + diff].len; lastsf += diff;
            if (write) PutBit(stream, book12[SF_DELTA + diff].data, book12[SF_DELTA + diff].len);
        }
    } return bits;
}
