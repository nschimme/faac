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


static int book_for_maxq(int maxq)
{
    if (maxq < 1)
        return HCB_ZERO;
    if (maxq < 2)
        return 1;
    if (maxq < 3)
        return 3;
    if (maxq < 5)
        return 5;
    if (maxq < 8)
        return 7;
    if (maxq < 13)
        return 9;

    return HCB_ESC;
}

static int maxq_for_book(int book)
{
    switch (book)
    {
    case 1:
    case 2:
        return 1;
    case 3:
    case 4:
        return 2;
    case 5:
    case 6:
        return 4;
    case 7:
    case 8:
        return 7;
    case 9:
    case 10:
        return 12;
    case 11:
        return MAX_HUFF_ESC_VAL;
    default:
        return 0;
    }
}

static void huff_count_all_books(CoderInfo *coder, int band, int *costs)
{
    int maxq = coder->maxq[band];
    int len = coder->qlen[band];
    int *qs = (coder->qspec + coder->qoffset[band]);
    int ofs, k;

    for (k = 1; k <= 11; k++)
    {
        if (maxq > maxq_for_book(k))
            costs[k] = -1;
        else
            costs[k] = 0;
    }

    if (maxq == 0)
    {
        /* O(1) fast-path for zeros: actual table lengths for zero-index symbols */
        if (costs[1] >= 0) costs[1] = (len >> 2) * book01[40].len;
        if (costs[2] >= 0) costs[2] = (len >> 2) * book02[40].len;
        if (costs[3] >= 0) costs[3] = (len >> 2) * book03[0].len;
        if (costs[4] >= 0) costs[4] = (len >> 2) * book04[0].len;
        if (costs[5] >= 0) costs[5] = (len >> 1) * book05[40].len;
        if (costs[6] >= 0) costs[6] = (len >> 1) * book06[40].len;
        if (costs[7] >= 0) costs[7] = (len >> 1) * book07[0].len;
        if (costs[8] >= 0) costs[8] = (len >> 1) * book08[0].len;
        if (costs[9] >= 0) costs[9] = (len >> 1) * book09[0].len;
        if (costs[10] >= 0) costs[10] = (len >> 1) * book10[0].len;
        if (costs[11] >= 0) costs[11] = (len >> 1) * book11[0].len;
        return;
    }

    /* Single pass over the spectrum: hoisting and loop-fusion to minimize traffic */
    for (ofs = 0; ofs < len; ofs += 4)
    {
        const int *qp = qs + ofs;
        int q0 = qp[0], q1 = qp[1], q2 = qp[2], q3 = qp[3];
        int s0 = (q0 != 0), s1 = (q1 != 0), s2 = (q2 != 0), s3 = (q3 != 0);

        /* 4-tuples */
        if (costs[1] >= 0) costs[1] += book01[27 * q0 + 9 * q1 + 3 * q2 + q3 + 40].len;
        if (costs[2] >= 0) costs[2] += book02[27 * q0 + 9 * q1 + 3 * q2 + q3 + 40].len;
        if (costs[3] >= 0 || costs[4] >= 0)
        {
            int a0 = abs(q0), a1 = abs(q1), a2 = abs(q2), a3 = abs(q3);
            int idx = 27 * a0 + 9 * a1 + 3 * a2 + a3;
            int signbits = s0 + s1 + s2 + s3;
            if (costs[3] >= 0) costs[3] += book03[idx].len + signbits;
            if (costs[4] >= 0) costs[4] += book04[idx].len + signbits;
        }

        /* First pair of the 4-tuple */
        if (costs[5] >= 0) costs[5] += book05[9 * q0 + q1 + 40].len;
        if (costs[6] >= 0) costs[6] += book06[9 * q0 + q1 + 40].len;
        if (costs[7] >= 0 || costs[8] >= 0 || costs[9] >= 0 || costs[10] >= 0 || costs[11] >= 0)
        {
            int a0 = abs(q0), a1 = abs(q1);
            int signbits = s0 + s1;
            if (costs[7] >= 0) costs[7] += book07[8 * a0 + a1].len + signbits;
            if (costs[8] >= 0) costs[8] += book08[8 * a0 + a1].len + signbits;
            if (costs[9] >= 0) costs[9] += book09[13 * a0 + a1].len + signbits;
            if (costs[10] >= 0) costs[10] += book10[13 * a0 + a1].len + signbits;
            if (costs[11] >= 0)
            {
                int x0 = (a0 > 16) ? 16 : a0;
                int x1 = (a1 > 16) ? 16 : a1;
                int dummy;
                costs[11] += book11[17 * x0 + x1].len + signbits;
                if (a0 >= 16) costs[11] += escape(a0, &dummy);
                if (a1 >= 16) costs[11] += escape(a1, &dummy);
            }
        }

        /* Second pair of the 4-tuple */
        if (costs[5] >= 0) costs[5] += book05[9 * q2 + q3 + 40].len;
        if (costs[6] >= 0) costs[6] += book06[9 * q2 + q3 + 40].len;
        if (costs[7] >= 0 || costs[8] >= 0 || costs[9] >= 0 || costs[10] >= 0 || costs[11] >= 0)
        {
            int a2 = abs(q2), a3 = abs(q3);
            int signbits = s2 + s3;
            if (costs[7] >= 0) costs[7] += book07[8 * a2 + a3].len + signbits;
            if (costs[8] >= 0) costs[8] += book08[8 * a2 + a3].len + signbits;
            if (costs[9] >= 0) costs[9] += book09[13 * a2 + a3].len + signbits;
            if (costs[10] >= 0) costs[10] += book10[13 * a2 + a3].len + signbits;
            if (costs[11] >= 0)
            {
                int x2 = (a2 > 16) ? 16 : a2;
                int x3 = (a3 > 16) ? 16 : a3;
                int dummy;
                costs[11] += book11[17 * x2 + x3].len + signbits;
                if (a2 >= 16) costs[11] += escape(a2, &dummy);
                if (a3 >= 16) costs[11] += escape(a3, &dummy);
            }
        }
    }
}

