/****************************************************************************
    Huffman coding optimized for Ingenic T31/T40 (Guided Hybrid A+B)

    Copyright (C) 2017 Krzysztof Nikiel
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
static inline int clz(unsigned int x)
{
    unsigned long index;
    if (_BitScanReverse(&index, x))
        return 31 - (int)index;
    return 32;
}
#else
#define clz(x) __builtin_clz(x)
#endif

static inline int get_book_limit(int bnum)
{
    switch(bnum) {
        case 1: case 2: return 1;
        case 3: case 4: return 2;
        case 5: case 6: return 4;
        case 7: case 8: return 7;
        case 9: case 10: return 12;
        case 11: return MAX_HUFF_ESC_VAL;
        default: return 0;
    }
}

static int escape(int x, int *code)
{
    int preflen = 0;
    int base = 32;

    if (x > MAX_HUFF_ESC_VAL)
    {
        fprintf(stderr, "%s(%d): x_quant > %d\n", __FILE__, __LINE__, MAX_HUFF_ESC_VAL);
        return 0;
    }

    *code = 0;
    while (base <= x)
    {
        base <<= 1;
        *code <<= 1;
        *code |= 1;
        preflen++;
    }
    base >>= 1;
    *code <<= 1;
    *code <<= (preflen + 4);
    *code |= (x - base);

    return (preflen << 1) + 5;
}

static inline int escape_len(int x)
{
    if (x < 32) return 5;
    int preflen = 31 - clz(x) - 4;
    return (preflen << 1) + 5;
}

static int huffcode(const int * __restrict qs,
                    int len,
                    int bnum,
                    CoderInfo *coder)
{
    int bits = 0;
    int ofs;
    const uint16_t offset = huff_offsets[bnum];
    const uint8_t * __restrict blut = huff_len_flat + offset;

    if (coder) {
        int datacnt = coder->datacnt;
        const uint16_t * __restrict dlut = huff_data_flat + offset;
        switch (bnum) {
        case 1: case 2:
            for(ofs = 0; ofs < len; ofs += 4) {
                int idx = 27 * qs[ofs] + 9 * qs[ofs+1] + 3 * qs[ofs+2] + qs[ofs+3] + 40;
                coder->s[datacnt].data = dlut[idx];
                coder->s[datacnt++].len = blut[idx];
                bits += blut[idx];
            }
            break;
        case 3: case 4:
            for(ofs = 0; ofs < len; ofs += 4) {
                int q0 = qs[ofs], q1 = qs[ofs+1], q2 = qs[ofs+2], q3 = qs[ofs+3];
                int aq0 = abs(q0), aq1 = abs(q1), aq2 = abs(q2), aq3 = abs(q3);
                int idx = 27 * aq0 + 9 * aq1 + 3 * aq2 + aq3;
                int blen = blut[idx];
                int data = dlut[idx];
                if (q0) { blen++; data <<= 1; if (q0 < 0) data |= 1; }
                if (q1) { blen++; data <<= 1; if (q1 < 0) data |= 1; }
                if (q2) { blen++; data <<= 1; if (q2 < 0) data |= 1; }
                if (q3) { blen++; data <<= 1; if (q3 < 0) data |= 1; }
                coder->s[datacnt].data = data;
                coder->s[datacnt++].len = blen;
                bits += blen;
            }
            break;
        case 5: case 6:
            for(ofs = 0; ofs < len; ofs += 2) {
                int idx = 9 * qs[ofs] + qs[ofs+1] + 40;
                coder->s[datacnt].data = dlut[idx];
                coder->s[datacnt++].len = blut[idx];
                bits += blut[idx];
            }
            break;
        case 7: case 8: case 9: case 10:
            {
                int mult = (bnum <= 8) ? 8 : 13;
                for(ofs = 0; ofs < len; ofs += 2) {
                    int q0 = qs[ofs], q1 = qs[ofs+1];
                    int aq0 = abs(q0), aq1 = abs(q1);
                    int idx = mult * aq0 + aq1;
                    int blen = blut[idx];
                    int data = dlut[idx];
                    if (q0) { blen++; data <<= 1; if (q0 < 0) data |= 1; }
                    if (q1) { blen++; data <<= 1; if (q1 < 0) data |= 1; }
                    coder->s[datacnt].data = data;
                    coder->s[datacnt++].len = blen;
                    bits += blen;
                }
            }
            break;
        case 11:
            for(ofs = 0; ofs < len; ofs += 2) {
                int q0 = qs[ofs], q1 = qs[ofs+1];
                int aq0 = abs(q0), aq1 = abs(q1);
                int x0 = (aq0 > 16) ? 16 : aq0;
                int x1 = (aq1 > 16) ? 16 : aq1;
                int idx = 17 * x0 + x1;
                int blen = blut[idx];
                int data = dlut[idx];
                if (q0) { blen++; data <<= 1; if (q0 < 0) data |= 1; }
                if (q1) { blen++; data <<= 1; if (q1 < 0) data |= 1; }
                coder->s[datacnt].data = data;
                coder->s[datacnt++].len = blen;
                bits += blen;
                if (aq0 >= 16) {
                    int edata = 0;
                    int elen = escape(aq0, &edata);
                    coder->s[datacnt].data = edata;
                    coder->s[datacnt++].len = elen;
                    bits += elen;
                }
                if (aq1 >= 16) {
                    int edata = 0;
                    int elen = escape(aq1, &edata);
                    coder->s[datacnt].data = edata;
                    coder->s[datacnt++].len = elen;
                    bits += elen;
                }
            }
            break;
        default: return -1;
        }
        coder->datacnt = datacnt;
    } else {
        switch (bnum) {
        case 1: case 2:
            for(ofs = 0; ofs < len; ofs += 4)
                bits += blut[27 * qs[ofs] + 9 * qs[ofs+1] + 3 * qs[ofs+2] + qs[ofs+3] + 40];
            break;
        case 3: case 4:
            for(ofs = 0; ofs < len; ofs += 4) {
                int q0 = qs[ofs], q1 = qs[ofs+1], q2 = qs[ofs+2], q3 = qs[ofs+3];
                bits += blut[27 * abs(q0) + 9 * abs(q1) + 3 * abs(q2) + abs(q3)];
                if (q0) bits++;
                if (q1) bits++;
                if (q2) bits++;
                if (q3) bits++;
            }
            break;
        case 5: case 6:
            for(ofs = 0; ofs < len; ofs += 2)
                bits += blut[9 * qs[ofs] + qs[ofs+1] + 40];
            break;
        case 7: case 8:
            for(ofs = 0; ofs < len; ofs += 2) {
                int q0 = qs[ofs], q1 = qs[ofs+1];
                bits += blut[8 * abs(q0) + abs(q1)];
                if (q0) bits++;
                if (q1) bits++;
            }
            break;
        case 9: case 10:
            for(ofs = 0; ofs < len; ofs += 2) {
                int q0 = qs[ofs], q1 = qs[ofs+1];
                bits += blut[13 * abs(q0) + abs(q1)];
                if (q0) bits++;
                if (q1) bits++;
            }
            break;
        case 11:
            for(ofs = 0; ofs < len; ofs += 2) {
                int aq0 = abs(qs[ofs]), aq1 = abs(qs[ofs+1]);
                bits += blut[17 * ((aq0 > 16) ? 16 : aq0) + ((aq1 > 16) ? 16 : aq1)];
                if (qs[ofs]) bits++;
                if (qs[ofs+1]) bits++;
                if (aq0 >= 16) bits += escape_len(aq0);
                if (aq1 >= 16) bits += escape_len(aq1);
            }
            break;
        }
    }
    return bits;
}

static inline int get_maxq(const int * __restrict qs, int len)
{
    int maxq = 0;
    int i;
    for (i = 0; i < len; i++) {
        int q = abs(qs[i]);
        if (q > maxq) maxq = q;
    }
    return maxq;
}

int huffbook(CoderInfo *coder,
             int *qs /* quantized spectrum */,
             int len)
{
    int maxq = get_maxq(qs, len);
    int bookmin = HCB_ZERO;
    int prev_book = -1;

    if (coder->bandcnt > 0 && (coder->bandcnt % coder->sfbn) != 0) {
        prev_book = coder->book[coder->bandcnt - 1];
    }

    if (maxq < 1) {
        bookmin = HCB_ZERO;
    } else {
        int lenmin = 1000000;
        int candidates[3];
        int num_cands = 0;

        if (prev_book >= 1 && prev_book <= 11 && maxq <= get_book_limit(prev_book)) {
            bookmin = prev_book;
            lenmin = huffcode(qs, len, prev_book, NULL) - 4;
        }

        if (maxq < 2) { candidates[num_cands++] = 1; candidates[num_cands++] = 2; }
        else if (maxq < 3) { candidates[num_cands++] = 3; candidates[num_cands++] = 4; }
        else if (maxq < 5) { candidates[num_cands++] = 5; candidates[num_cands++] = 6; }
        else if (maxq < 8) { candidates[num_cands++] = 7; candidates[num_cands++] = 8; }
        else if (maxq < 13) { candidates[num_cands++] = 9; candidates[num_cands++] = 10; }
        else { candidates[num_cands++] = 11; }

        for (int i = 0; i < num_cands; i++) {
            int bnum = candidates[i];
            if (bnum == prev_book) continue;
            int cost = huffcode(qs, len, bnum, NULL);
            if (cost < lenmin) {
                lenmin = cost;
                bookmin = bnum;
            }
        }
        if (bookmin == HCB_ZERO) bookmin = 11;
    }

    if (bookmin > HCB_ZERO)
        huffcode(qs, len, bookmin, coder);

    coder->book[coder->bandcnt] = bookmin;
    return 0;
}

