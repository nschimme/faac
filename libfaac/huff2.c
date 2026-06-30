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
#include "coder.h"
#include "huffdata.h"
#include "huff2.h"
#include "bitstream.h"
#include "util.h"

/* Escape suffix for HCB_ESC: a magnitude |q| >= LAV_ESC is sent as the pair index
 * LAV_ESC (emitted by the caller) plus this suffix - a unary prefix of `preflen`
 * ones and a zero, then the low `preflen+4` bits of x. preflen counts how far x is
 * past the 16-window, so the suffix is 2*preflen+5 bits. */
static int escape(int x, int *code)
{
    int preflen;
    int base;

    if (x > MAX_HUFF_ESC_VAL)
    {
        fprintf(stderr, "%s(%d): x_quant > %d\n", __FILE__, __LINE__, MAX_HUFF_ESC_VAL);
        return 0;
    }

    preflen = 31 - CountLeadingZeros(x) - 4;
    base = 1 << (preflen + 4);

    *code = (1 << (preflen + 1)) - 2; /* preflen 1s and a 0 */
    *code <<= (preflen + 4);
    *code |= (x - base);

    return (preflen << 1) + 5;
}

#define arrlen(array) (sizeof(array) / sizeof(*array))

static hcode16_t * const hmap[12] = {0, book01, book02, book03, book04,
  book05, book06, book07, book08, book09, book10, book11};

/* Decoupled sizing logic for multiple bit-cost trials during quantization.
 * Uses branchless sign counting and precomputed radix constants. */
static int huffcode_size(int *qs, int len, int bnum)
{
    hcode16_t *book = hmap[bnum];
    int bits = 0;
    int ofs;

    switch (bnum)
    {
    case HCB_1:
    case HCB_2:
        for(ofs = 0; ofs < len; ofs += 4)
        {
            int idx = 27 * qs[ofs] + 9 * qs[ofs+1] + 3 * qs[ofs+2] + qs[ofs+3] + 40;
            bits += book[idx].len;
        }
        break;
    case HCB_3:
    case HCB_4:
        for(ofs = 0; ofs < len; ofs += 4)
        {
            int q0 = qs[ofs], q1 = qs[ofs+1], q2 = qs[ofs+2], q3 = qs[ofs+3];
            int idx = 27 * abs(q0) + 9 * abs(q1) + 3 * abs(q2) + abs(q3);
            bits += book[idx].len + (q0 != 0) + (q1 != 0) + (q2 != 0) + (q3 != 0);
        }
        break;
    case HCB_5:
    case HCB_6:
        for(ofs = 0; ofs < len; ofs += 2)
        {
            int idx = 9 * qs[ofs] + qs[ofs+1] + 40;
            bits += book[idx].len;
        }
        break;
    case HCB_7:
    case HCB_8:
        for(ofs = 0; ofs < len; ofs += 2)
        {
            int q0 = qs[ofs], q1 = qs[ofs+1];
            int idx = 8 * abs(q0) + abs(q1);
            bits += book[idx].len + (q0 != 0) + (q1 != 0);
        }
        break;
    case HCB_9:
    case HCB_10:
        for(ofs = 0; ofs < len; ofs += 2)
        {
            int q0 = qs[ofs], q1 = qs[ofs+1];
            int idx = 13 * abs(q0) + abs(q1);
            bits += book[idx].len + (q0 != 0) + (q1 != 0);
        }
        break;
    case HCB_ESC:
        for(ofs = 0; ofs < len; ofs += 2)
        {
            int q0 = qs[ofs], q1 = qs[ofs+1];
            int x0 = abs(q0), x1 = abs(q1);
            if (x0 > LAV_ESC) x0 = LAV_ESC;
            if (x1 > LAV_ESC) x1 = LAV_ESC;
            bits += book[17 * x0 + x1].len + (q0 != 0) + (q1 != 0);
            if (x0 >= LAV_ESC) {
                int preflen = 31 - CountLeadingZeros(abs(q0)) - 4;
                bits += (preflen << 1) + 5;
            }
            if (x1 >= LAV_ESC) {
                int preflen = 31 - CountLeadingZeros(abs(q1)) - 4;
                bits += (preflen << 1) + 5;
            }
        }
        break;
    default: return -1;
    }
    return bits;
}

