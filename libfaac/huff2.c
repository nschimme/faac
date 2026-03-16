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

    if (x >= 8192)
    {
        fprintf(stderr, "%s(%d): x_quant >= 8192\n", __FILE__, __LINE__);
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
    int data;
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

static int huff_count_bits(int *qs, int len, int book)
{
    if (book == HCB_ZERO)
    {
        int i;
        for (i = 0; i < len; i++)
            if (qs[i])
                return 1000000; // Impossible
        return 0;
    }
    if (book == HCB_PNS || book == HCB_INTENSITY || book == HCB_INTENSITY2)
    {
        return 0; // Handled separately
    }
    if (book == HCB_ESC)
    {
        return huffcode(qs, len, book, NULL);
    }
    if (book >= 1 && book <= 10)
    {
        int i;
        int maxq = 0;
        static const int book_limit[] = {0, 1, 1, 2, 2, 4, 4, 7, 7, 12, 12};
        for (i = 0; i < len; i++)
        {
            int q = abs(qs[i]);
            if (q > maxq) maxq = q;
        }
        if (maxq > book_limit[book])
            return 1000000; // Impossible

        return huffcode(qs, len, book, NULL);
    }
    return 1000000;
}

#define MAX_BOOKS (HCB_INTENSITY + 1)

typedef struct {
    int cost;
    int prev_book;
    int prev_run;
    int last_sf;
    int last_is;
    int last_pns;
} Node;

void optimize_books(CoderInfo *coder)
{
    int band, book, run;
    int maxcnt;
    int cntbits;
    int group;
    int start_band = 0;
    Node (*nodes)[MAX_BOOKS][32];
    int group_last_sf = coder->global_gain;
    int group_last_is = 0;
    int group_last_pns = coder->global_gain - 90;

    nodes = malloc(MAX_SCFAC_BANDS * sizeof(*nodes));
    if (!nodes) return;

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
        int end_band = start_band + coder->sfbn;
        int b;

        // Initialize first band in group
        for (book = 0; book < MAX_BOOKS; book++)
        {
            for (run = 0; run <= maxcnt; run++)
            {
                nodes[start_band][book][run].cost = 1000000;
            }
        }

        for (book = 0; book < MAX_BOOKS; book++)
        {
            // Preserve PNS and Intensity decisions
            if (coder->book[start_band] == HCB_PNS && book != HCB_PNS) continue;
            if (coder->book[start_band] != HCB_PNS && book == HCB_PNS) continue;
            if ((coder->book[start_band] == HCB_INTENSITY || coder->book[start_band] == HCB_INTENSITY2) &&
                (book != HCB_INTENSITY && book != HCB_INTENSITY2)) continue;
            if ((coder->book[start_band] != HCB_INTENSITY && coder->book[start_band] != HCB_INTENSITY2) &&
                (book == HCB_INTENSITY || book == HCB_INTENSITY2)) continue;

            int spectral_bits = huff_count_bits(&coder->xi[coder->band_offset[start_band]],
                                                coder->band_len[start_band], book);
            if (spectral_bits < 1000000)
            {
                int sf_bits = 0;
                int last_sf = group_last_sf;
                int last_is = group_last_is;
                int last_pns = group_last_pns;

                if (book == HCB_INTENSITY || book == HCB_INTENSITY2)
                {
                    int diff = coder->sf[start_band] - last_is;
                    if (diff < -60) diff = -60;
                    if (diff > 60) diff = 60;
                    sf_bits = book12[60 + diff].len;
                    last_is += diff;
                }
                else if (book == HCB_PNS)
                {
                    sf_bits = 9; // PNS energy is always 9 bits for the first PNS band
                    last_pns = coder->sf[start_band];
                }
                else if (book != HCB_ZERO)
                {
                    int diff = coder->sf[start_band] - last_sf;
                    if (diff < -60) diff = -60;
                    if (diff > 60) diff = 60;
                    sf_bits = book12[60 + diff].len;
                    last_sf += diff;
                }

                nodes[start_band][book][1].cost = 4 + cntbits + spectral_bits + sf_bits;
                nodes[start_band][book][1].prev_book = -1;
                nodes[start_band][book][1].last_sf = last_sf;
                nodes[start_band][book][1].last_is = last_is;
                nodes[start_band][book][1].last_pns = last_pns;
            }
        }

        for (b = start_band + 1; b < end_band; b++)
        {
            for (book = 0; book < MAX_BOOKS; book++)
                for (run = 0; run <= maxcnt; run++)
                    nodes[b][book][run].cost = 1000000;

            for (book = 0; book < MAX_BOOKS; book++)
            {
                if (coder->book[b] == HCB_PNS && book != HCB_PNS) continue;
                if (coder->book[b] != HCB_PNS && book == HCB_PNS) continue;
                if ((coder->book[b] == HCB_INTENSITY || coder->book[b] == HCB_INTENSITY2) &&
                    (book != HCB_INTENSITY && book != HCB_INTENSITY2)) continue;
                if ((coder->book[b] != HCB_INTENSITY && coder->book[b] != HCB_INTENSITY2) &&
                    (book == HCB_INTENSITY || book == HCB_INTENSITY2)) continue;

                int spectral_bits = huff_count_bits(&coder->xi[coder->band_offset[b]],
                                                    coder->band_len[b], book);
                if (spectral_bits >= 1000000) continue;

                // Option 1: Continue previous run
                for (run = 1; run < maxcnt; run++)
                {
                    if (nodes[b - 1][book][run].cost < 1000000)
                    {
                        int sf_bits = 0;
                        int last_sf = nodes[b - 1][book][run].last_sf;
                        int last_is = nodes[b - 1][book][run].last_is;
                        int last_pns = nodes[b - 1][book][run].last_pns;
                        if (book == HCB_INTENSITY || book == HCB_INTENSITY2)
                        {
                            int diff = coder->sf[b] - last_is;
                            if (diff < -60) diff = -60;
                            if (diff > 60) diff = 60;
                            sf_bits = book12[60 + diff].len;
                            last_is += diff;
                        }
                        else if (book == HCB_PNS)
                        {
                            int diff = coder->sf[b] - last_pns;
                            if (diff < -60) diff = -60;
                            if (diff > 60) diff = 60;
                            sf_bits = book12[60 + diff].len;
                            last_pns += diff;
                        }
                        else if (book != HCB_ZERO)
                        {
                            int diff = coder->sf[b] - last_sf;
                            if (diff < -60) diff = -60;
                            if (diff > 60) diff = 60;
                            sf_bits = book12[60 + diff].len;
                            last_sf += diff;
                        }

                        int new_cost = nodes[b - 1][book][run].cost + spectral_bits + sf_bits;
                        if (new_cost < nodes[b][book][run + 1].cost)
                        {
                            nodes[b][book][run + 1].cost = new_cost;
                            nodes[b][book][run + 1].prev_book = book;
                            nodes[b][book][run + 1].prev_run = run;
                            nodes[b][book][run + 1].last_sf = last_sf;
                            nodes[b][book][run + 1].last_is = last_is;
                            nodes[b][book][run + 1].last_pns = last_pns;
                        }
                    }
                }

                // Option 2: Start new run (could be same book or different book)
                for (int prev_book = 0; prev_book < MAX_BOOKS; prev_book++)
                {
                    for (int prev_run = 1; prev_run <= maxcnt; prev_run++)
                    {
                        if (nodes[b - 1][prev_book][prev_run].cost < 1000000)
                        {
                            int sf_bits = 0;
                            int last_sf = nodes[b - 1][prev_book][prev_run].last_sf;
                            int last_is = nodes[b - 1][prev_book][prev_run].last_is;
                            int last_pns = nodes[b - 1][prev_book][prev_run].last_pns;
                            if (book == HCB_INTENSITY || book == HCB_INTENSITY2)
                            {
                                int diff = coder->sf[b] - last_is;
                                if (diff < -60) diff = -60;
                                if (diff > 60) diff = 60;
                                sf_bits = book12[60 + diff].len;
                                last_is += diff;
                            }
                            else if (book == HCB_PNS)
                            {
                                // Correct AAC PNS logic: first PNS band in frame is 9 bits, subsequent are differential.
                                // Let's simplify: if last_pns was updated, it's not the first.
                                if (last_pns == (group_last_pns)) {
                                    sf_bits = 9;
                                    last_pns = coder->sf[b];
                                } else {
                                    int diff = coder->sf[b] - last_pns;
                                    if (diff < -60) diff = -60;
                                    if (diff > 60) diff = 60;
                                    sf_bits = book12[60 + diff].len;
                                    last_pns += diff;
                                }
                            }
                            else if (book != HCB_ZERO)
                            {
                                int diff = coder->sf[b] - last_sf;
                                if (diff < -60) diff = -60;
                                if (diff > 60) diff = 60;
                                sf_bits = book12[60 + diff].len;
                                last_sf += diff;
                            }

                            int new_cost = nodes[b - 1][prev_book][prev_run].cost + 4 + cntbits + spectral_bits + sf_bits;
                            if (new_cost < nodes[b][book][1].cost)
                            {
                                nodes[b][book][1].cost = new_cost;
                                nodes[b][book][1].prev_book = prev_book;
                                nodes[b][book][1].prev_run = prev_run;
                                nodes[b][book][1].last_sf = last_sf;
                                nodes[b][book][1].last_is = last_is;
                                nodes[b][book][1].last_pns = last_pns;
                            }
                        }
                    }
                }
            }
        }

        // Backtrack
        int min_cost = 1000000;
        int best_book = -1;
        int best_run = -1;
        for (book = 0; book < MAX_BOOKS; book++)
        {
            for (run = 1; run <= maxcnt; run++)
            {
                if (nodes[end_band - 1][book][run].cost < min_cost)
                {
                    min_cost = nodes[end_band - 1][book][run].cost;
                    best_book = book;
                    best_run = run;
                }
            }
        }

        if (best_book != -1)
        {
            group_last_sf = nodes[end_band - 1][best_book][best_run].last_sf;
            group_last_is = nodes[end_band - 1][best_book][best_run].last_is;
            group_last_pns = nodes[end_band - 1][best_book][best_run].last_pns;
            b = end_band - 1;
            while (b >= start_band)
            {
                int pb = nodes[b][best_book][best_run].prev_book;
                int pr = nodes[b][best_book][best_run].prev_run;
                coder->book[b] = best_book;
                // Re-calculate the sf for this band based on the best path's last_sf
                if (best_book == HCB_INTENSITY || best_book == HCB_INTENSITY2)
                {
                    coder->sf[b] = nodes[b][best_book][best_run].last_is;
                }
                else if (best_book == HCB_PNS)
                {
                    coder->sf[b] = nodes[b][best_book][best_run].last_pns;
                }
                else if (best_book != HCB_ZERO)
                {
                    coder->sf[b] = nodes[b][best_book][best_run].last_sf;
                }
                best_book = pb;
                best_run = pr;
                b--;
            }
        }

        start_band = end_band;
    }

    free(nodes);

    // Finally, populate the bitstream codewords with the selected books
    coder->datacnt = 0;
    for (band = 0; band < coder->bandcnt; band++)
    {
        if (coder->book[band] > HCB_ZERO && coder->book[band] <= HCB_ESC)
        {
            huffcode(&coder->xi[coder->band_offset[band]], coder->band_len[band], coder->book[band], coder);
        }
    }

    // Update global_gain to the first non-zero band's scalefactor
    for (band = 0; band < coder->bandcnt; band++)
    {
        int book = coder->book[band];
        if (book != HCB_ZERO && book != HCB_INTENSITY && book != HCB_INTENSITY2)
        {
            coder->global_gain = coder->sf[band];
            break;
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
