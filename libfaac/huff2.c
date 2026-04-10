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
#include "quantize.h"
#include "util.h"


static inline int escape(int x, int *code)
{
    int preflen = 0;
    int base;

    if (UNLIKELY(x > MAX_HUFF_ESC_VAL))
    {
        fprintf(stderr, "%s(%d): x_quant > %d\n", __FILE__, __LINE__, MAX_HUFF_ESC_VAL);
        x = MAX_HUFF_ESC_VAL;
    }

    if (x >= 32)
    {
#ifdef __GNUC__
        preflen = 31 - __builtin_clz((unsigned)x) - 4;
#else
        int tmp = x >> 5;
        while (tmp)
        {
            tmp >>= 1;
            preflen++;
        }
#endif
    }

    base = 1 << (preflen + 4);
    *code = (((1 << preflen) - 1) << (preflen + 5)) | (x - base);

    return (preflen << 1) + 5;
}

#define arrlen(array) (sizeof(array) / sizeof(*array))

typedef struct {
    int data;
    int len;
} huff_res_t;

static inline huff_res_t get_huff_res(const int *qp, int bnum, hcode16_t *book)
{
    huff_res_t res;
    int idx, cnt;
    int data;
    int blen;

    switch (bnum) {
    case 1:
    case 2:
        idx = 27 * qp[0] + 9 * qp[1] + 3 * qp[2] + qp[3] + 40;
        res.data = book[idx].data;
        res.len = book[idx].len;
        break;
    case 3:
    case 4:
        idx = 27 * abs(qp[0]) + 9 * abs(qp[1]) + 3 * abs(qp[2]) + abs(qp[3]);
        data = book[idx].data;
        blen = book[idx].len;
        for (cnt = 0; cnt < 4; cnt++) {
            if (qp[cnt]) {
                blen++;
                data <<= 1;
                if (qp[cnt] < 0) data |= 1;
            }
        }
        res.data = data;
        res.len = blen;
        break;
    case 5:
    case 6:
        idx = 9 * qp[0] + qp[1] + 40;
        res.data = book[idx].data;
        res.len = book[idx].len;
        break;
    case 7:
    case 8:
        idx = 8 * abs(qp[0]) + abs(qp[1]);
        data = book[idx].data;
        blen = book[idx].len;
        for (cnt = 0; cnt < 2; cnt++) {
            if (qp[cnt]) {
                blen++;
                data <<= 1;
                if (qp[cnt] < 0) data |= 1;
            }
        }
        res.data = data;
        res.len = blen;
        break;
    case 9:
    case 10:
        idx = 13 * abs(qp[0]) + abs(qp[1]);
        data = book[idx].data;
        blen = book[idx].len;
        for (cnt = 0; cnt < 2; cnt++) {
            if (qp[cnt]) {
                blen++;
                data <<= 1;
                if (qp[cnt] < 0) data |= 1;
            }
        }
        res.data = data;
        res.len = blen;
        break;
    case HCB_ESC:
    default:
        {
            int x0 = abs(qp[0]);
            int x1 = abs(qp[1]);
            idx = 17 * (x0 > 16 ? 16 : x0) + (x1 > 16 ? 16 : x1);
            data = book[idx].data;
            blen = book[idx].len;
            for (cnt = 0; cnt < 2; cnt++) {
                if (qp[cnt]) {
                    blen++;
                    data <<= 1;
                    if (qp[cnt] < 0) data |= 1;
                }
            }
            res.data = data;
            res.len = blen;
        }
        break;
    }
    return res;
}

static int huffcode(const int * __restrict qs /* quantized spectrum */,
                    int len,
                    int bnum,
                    CoderInfo * __restrict coder)
{
    static hcode16_t * const hmap[12] = {0, book01, book02, book03, book04,
      book05, book06, book07, book08, book09, book10, book11};
    hcode16_t *book = hmap[bnum];
    int bits = 0;
    int ofs;
    int datacnt = 0;
    int stride = (bnum <= 4) ? 4 : 2;

    if (coder) {
        datacnt = coder->datacnt;
        if (UNLIKELY((size_t)len * 2 >= (size_t)(DATASIZE - datacnt)))
            return -1;
    }

    for (ofs = 0; ofs < len; ofs += stride) {
        huff_res_t res = get_huff_res(qs + ofs, bnum, book);
        if (coder) {
            coder->s[datacnt].data = res.data;
            coder->s[datacnt++].len = res.len;
        }
        bits += res.len;

        if (bnum == HCB_ESC) {
            int x0 = abs(qs[ofs]);
            int x1 = abs(qs[ofs + 1]);
            if (UNLIKELY(x0 >= 16)) {
                int esc_code, esc_len;
                esc_len = escape(x0, &esc_code);
                if (coder) {
                    coder->s[datacnt].data = esc_code;
                    coder->s[datacnt++].len = esc_len;
                }
                bits += esc_len;
            }
            if (UNLIKELY(x1 >= 16)) {
                int esc_code, esc_len;
                esc_len = escape(x1, &esc_code);
                if (coder) {
                    coder->s[datacnt].data = esc_code;
                    coder->s[datacnt++].len = esc_len;
                }
                bits += esc_len;
            }
        }
    }

    if (coder) coder->datacnt = datacnt;
    return bits;
}


int huffbook(CoderInfo * __restrict coder,
             const int * __restrict qs /* quantized spectrum */,
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
    lastpns = coder->global_gain - 90;

    // fixme: move range check to quantizer
    for (cnt = 0; cnt < coder->bandcnt; cnt++)
    {
        int book = coder->book[cnt];

        if ((book == HCB_INTENSITY) || (book== HCB_INTENSITY2))
        {
            diff = coder->sf[cnt] - lastis;
            if (diff > 60)
                diff = 60;
            if (diff < -60)
                diff = -60;
            length = book12[60 + diff].len;

            bits += length;

            lastis += diff;

            if (write)
                PutBit(stream, book12[60 + diff].data, length);
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

            if (diff > 60)
                diff = 60;
            if (diff < -60)
                diff = -60;

            length = book12[60 + diff].len;
            bits += length;
            lastpns += diff;

            if (write)
                PutBit(stream, book12[60 + diff].data, length);
        }
        else if (book)
        {
            diff = coder->sf[cnt] - lastsf;
            if (diff > 60)
                diff = 60;
            if (diff < -60)
                diff = -60;
            length = book12[60 + diff].len;

            bits += length;
            lastsf += diff;

            if (write)
                PutBit(stream, book12[60 + diff].data, length);
        }

    }
    return bits;
}
