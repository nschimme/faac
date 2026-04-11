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
#include <assert.h>
#include "coder.h"
#include "huffdata.h"
#include "huff2.h"
#include "bitstream.h"
#include "quantize.h"
#include "util.h"

static int escape(int x, int *code)
{
    if (x > MAX_HUFF_ESC_VAL)
    {
        fprintf(stderr, "%s(%d): x_quant > %d\n", __FILE__, __LINE__, MAX_HUFF_ESC_VAL);
        return 0;
    }

    // N ones (prefix), a zero (separator), and N+4 bits for the escape value.
    int preflen = 27 - CLZ((unsigned)x);
    int base = 1 << (preflen + 4);

    *code = ((((1 << preflen) - 1) << 1) << (preflen + 4)) | (x - base);

    return (preflen << 1) + 5;
}

#define arrlen(array) (sizeof(array) / sizeof(*array))

static inline int get_huff_res(int data, int len, int *bits, int *datacnt, CoderInfo *coder)
{
    *bits += len;
    if (coder)
    {
        if (UNLIKELY(*datacnt >= DATASIZE))
        {
            fprintf(stderr, "DATASIZE exceeded: truncation occurred!\n");
            assert(0);
            return -1;
        }
        coder->s[*datacnt].data = data;
        coder->s[(*datacnt)++].len = len;
    }
    return 0;
}

static inline int huff_loop(int *qs, int len, int stride, int book_size, hcode16_t *book, int sign_count, int *bits, int *datacnt, CoderInfo *coder)
{
    int ofs, cnt;
    for (ofs = 0; ofs < len; ofs += stride)
    {
        int *qp = qs + ofs;
        int idx;
        if (stride == 4)
            idx = 27 * abs(qp[0]) + 9 * abs(qp[1]) + 3 * abs(qp[2]) + abs(qp[3]);
        else
            idx = book_size == (int)arrlen(book07) ? 8 * abs(qp[0]) + abs(qp[1]) : 13 * abs(qp[0]) + abs(qp[1]);

        if (UNLIKELY(idx < 0 || idx >= book_size))
            return -1;

        int blen = book[idx].len;
        int data = book[idx].data;
        for (cnt = 0; cnt < sign_count; cnt++)
        {
            if (qp[cnt])
            {
                blen++;
                data <<= 1;
                if (qp[cnt] < 0) data |= 1;
            }
        }
        if (get_huff_res(data, blen, bits, datacnt, coder) < 0)
            return -1;
    }
    return 0;
}

static int huffcode(int *qs /* quantized spectrum */,
                    int len,
                    int bnum,
                    CoderInfo *coder)
{
    static hcode16_t * const hmap[12] = {0, book01, book02, book03, book04,
      book05, book06, book07, book08, book09, book10, book11};
    hcode16_t *book;
    int bits = 0;
    int ofs, *qp;
    int idx;
    int datacnt = coder ? coder->datacnt : 0;

    book = hmap[bnum];
    switch (bnum)
    {
    case 1:
    case 2:
        for(ofs = 0; ofs < len; ofs += 4)
        {
            qp = qs+ofs;
            idx = 27 * qp[0] + 9 * qp[1] + 3 * qp[2] + qp[3] + 40;
            if (UNLIKELY(idx < 0 || idx >= (int)arrlen(book01))) return -1;
            if (get_huff_res(book[idx].data, book[idx].len, &bits, &datacnt, coder) < 0) return -1;
        }
        break;
    case 3:
    case 4:
        if (huff_loop(qs, len, 4, (int)arrlen(book03), book, 4, &bits, &datacnt, coder) < 0) return -1;
        break;
    case 5:
    case 6:
        for(ofs = 0; ofs < len; ofs += 2)
        {
            qp = qs+ofs;
            idx = 9 * qp[0] + qp[1] + 40;
            if (UNLIKELY(idx < 0 || idx >= (int)arrlen(book05))) return -1;
            if (get_huff_res(book[idx].data, book[idx].len, &bits, &datacnt, coder) < 0) return -1;
        }
        break;
    case 7:
    case 8:
        if (huff_loop(qs, len, 2, (int)arrlen(book07), book, 2, &bits, &datacnt, coder) < 0) return -1;
        break;
    case 9:
    case 10:
        if (huff_loop(qs, len, 2, (int)arrlen(book09), book, 2, &bits, &datacnt, coder) < 0) return -1;
        break;
    case HCB_ESC:
        for(ofs = 0; ofs < len; ofs += 2)
        {
            int x0, x1, cnt;
            int blen, data;
            qp = qs+ofs;
            x0 = abs(qp[0]);
            x1 = abs(qp[1]);

            idx = 17 * min(x0, 16) + min(x1, 16);
            if (UNLIKELY(idx < 0 || idx >= (int)arrlen(book11))) return -1;
            blen = book[idx].len;
            data = book[idx].data;
            for (cnt = 0; cnt < 2; cnt++)
            {
                if (qp[cnt])
                {
                    blen++;
                    data <<= 1;
                    if (qp[cnt] < 0) data |= 1;
                }
            }
            if (get_huff_res(data, blen, &bits, &datacnt, coder) < 0) return -1;

            if (x0 >= 16)
            {
                blen = escape(x0, &data);
                if (get_huff_res(data, blen, &bits, &datacnt, coder) < 0) return -1;
            }
            if (x1 >= 16)
            {
                blen = escape(x1, &data);
                if (get_huff_res(data, blen, &bits, &datacnt, coder) < 0) return -1;
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

#define BOOKMIN(n)bookmin=n;lenmin=huffcode(qs,len,bookmin,0);if(huffcode(qs,len,bookmin+1,0)<lenmin)bookmin++;

    if (maxq < 1)
    {
        bookmin = HCB_ZERO;
        lenmin = 0;
    }
    else if (maxq < 2)
    {
        BOOKMIN(1);
    }
    else if (maxq < 3)
    {
        BOOKMIN(3);
    }
    else if (maxq < 5)
    {
        BOOKMIN(5);
    }
    else if (maxq < 8)
    {
        BOOKMIN(7);
    }
    else if (maxq < 13)
    {
        BOOKMIN(9);
    }
    else
    {
        bookmin = HCB_ESC;
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

    // fixme: move range check to quantizer
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
