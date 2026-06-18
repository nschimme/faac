/****************************************************************************
    Huffman coding optimized for Ingenic T31/T40 (Hybrid A+B)

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
    int datacnt = (coder) ? coder->datacnt : 0;
    const uint16_t offset = huff_offsets[bnum];
    const uint8_t * __restrict blut = huff_len_flat + offset;
    const uint16_t * __restrict dlut = huff_data_flat + offset;

    switch (bnum)
    {
    case 1:
    case 2:
        for(ofs = 0; ofs < len; ofs += 4)
        {
            int idx = 27 * qs[ofs] + 9 * qs[ofs+1] + 3 * qs[ofs+2] + qs[ofs+3] + 40;
            int blen = blut[idx];
            if (coder)
            {
                coder->s[datacnt].data = dlut[idx];
                coder->s[datacnt++].len = blen;
            }
            bits += blen;
        }
        break;
    case 3:
    case 4:
        for(ofs = 0; ofs < len; ofs += 4)
        {
            int q0 = qs[ofs], q1 = qs[ofs+1], q2 = qs[ofs+2], q3 = qs[ofs+3];
            int idx = 27 * abs(q0) + 9 * abs(q1) + 3 * abs(q2) + abs(q3);
            int blen = blut[idx];

            if (q0) blen++;
            if (q1) blen++;
            if (q2) blen++;
            if (q3) blen++;

            if (coder)
            {
                int data = dlut[idx];
                if (q0) { data <<= 1; if (q0 < 0) data |= 1; }
                if (q1) { data <<= 1; if (q1 < 0) data |= 1; }
                if (q2) { data <<= 1; if (q2 < 0) data |= 1; }
                if (q3) { data <<= 1; if (q3 < 0) data |= 1; }
                coder->s[datacnt].data = data;
                coder->s[datacnt++].len = blen;
            }
            bits += blen;
        }
        break;
    case 5:
    case 6:
        for(ofs = 0; ofs < len; ofs += 2)
        {
            int idx = 9 * qs[ofs] + qs[ofs+1] + 40;
            int blen = blut[idx];
            if (coder)
            {
                coder->s[datacnt].data = dlut[idx];
                coder->s[datacnt++].len = blen;
            }
            bits += blen;
        }
        break;
    case 7:
    case 8:
    case 9:
    case 10:
        {
            int mult = (bnum <= 8) ? 8 : 13;
            for(ofs = 0; ofs < len; ofs += 2)
            {
                int q0 = qs[ofs], q1 = qs[ofs+1];
                int idx = mult * abs(q0) + abs(q1);
                int blen = blut[idx];

                if (q0) blen++;
                if (q1) blen++;

                if (coder)
                {
                    int data = dlut[idx];
                    if (q0) { data <<= 1; if (q0 < 0) data |= 1; }
                    if (q1) { data <<= 1; if (q1 < 0) data |= 1; }
                    coder->s[datacnt].data = data;
                    coder->s[datacnt++].len = blen;
                }
                bits += blen;
            }
        }
        break;
    case 11:
        for(ofs = 0; ofs < len; ofs += 2)
        {
            int q0 = qs[ofs], q1 = qs[ofs+1];
            int aq0 = abs(q0), aq1 = abs(q1);
            int x0 = (aq0 > 16) ? 16 : aq0;
            int x1 = (aq1 > 16) ? 16 : aq1;
            int idx = 17 * x0 + x1;
            int blen = blut[idx];

            if (q0) blen++;
            if (q1) blen++;

            if (coder)
            {
                int data = dlut[idx];
                if (q0) { data <<= 1; if (q0 < 0) data |= 1; }
                if (q1) { data <<= 1; if (q1 < 0) data |= 1; }
                coder->s[datacnt].data = data;
                coder->s[datacnt++].len = blen;
            }
            bits += blen;

            if (aq0 >= 16)
            {
                int edata = 0;
                int elen = escape(aq0, &edata);
                if (coder)
                {
                    coder->s[datacnt].data = edata;
                    coder->s[datacnt++].len = elen;
                }
                bits += elen;
            }

            if (aq1 >= 16)
            {
                int edata = 0;
                int elen = escape(aq1, &edata);
                if (coder)
                {
                    coder->s[datacnt].data = edata;
                    coder->s[datacnt++].len = elen;
                }
                bits += elen;
            }
        }
        break;
    default:
        return -1;
    }

    if (coder)
        coder->datacnt = datacnt;

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
    int lenmin = 1000000;
    int bnum;
    int prev_book = -1;

    if (coder->bandcnt > 0 && (coder->bandcnt % coder->sfbn) != 0) {
        prev_book = coder->book[coder->bandcnt - 1];
    }

    if (maxq < 1) {
        bookmin = HCB_ZERO;
    } else {
        if (prev_book >= 1 && prev_book <= 11 && maxq <= get_book_limit(prev_book)) {
            bookmin = prev_book;
            lenmin = huffcode(qs, len, prev_book, NULL) - 4;
        }

        for (bnum = 1; bnum <= 11; bnum++) {
            if (bnum == prev_book) continue;
            if (maxq <= get_book_limit(bnum)) {
                int cost = huffcode(qs, len, bnum, NULL);
                if (cost < lenmin) {
                    lenmin = cost;
                    bookmin = bnum;
                }
            }
        }
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
            if (write) {
                PutBit(stream, book, bookbits);
            }
            bits += bookbits;

            if (band < maxband)
            {
                while (book == coder->book[band])
                {
                    band++;
                    bookcnt++;
                    if (band >= maxband)
                        break;
                }
            }

            while (bookcnt >= maxcnt)
            {
                if (write)
                    PutBit(stream, maxcnt, cntbits);
                bits += cntbits;
                bookcnt -= maxcnt;
            }
            if (write)
                PutBit(stream, bookcnt, cntbits);
            bits += cntbits;
        }
    }

    return bits;
}

int writesf(CoderInfo *coder, BitStream *stream, int write)
{
    int cnt;
    int bits = 0;
    int diff, length;
    int lastsf;
    int lastis;
    int lastpns;
    int initpns = 1;

    lastsf = coder->global_gain;
    lastis = 0;
    lastpns = coder->global_gain - SF_PNS_OFFSET;

    for (cnt = 0; cnt < coder->bandcnt; cnt++)
    {
        int book = coder->book[cnt];

        if ((book == HCB_INTENSITY) || (book== HCB_INTENSITY2))
        {
            diff = coder->sf[cnt] - lastis;
            diff = clamp_sf_diff(diff);
            length = book12[SF_DELTA + diff].len;
            bits += length;
            lastis += diff;
            if (write)
                PutBit(stream, book12[SF_DELTA + diff].data, length);
        }
        else if (book == HCB_PNS)
        {
            diff = coder->sf[cnt] - lastpns;
            if (initpns)
            {
                initpns = 0;
                length = 9;
                bits += length;
                lastpns += diff;
                if (write)
                    PutBit(stream, diff + 256, length);
                continue;
            }
            diff = clamp_sf_diff(diff);
            length = book12[SF_DELTA + diff].len;
            bits += length;
            lastpns += diff;
            if (write)
                PutBit(stream, book12[SF_DELTA + diff].data, length);
        }
        else if ((book != HCB_ZERO) && (book != HCB_NONE))
        {
            diff = coder->sf[cnt] - lastsf;
            diff = clamp_sf_diff(diff);
            length = book12[SF_DELTA + diff].len;
            bits += length;
            lastsf += diff;
            if (write)
                PutBit(stream, book12[SF_DELTA + diff].data, length);
        }
    }
    return bits;
}