/* Decoupled writing logic to emit codewords to coder->s[]. */
static int huffcode_write(int *qs, int len, int bnum, CoderInfo *coder)
{
    hcode16_t *book = hmap[bnum];
    int bits = 0, datacnt = coder->datacnt;
    int ofs, cnt;

    switch (bnum)
    {
    case HCB_1:
    case HCB_2:
        for(ofs = 0; ofs < len; ofs += 4)
        {
            int idx = 27 * qs[ofs] + 9 * qs[ofs+1] + 3 * qs[ofs+2] + qs[ofs+3] + 40;
            int blen = book[idx].len;
            coder->s[datacnt].data = book[idx].data;
            coder->s[datacnt++].len = blen;
            bits += blen;
        }
        break;
    case HCB_3:
    case HCB_4:
        for(ofs = 0; ofs < len; ofs += 4)
        {
            int q[4];
            q[0] = qs[ofs]; q[1] = qs[ofs+1]; q[2] = qs[ofs+2]; q[3] = qs[ofs+3];
            int idx = 27 * abs(q[0]) + 9 * abs(q[1]) + 3 * abs(q[2]) + abs(q[3]);
            int blen = book[idx].len;
            int data = book[idx].data;
            for(cnt = 0; cnt < 4; cnt++) {
                if(q[cnt]) {
                    blen++;
                    data = (data << 1) | (q[cnt] < 0);
                }
            }
            coder->s[datacnt].data = data;
            coder->s[datacnt++].len = blen;
            bits += blen;
        }
        break;
    case HCB_5:
    case HCB_6:
        for(ofs = 0; ofs < len; ofs += 2)
        {
            int idx = 9 * qs[ofs] + qs[ofs+1] + 40;
            int blen = book[idx].len;
            coder->s[datacnt].data = book[idx].data;
            coder->s[datacnt++].len = blen;
            bits += blen;
        }
        break;
    case HCB_7:
    case HCB_8:
        for(ofs = 0; ofs < len; ofs += 2)
        {
            int q[2];
            q[0] = qs[ofs]; q[1] = qs[ofs+1];
            int idx = 8 * abs(q[0]) + abs(q[1]);
            int blen = book[idx].len;
            int data = book[idx].data;
            for(cnt = 0; cnt < 2; cnt++) {
                if(q[cnt]) {
                    blen++;
                    data = (data << 1) | (q[cnt] < 0);
                }
            }
            coder->s[datacnt].data = data;
            coder->s[datacnt++].len = blen;
            bits += blen;
        }
        break;
    case HCB_9:
    case HCB_10:
        for(ofs = 0; ofs < len; ofs += 2)
        {
            int q[2];
            q[0] = qs[ofs]; q[1] = qs[ofs+1];
            int idx = 13 * abs(q[0]) + abs(q[1]);
            int blen = book[idx].len;
            int data = book[idx].data;
            for(cnt = 0; cnt < 2; cnt++) {
                if(q[cnt]) {
                    blen++;
                    data = (data << 1) | (q[cnt] < 0);
                }
            }
            coder->s[datacnt].data = data;
            coder->s[datacnt++].len = blen;
            bits += blen;
        }
        break;
    case HCB_ESC:
        for(ofs = 0; ofs < len; ofs += 2)
        {
            int q[2];
            q[0] = qs[ofs]; q[1] = qs[ofs+1];
            int x[2];
            x[0] = abs(q[0]); x[1] = abs(q[1]);
            int v[2];
            v[0] = (x[0] > LAV_ESC) ? LAV_ESC : x[0];
            v[1] = (x[1] > LAV_ESC) ? LAV_ESC : x[1];

            int idx = 17 * v[0] + v[1];
            int blen = book[idx].len;
            int data = book[idx].data;
            for(cnt = 0; cnt < 2; cnt++) {
                if(q[cnt]) {
                    blen++;
                    data = (data << 1) | (q[cnt] < 0);
                }
            }
            coder->s[datacnt].data = data;
            coder->s[datacnt++].len = blen;
            bits += blen;

            for(cnt = 0; cnt < 2; cnt++) {
                if (x[cnt] >= LAV_ESC) {
                    int edata = 0;
                    int elen = escape(x[cnt], &edata);
                    coder->s[datacnt].data = edata;
                    coder->s[datacnt++].len = elen;
                    bits += elen;
                }
            }
        }
        break;
    default: return -1;
    }
    coder->datacnt = datacnt;
    return bits;
}

/* Per-band codebook selection: scan |maxq| to pick the lowest range-pair that can
 * represent it, keep whichever book of the pair {base, base+1} codes the band
 * shorter, emit it, and record the choice in coder->book[]. */
int huffbook(CoderInfo *coder,
             int *qs /* quantized spectrum */,
             int len)
{
    int cnt;
    int maxq = 0;
    int bookmin, lenmin;

    for (cnt = 0; cnt < len; cnt++)
    {
        int q = abs(qs[cnt]);
        if (maxq < q)
            maxq = q;
    }

    /* Size the band under both books of the covering pair; keep the cheaper. */
#define BOOKMIN(n)bookmin=n;lenmin=huffcode_size(qs,len,bookmin);if(huffcode_size(qs,len,bookmin+1)<lenmin)bookmin++;

    if (maxq < 1)
    {
        bookmin = HCB_ZERO;
        lenmin = 0;
    }
    else if (maxq < LAV_1 + 1)
    {
        BOOKMIN(HCB_1);
    }
    else if (maxq < LAV_2 + 1)
    {
        BOOKMIN(HCB_3);
    }
    else if (maxq < LAV_4 + 1)
    {
        BOOKMIN(HCB_5);
    }
    else if (maxq < LAV_7 + 1)
    {
        BOOKMIN(HCB_7);
    }
    else if (maxq < LAV_12 + 1)
    {
        BOOKMIN(HCB_9);
    }
    else
    {
        bookmin = HCB_ESC;
    }

    if (bookmin > HCB_ZERO)
        huffcode_write(qs, len, bookmin, coder);
    coder->book[coder->bandcnt] = bookmin;

    return 0;
}

/* Write (or size) the section layout: a 4-bit book + run length per maximal run of
 * equal adjacent books. The count field is cntbits wide (5 long / 3 short), so a
 * run past maxcnt splits into repeated full sections under the same book. */
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

    /* Three independent DPCM chains share HCB_DELTA, each seeded so its first delta
     * is self-contained: intensity positions from 0, scalefactors from global_gain
     * (itself the first regular sf), PNS energies from global_gain - SF_PNS_OFFSET.
     * The decoder rebuilds each by the same running sum, so deltas match. */
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
                /* First PNS band has no prior energy to delta against, so the spec
                 * sends a raw 9-bit value (diff + 256) instead of an HCB_DELTA code;
                 * later PNS bands delta off this one. */
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