int huffbook(CoderInfo *coder,
             int *qs /* quantized spectrum */,
             int len)
{
    int cnt;
    int maxq = 0;
    int bookmin, lenmin;
    int band = coder->bandcnt;

    if (qs)
    {
        for (cnt = 0; cnt < len; cnt++)
        {
            int q = abs(qs[cnt]);
            if (maxq < q)
                maxq = q;
        }
    }
    coder->maxq[band] = maxq;

    int *costs = coder->all_costs[band];
    huff_count_all_books(coder, band, costs);

    if (maxq == 0)
    {
        bookmin = HCB_ZERO;
        lenmin = 0;
    }
    else
    {
        bookmin = book_for_maxq(maxq);
        if (bookmin == HCB_ESC)
        {
            lenmin = costs[HCB_ESC];
        }
        else
        {
            int l0 = costs[bookmin];
            int l1 = costs[bookmin + 1];

            if (l1 >= 0 && (l0 < 0 || l1 < l0))
            {
                bookmin++;
                lenmin = l1;
            }
            else
            {
                lenmin = l0;
            }
        }
    }

    coder->book[band] = bookmin;
    coder->blen[band] = lenmin;

    return 0;
}

static int count_header_bits(int cnt, int maxcnt, int cntbits)
{
    int bits = 4;
    while (cnt >= maxcnt)
    {
        bits += cntbits;
        cnt -= maxcnt;
    }
    bits += cntbits;
    return bits;
}

void section_optimize(CoderInfo *coder)
{
    int maxcnt, cntbits;
    int group;

    if (coder->block_type == ONLY_SHORT_WINDOW)
    {
        maxcnt = 7;
        cntbits = 3;
    }
    else
    {
        maxcnt = 31;
        cntbits = 5;
    }

    for (group = 0; group < coder->groups.n; group++)
    {
        int band = group * coder->sfbn;
        int maxband = band + coder->sfbn;

        while (band < maxband)
        {
            int start_band = band;
            int current_book = coder->book[band];
            int current_spec_sum = coder->blen[band];
            int maxq = coder->maxq[band];
            int run_costs[13];

            if (current_book == HCB_PNS || current_book == HCB_INTENSITY || current_book == HCB_INTENSITY2)
            {
                band++;
                continue;
            }

            for (int k = 1; k <= 11; k++) run_costs[k] = coder->all_costs[band][k];

            band++;
            while (band < maxband)
            {
                int next_book = coder->book[band];
                int next_maxq = coder->maxq[band];
                int merged_book, merged_cost;
                int delta, header_saved;

                if (next_book == HCB_PNS || next_book == HCB_INTENSITY || next_book == HCB_INTENSITY2)
                    break;

                int span_maxq = (maxq > next_maxq) ? maxq : next_maxq;
                merged_book = book_for_maxq(span_maxq);

                if (merged_book == HCB_ZERO)
                {
                    merged_cost = 0;
                }
                else
                {
                    // Accumulate costs (O(1) work)
                    for (int k = 1; k <= 11; k++)
                    {
                        if (run_costs[k] >= 0 && coder->all_costs[band][k] >= 0)
                            run_costs[k] += coder->all_costs[band][k];
                        else
                            run_costs[k] = -1;
                    }

                    if (merged_book == HCB_ESC)
                    {
                        merged_cost = run_costs[HCB_ESC];
                    }
                    else
                    {
                        int l0 = run_costs[merged_book];
                        int l1 = run_costs[merged_book + 1];
                        if (l1 >= 0 && (l0 < 0 || l1 < l0))
                        {
                            merged_book++;
                            merged_cost = l1;
                        }
                        else
                        {
                            merged_cost = l0;
                        }
                    }
                }

                if (merged_cost < 0) break;

                int span_len = band - start_band;
                header_saved = count_header_bits(span_len, maxcnt, cntbits) +
                               count_header_bits(1, maxcnt, cntbits) -
                               count_header_bits(span_len + 1, maxcnt, cntbits);

                delta = merged_cost - (current_spec_sum + coder->blen[band]);

                if (delta < header_saved)
                {
                    for (int b = start_band; b <= band; b++)
                        coder->book[b] = merged_book;
                    current_book = merged_book;
                    current_spec_sum = merged_cost;
                    maxq = span_maxq;
                    band++;
                }
                else
                {
                    break;
                }
            }
        }
    }
}

void emit_spectral(CoderInfo *coder)
{
    int group;
    coder->datacnt = 0;
    for (group = 0; group < coder->groups.n; group++)
    {
        int band = group * coder->sfbn;
        int maxband = band + coder->sfbn;

        while (band < maxband)
        {
            int book = coder->book[band];
            if (book > HCB_ZERO && book <= HCB_ESC)
            {
                int start_band = band;
                int total_len = coder->qlen[band];
                band++;
                while (band < maxband && coder->book[band] == book)
                {
                    total_len += coder->qlen[band];
                    band++;
                }
                huffcode(coder->qspec + coder->qoffset[start_band], total_len, book, coder);
            }
            else
            {
                band++;
            }
        }
    }
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
