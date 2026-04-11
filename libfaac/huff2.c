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
#include "quantize.h"
#include "bitstream.h"
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

static inline void get_huff_res(int data, int len, int *bits, int *datacnt, CoderInfo *coder)
{
    *bits += len;
    if (coder)
    {
        coder->s[*datacnt].data = data;
        coder->s[(*datacnt)++].len = len;
    }
}

static int huffcode(int *qs /* quantized spectrum */,
                    int len,
                    int bnum,
                    int maxq,
                    CoderInfo *coder)
{
    static hcode16_t * const hmap[12] = {0, book01, book02, book03, book04,
      book05, book06, book07, book08, book09, book10, book11};
    hcode16_t *book;
    int bits = 0, blen;
    int ofs, *qp;
    int data = 0;
    int idx;
    int datacnt = coder ? coder->datacnt : 0;

    /* Safety checks hoisted out of the loop */
    if (coder)
    {
        /* Worst case: two escape sequences and one 2-tuple codeword per 2 spectral lines */
        /* Each 2 spectral lines can produce up to 3 codewords in the coder->s array */
        int worst_case_len = (len / 2) * 3;
        if (UNLIKELY(datacnt + worst_case_len > DATASIZE))
        {
            fprintf(stderr, "DATASIZE exceeded: truncation occurred!\n");
            return -1;
        }
    }

    /* Codebook range check hoisted */
    if (bnum > 0 && bnum <= 10)
    {
        static const int book_maxq[] = {0, 1, 1, 2, 2, 4, 4, 7, 7, 12, 12};
        if (UNLIKELY(maxq > book_maxq[bnum]))
            return -1;
    }

    book = hmap[bnum];
    switch (bnum)
    {
    case 1:
    case 2:
        for(ofs = 0; ofs < len; ofs += 4)
        {
            qp = qs+ofs;
            idx = 27 * qp[0] + 9 * qp[1] + 3 * qp[2] + qp[3] + 40;
            get_huff_res(book[idx].data, book[idx].len, &bits, &datacnt, coder);
        }
        break;
    case 3:
    case 4:
        for(ofs = 0; ofs < len; ofs += 4)
        {
            int q0 = qs[ofs], q1 = qs[ofs+1], q2 = qs[ofs+2], q3 = qs[ofs+3];
            int a0 = abs(q0), a1 = abs(q1), a2 = abs(q2), a3 = abs(q3);
            idx = 27 * a0 + 9 * a1 + 3 * a2 + a3;
            blen = book[idx].len;
            data = book[idx].data;
            if (a0) { blen++; data = (data << 1) | (q0 < 0); }
            if (a1) { blen++; data = (data << 1) | (q1 < 0); }
            if (a2) { blen++; data = (data << 1) | (q2 < 0); }
            if (a3) { blen++; data = (data << 1) | (q3 < 0); }
            get_huff_res(data, blen, &bits, &datacnt, coder);
        }
        break;
    case 5:
    case 6:
        for(ofs = 0; ofs < len; ofs += 2)
        {
            qp = qs+ofs;
            idx = 9 * qp[0] + qp[1] + 40;
            get_huff_res(book[idx].data, book[idx].len, &bits, &datacnt, coder);
        }
        break;
    case 7:
    case 8:
        for(ofs = 0; ofs < len; ofs += 2)
        {
            int q0 = qs[ofs], q1 = qs[ofs+1];
            int a0 = abs(q0), a1 = abs(q1);
            idx = 8 * a0 + a1;
            blen = book[idx].len;
            data = book[idx].data;
            if (a0) { blen++; data = (data << 1) | (q0 < 0); }
            if (a1) { blen++; data = (data << 1) | (q1 < 0); }
            get_huff_res(data, blen, &bits, &datacnt, coder);
        }
        break;
    case 9:
    case 10:
        for(ofs = 0; ofs < len; ofs += 2)
        {
            int q0 = qs[ofs], q1 = qs[ofs+1];
            int a0 = abs(q0), a1 = abs(q1);
            idx = 13 * a0 + a1;
            blen = book[idx].len;
            data = book[idx].data;
            if (a0) { blen++; data = (data << 1) | (q0 < 0); }
            if (a1) { blen++; data = (data << 1) | (q1 < 0); }
            get_huff_res(data, blen, &bits, &datacnt, coder);
        }
        break;
    case HCB_ESC:
        for(ofs = 0; ofs < len; ofs += 2)
        {
            int q0 = qs[ofs], q1 = qs[ofs+1];
            int a0 = abs(q0), a1 = abs(q1);

            idx = 17 * min(a0, 16) + min(a1, 16);
            blen = book[idx].len;
            data = book[idx].data;
            if (a0) { blen++; data = (data << 1) | (q0 < 0); }
            if (a1) { blen++; data = (data << 1) | (q1 < 0); }
            get_huff_res(data, blen, &bits, &datacnt, coder);

            if (a0 >= 16)
            {
                blen = escape(a0, &data);
                get_huff_res(data, blen, &bits, &datacnt, coder);
            }
            if (a1 >= 16)
            {
                blen = escape(a1, &data);
                get_huff_res(data, blen, &bits, &datacnt, coder);
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

#define BOOKMIN(n)bookmin=n;lenmin=huffcode(qs,len,bookmin,maxq,0);if(huffcode(qs,len,bookmin+1,maxq,0)<lenmin)bookmin++;

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
        huffcode(qs, len, bookmin, maxq, coder);
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
