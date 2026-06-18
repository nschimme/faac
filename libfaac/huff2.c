/****************************************************************************
    Huffman coding

    Copyright (C) 2017 Krzysztof Nikiel

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
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

    // separator
    *code <<= 1;

    *code <<= (preflen + 4);
    *code |= (x - base);

    return (preflen << 1) + 5;
}

#define arrlen(array) (sizeof(array) / sizeof(*array))

static int huffcode(int *qs /* quantized spectrum */,
                    int len,
                    int bnum,
                    CoderInfo *coder)
{
    static hcode16_t * const hmap[12] = {0, book01, book02, book03, book04,
      book05, book06, book07, book08, book09, book10, book11};
    hcode16_t *book;
    int cnt;
    int bits = 0, blen;
    int ofs, *qp;
    int data = 0;
    int idx;
    int datacnt;

    if (coder)
        datacnt = coder->datacnt;
    else
        datacnt = 0;

    book = hmap[bnum];
    switch (bnum)
    {
    case 1:
    case 2:
        for(ofs = 0; ofs < len; ofs += 4)
        {
            qp = qs+ofs;
            idx = 27 * qp[0] + 9 * qp[1] + 3 * qp[2] + qp[3] + 40;
            if (idx < 0 || idx >= arrlen(book01))
            {
                return -1;
            }
            blen = book[idx].len;
            if (coder)
            {
                data = book[idx].data;
                coder->s[datacnt].data = data;
                coder->s[datacnt++].len = blen;
            }
            bits += blen;
        }
        break;
    case 3:
    case 4:
        for(ofs = 0; ofs < len; ofs += 4)
        {
            qp = qs+ofs;
            idx = 27 * abs(qp[0]) + 9 * abs(qp[1]) + 3 * abs(qp[2]) + abs(qp[3]);
            if (idx < 0 || idx >= arrlen(book03))
            {
                return -1;
            }
            blen = book[idx].len;
            if (!coder)
            {
                // add sign bits
                for(cnt = 0; cnt < 4; cnt++)
                    if(qp[cnt])
                        blen++;
            }
            else
            {
                data = book[idx].data;
                // add sign bits
                for(cnt = 0; cnt < 4; cnt++)
                {
                    if(qp[cnt])
                    {
                        blen++;
                        data <<= 1;
                        if (qp[cnt] < 0)
                            data |= 1;
                    }
                }
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
            qp = qs+ofs;
            idx = 9 * qp[0] + qp[1] + 40;
            if (idx < 0 || idx >= arrlen(book05))
            {
                return -1;
            }
            blen = book[idx].len;
            if (coder)
            {
                data = book[idx].data;
                coder->s[datacnt].data = data;
                coder->s[datacnt++].len = blen;
            }
            bits += blen;
        }
        break;
    case 7:
    case 8:
        for(ofs = 0; ofs < len; ofs += 2)
        {
            qp = qs+ofs;
            idx = 8 * abs(qp[0]) + abs(qp[1]);
            if (idx < 0 || idx >= arrlen(book07))
            {
                return -1;
            }
            blen = book[idx].len;
            if (!coder)
            {
                for(cnt = 0; cnt < 2; cnt++)
                    if(qp[cnt])
                        blen++;
            }
            else
            {
                data = book[idx].data;
                for(cnt = 0; cnt < 2; cnt++)
                {
                    if(qp[cnt])
                    {
                        blen++;
                        data <<= 1;
                        if (qp[cnt] < 0)
                            data |= 1;
                    }
                }
                coder->s[datacnt].data = data;
                coder->s[datacnt++].len = blen;
            }
            bits += blen;
        }
        break;
    case 9:
    case 10:
        for(ofs = 0; ofs < len; ofs += 2)
        {
            qp = qs+ofs;
            idx = 13 * abs(qp[0]) + abs(qp[1]);
            if (idx < 0 || idx >= arrlen(book09))
            {
                return -1;
            }
            blen = book[idx].len;
            if (!coder)
            {
                for(cnt = 0; cnt < 2; cnt++)
                    if(qp[cnt])
                        blen++;
            }
            else
            {
                data = book[idx].data;
                for(cnt = 0; cnt < 2; cnt++)
                {
                    if(qp[cnt])
                    {
                        blen++;
                        data <<= 1;
                        if (qp[cnt] < 0)
                            data |= 1;
                    }
                }
                coder->s[datacnt].data = data;
                coder->s[datacnt++].len = blen;
            }
            bits += blen;
        }
        break;
    case HCB_ESC:
        for(ofs = 0; ofs < len; ofs += 2)
        {
            int x0, x1;

            qp = qs+ofs;

            x0 = abs(qp[0]);
            x1 = abs(qp[1]);
            if (x0 > 16)
                x0 = 16;
            if (x1 > 16)
                x1 = 16;
            idx = 17 * x0 + x1;
            if (idx < 0 || idx >= arrlen(book11))
            {
                return -1;
            }

            blen = book[idx].len;
            if (!coder)
            {
                for(cnt = 0; cnt < 2; cnt++)
                    if(qp[cnt])
                        blen++;
            }
            else
            {
                data = book[idx].data;
                for(cnt = 0; cnt < 2; cnt++)
                {
                    if(qp[cnt])
                    {
                        blen++;
                        data <<= 1;
                        if (qp[cnt] < 0)
                            data |= 1;
                    }
                }
                coder->s[datacnt].data = data;
                coder->s[datacnt++].len = blen;
            }
            bits += blen;

            if (x0 >= 16)
            {
                blen = escape(abs(qp[0]), &data);
                if (coder)
                {
                    coder->s[datacnt].data = data;
                    coder->s[datacnt++].len = blen;
                }
                bits += blen;
            }

            if (x1 >= 16)
            {
                blen = escape(abs(qp[1]), &data);
                if (coder)
                {
                    coder->s[datacnt].data = data;
                    coder->s[datacnt++].len = blen;
                }
                bits += blen;
            }
        }
        break;
    default:
        fprintf(stderr, "%s(%d) book %d out of range\n", __FILE__, __LINE__, bnum);
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

static inline int escape_len(int x)
{
    if (x < 32) return 5;
    /* Bits = 2 * (floor(log2(x)) - 4) + 5 */
    int preflen = 31 - clz(x) - 4;
    return (preflen << 1) + 5;
}

static int calc_bit_cost(const int * __restrict qs, int len, int bnum)
{
    int bits = 0;
    int ofs;
    const uint8_t * __restrict lut = huff_len_lut[bnum];

    switch (bnum) {
    case 1:
    case 2:
        for (ofs = 0; ofs < len; ofs += 4) {
            int idx = 27 * qs[ofs] + 9 * qs[ofs+1] + 3 * qs[ofs+2] + qs[ofs+3] + 40;
            bits += lut[idx];
        }
        break;
    case 3:
    case 4:
        for (ofs = 0; ofs < len; ofs += 4) {
            int idx = 27 * abs(qs[ofs]) + 9 * abs(qs[ofs+1]) + 3 * abs(qs[ofs+2]) + abs(qs[ofs+3]);
            bits += lut[idx];
            bits += (qs[ofs] != 0);
            bits += (qs[ofs+1] != 0);
            bits += (qs[ofs+2] != 0);
            bits += (qs[ofs+3] != 0);
        }
        break;
    case 5:
    case 6:
        for (ofs = 0; ofs < len; ofs += 2) {
            int idx = 9 * qs[ofs] + qs[ofs+1] + 40;
            bits += lut[idx];
        }
        break;
    case 7:
    case 8:
        for (ofs = 0; ofs < len; ofs += 2) {
            int idx = 8 * abs(qs[ofs]) + abs(qs[ofs+1]);
            bits += lut[idx];
            bits += (qs[ofs] != 0);
            bits += (qs[ofs+1] != 0);
        }
        break;
    case 9:
    case 10:
        for (ofs = 0; ofs < len; ofs += 2) {
            int idx = 13 * abs(qs[ofs]) + abs(qs[ofs+1]);
            bits += lut[idx];
            bits += (qs[ofs] != 0);
            bits += (qs[ofs+1] != 0);
        }
        break;
    case 11:
        for (ofs = 0; ofs < len; ofs += 2) {
            int q0 = abs(qs[ofs]);
            int q1 = abs(qs[ofs+1]);
            int x0 = (q0 > 16) ? 16 : q0;
            int x1 = (q1 > 16) ? 16 : q1;
            int idx = 17 * x0 + x1;
            bits += lut[idx];
            bits += (qs[ofs] != 0);
            bits += (qs[ofs+1] != 0);
            if (q0 >= 16) bits += escape_len(q0);
            if (q1 >= 16) bits += escape_len(q1);
        }
        break;
    default:
        return 1000000;
    }
    return bits;
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

    /* Candidate A: Only consider the immediately preceding band within the same window group. */
    if (coder->bandcnt > 0 && (coder->bandcnt % coder->sfbn) != 0) {
        prev_book = coder->book[coder->bandcnt - 1];
    }

    if (maxq < 1) {
        bookmin = HCB_ZERO;
        lenmin = 0;
    } else {
        /* Optimization: Start by evaluating the previous book with Candidate A penalty */
        if (prev_book >= 1 && prev_book <= 11) {
            if (maxq <= get_book_limit(prev_book)) {
                bookmin = prev_book;
                /* Bits(CB_prev) <= Bits(CB_new_optimal) + 4  => Bits(CB_new_optimal) >= Bits(CB_prev) - 4
                 * We set the bar for new books to be at least 5 bits better than the previous one. */
                lenmin = calc_bit_cost(qs, len, prev_book) - 4;
            }
        }

        /* Evaluate all valid books to find if any is significantly better than bookmin */
        for (bnum = 1; bnum <= 11; bnum++) {
            if (bnum == prev_book) continue;
            if (maxq <= get_book_limit(bnum)) {
                int cost = calc_bit_cost(qs, len, bnum);
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
    /* Note: coder->bandcnt is incremented by the caller (qlevel in quantize.c) */

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

    /* qlevel() bounds every stored sf[] and global_gain to [0, SF_MAX_ABS], so
     * the running reconstruction below cannot leave the decoder's range. */
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
