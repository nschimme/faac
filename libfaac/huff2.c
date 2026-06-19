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

    bookmin = book_for_maxq(maxq);

    if (bookmin == HCB_ZERO)
    {
        lenmin = 0;
    }
    else if (bookmin == HCB_ESC)
    {
        lenmin = huffcode(qs, len, HCB_ESC, 0);
    }
    else
    {
        int l0 = huffcode(qs, len, bookmin, 0);
        int l1 = huffcode(qs, len, bookmin + 1, 0);

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

    coder->book[coder->bandcnt] = bookmin;
    coder->blen[coder->bandcnt] = lenmin;
    coder->maxq[coder->bandcnt] = maxq;

    return 0;
}

static int spec_cost(CoderInfo *coder, int band, int book)
{
    if (book == HCB_ZERO)
        return 0;
    return huffcode(coder->qspec + coder->qoffset[band], coder->qlen[band], book, 0);
}

static int count_header_bits(int cnt, int maxcnt, int cntbits)
{
    int bits = 4;
    while (cnt > maxcnt)
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
    int costs[MAX_SCFAC_BANDS][13]; // bits for each book (1-11 pair or 11) per band

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

        // Precompute costs for the group (O(N))
        for (int b = band; b < maxband; b++)
        {
            int book = coder->book[b];
            if (book > HCB_ESC)
            {
                for (int k = 0; k <= 12; k++) costs[b][k] = 0;
                continue;
            }

            // Always calculate ESC cost
            costs[b][HCB_ESC] = spec_cost(coder, b, HCB_ESC);

            // Calculate costs for all potential base books
            for (int k = 1; k < HCB_ESC; k += 2)
            {
                costs[b][k] = spec_cost(coder, b, k);
                costs[b][k + 1] = spec_cost(coder, b, k + 1);
            }
        }

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

            for (int k = 1; k <= 12; k++) run_costs[k] = costs[band][k];

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
                    // Accumulate costs (O(1) work within O(N) loop)
                    for (int k = 1; k <= 12; k++)
                    {
                        if (run_costs[k] >= 0 && costs[band][k] >= 0)
                            run_costs[k] += costs[band][k];
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
    int band = 0;
    coder->datacnt = 0;
    while (band < coder->bandcnt)
    {
        int book = coder->book[band];
        if (book > HCB_ZERO && book <= HCB_ESC)
        {
            int start_band = band;
            int total_len = coder->qlen[band];
            band++;
            while (band < coder->bandcnt && coder->book[band] == book)
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

            while (bookcnt > maxcnt)
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