int writebooks(CoderInfo *coder, BitStream *stream, int write)
{
    int bits = 0;
    int maxcnt, cntbits;
    int group;
    int bookbits = 4;

    if (coder->block_type == ONLY_SHORT_WINDOW){
        maxcnt = 7;
        cntbits = 3;
    } else {
        maxcnt = 31;
        cntbits = 5;
    }

    for (group = 0; group < coder->groups.n; group++)
    {
        int band = group * coder->sfbn;
        int maxband = band + coder->sfbn;

        while (band < maxband)
        {
            int book = coder->book[band++];
            int bookcnt = 1;
            if (write) PutBit(stream, book, bookbits);
            bits += bookbits;

            if (band < maxband)
            {
                while (book == coder->book[band])
                {
                    band++;
                    bookcnt++;
                    if (band >= maxband) break;
                }
            }

            while (bookcnt >= maxcnt)
            {
                if (write) PutBit(stream, maxcnt, cntbits);
                bits += cntbits;
                bookcnt -= maxcnt;
            }
            if (write) PutBit(stream, bookcnt, cntbits);
            bits += cntbits;
        }
    }
    return bits;
}

int writesf(CoderInfo *coder, BitStream *stream, int write)
{
    int cnt, bits = 0;
    int lastsf = coder->global_gain;
    int lastis = 0, lastpns = coder->global_gain - SF_PNS_OFFSET;
    int initpns = 1;

    for (cnt = 0; cnt < coder->bandcnt; cnt++)
    {
        int book = coder->book[cnt];
        if ((book == HCB_INTENSITY) || (book== HCB_INTENSITY2))
        {
            int diff = clamp_sf_diff(coder->sf[cnt] - lastis);
            bits += book12[SF_DELTA + diff].len;
            lastis += diff;
            if (write) PutBit(stream, book12[SF_DELTA + diff].data, book12[SF_DELTA + diff].len);
        }
        else if (book == HCB_PNS)
        {
            int diff = coder->sf[cnt] - lastpns;
            if (initpns)
            {
                initpns = 0;
                bits += 9;
                lastpns += diff;
                if (write) PutBit(stream, diff + 256, 9);
                continue;
            }
            diff = clamp_sf_diff(diff);
            bits += book12[SF_DELTA + diff].len;
            lastpns += diff;
            if (write) PutBit(stream, book12[SF_DELTA + diff].data, book12[SF_DELTA + diff].len);
        }
        else if ((book != HCB_ZERO) && (book != HCB_NONE))
        {
            int diff = clamp_sf_diff(coder->sf[cnt] - lastsf);
            bits += book12[SF_DELTA + diff].len;
            lastsf += diff;
            if (write) PutBit(stream, book12[SF_DELTA + diff].data, book12[SF_DELTA + diff].len);
        }
    }
    return bits;
}
