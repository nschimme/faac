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
#include <string.h>
#include "coder.h"
#include "huffdata.h"
#include "huff2.h"
#include "bitstream.h"

#ifdef DRM
static int vcb11;
#endif

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

#ifdef DRM
# define DRMDATA if(coder){coder->num_data_cw[coder->cur_cw++]=1;\
    coder->iLenReordSpData+=blen;if(coder->iLenLongestCW<blen)\
    coder->iLenLongestCW=blen;}
#else
# define DRMDATA
#endif

#define arrlen(array) (sizeof(array) / sizeof(*array))

int huffcode(int *qs /* quantized spectrum */,
             int len,
             int bnum,
             CoderInfo *coder)
{
    static hcode16_t * const hmap[12] = {0, book01, book02, book03, book04,
      book05, book06, book07, book08, book09, book10, book11};
    hcode16_t *book = NULL;
    int cnt;
    int bits = 0, blen;
    int ofs, *qp;
    int data;
    int idx;
    int datacnt;
#ifdef DRM
    int drmbits = 0;
    int maxesc = 0;
#endif

    if (coder)
        datacnt = coder->datacnt;
    else
        datacnt = 0;

    if (bnum >= 1 && bnum <= 11)
        book = hmap[bnum];

    switch (bnum)
    {
    case HCB_ZERO:
        for (cnt = 0; cnt < len; cnt++)
            if (qs[cnt]) return -1;
#ifdef DRM
        if (coder) {
            for(ofs = 0; ofs < len; ofs += 4)
            {
                coder->s[datacnt].data = 0;
                coder->s[datacnt++].len = 0;
                coder->num_data_cw[coder->cur_cw++] = 1;
            }
        }
#endif
        return 0;

    case HCB_INTENSITY:
    case HCB_INTENSITY2:
    case HCB_PNS:
#ifdef DRM
        if (coder) {
            for(ofs = 0; ofs < len; ofs += 4)
            {
                coder->s[datacnt].data = 0;
                coder->s[datacnt++].len = 0;
                coder->num_data_cw[coder->cur_cw++] = 1;
            }
        }
#endif
        return 0;
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
                DRMDATA;
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
                DRMDATA;
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
                DRMDATA;
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
                DRMDATA;
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
                DRMDATA;
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
#ifdef DRM
                coder->num_data_cw[coder->cur_cw] = 1;
                drmbits = blen;
#endif
            }
            bits += blen;

            if (x0 >= 16)
            {
                blen = escape(abs(qp[0]), &data);
                if (coder)
                {
                    coder->s[datacnt].data = data;
                    coder->s[datacnt++].len = blen;
#ifdef DRM
                    coder->num_data_cw[coder->cur_cw]++;
                    drmbits += blen;

                    if (maxesc < data)
                        maxesc = data;
#endif
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
#ifdef DRM
                    coder->num_data_cw[coder->cur_cw]++;
                    drmbits += blen;

                    if (maxesc < data)
                        maxesc = data;
#endif
                }
                bits += blen;
            }
#ifdef DRM
            coder->iLenReordSpData += drmbits;
            if (coder->iLenLongestCW < drmbits)
                coder->iLenLongestCW = drmbits;

            coder->cur_cw++;
#endif
        }
#ifdef DRM
        /* VCB11: check which codebook should be used using max escape sequence */
        /* 8.5.3.1.3, table 157 */
        if (maxesc <= 15)
            vcb11 = 16;
        else if (maxesc <= 31)
            vcb11 = 17;
        else if (maxesc <= 47)
            vcb11 = 18;
        else if (maxesc <= 63)
            vcb11 = 19;
        else if (maxesc <= 95)
            vcb11 = 20;
        else if (maxesc <= 127)
            vcb11 = 21;
        else if (maxesc <= 159)
            vcb11 = 22;
        else if (maxesc <= 191)
            vcb11 = 23;
        else if (maxesc <= 223)
            vcb11 = 24;
        else if (maxesc <= 255)
            vcb11 = 25;
        else if (maxesc <= 319)
            vcb11 = 26;
        else if (maxesc <= 383)
            vcb11 = 27;
        else if (maxesc <= 511)
            vcb11 = 28;
        else if (maxesc <= 767)
            vcb11 = 29;
        else if (maxesc <= 1023)
            vcb11 = 30;
        else if (maxesc <= 2047)
            vcb11 = 31;
        /* else: codebook 11 -> it is already 11 */
#endif
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
    int bookmin = HCB_ZERO;
    int lenmin = 0;
    int book;
    int prev_book = HCB_ZERO;
    int section_overhead;

    for (cnt = 0; cnt < len; cnt++)
    {
        int q = abs(qs[cnt]);
        if (maxq < q)
            maxq = q;
    }

    if (maxq > 0)
    {
        int start_book = 1;
        if (maxq >= 13) start_book = 11;
        else if (maxq >= 8) start_book = 9;
        else if (maxq >= 5) start_book = 7;
        else if (maxq >= 3) start_book = 5;
        else if (maxq >= 2) start_book = 3;

        if (coder->bandcnt > 0)
            prev_book = coder->book[coder->bandcnt - 1];

        if (prev_book < 1 || prev_book > 11)
            prev_book = HCB_ZERO;

        /* Standard section overhead: 3 bits for short, 5 bits for long block length
           plus 4 bits for codebook index = 7 or 9 bits.
           Actually for long blocks it can be up to 5 bits for length, so 9 is a safe min. */
        section_overhead = (coder->block_type == ONLY_SHORT_WINDOW) ? 7 : 9;

        lenmin = 0x7FFFFFFF;
        bookmin = start_book;

        for (book = start_book; book <= 11; book++)
        {
            int bits = huffcode(qs, len, book, 0);
            if (bits >= 0)
            {
                if (book == prev_book)
                    bits -= section_overhead;

                if (bits < lenmin)
                {
                    lenmin = bits;
                    bookmin = book;
                }
            }
        }
    }

#ifdef DRM
    vcb11 = 0;
    huffcode(qs, len, bookmin, coder);
    if (vcb11)
        bookmin = vcb11;
#else
    if (bookmin > HCB_ZERO)
        huffcode(qs, len, bookmin, coder);
#endif
    coder->book[coder->bandcnt] = bookmin;

    return 0;
}

void huff_trellis(CoderInfo *coder, int (*cost)[12], int (*path)[12])
{
    int group, band, book;
    int section_overhead = (coder->block_type == ONLY_SHORT_WINDOW) ? 7 : 15;
    int best_books[MAX_SCFAC_BANDS];

    /* Initialize Trellis nodes */
    for (band = 0; band < coder->bandcnt; band++)
        for (book = 0; book <= 11; book++)
            cost[band][book] = 0x3FFFFFFF;

    coder->datacnt = 0;

    /* Process groups independently */
    int win_offset = 0;
    for (group = 0; group < coder->groups.n; group++)
    {
        int first_band = group * coder->sfbn;
        int group_size = coder->groups.len[group];

        /* Forward pass using pre-calculated bits */
        for (band = first_band; band < (group + 1) * coder->sfbn; band++)
        {
            int is_non_spectral = (coder->book[band] >= 13 && coder->book[band] < HCB_NONE);

            if (is_non_spectral) {
                int min_prev = 0x3FFFFFFF;
                int best_prev = 0;
                if (band == first_band) {
                    min_prev = 0;
                    best_prev = -1;
                } else {
                    for (int b = 0; b <= 11; b++) {
                        if (cost[band-1][b] < min_prev) {
                            min_prev = cost[band-1][b];
                            best_prev = b;
                        }
                    }
                }
                for (int b = 0; b <= 11; b++) {
                    cost[band][b] = min_prev;
                    path[band][b] = best_prev;
                }
                continue;
            }

            for (book = 0; book <= 11; book++)
            {
                int bits = coder->huff_bits[band][book];
                if (bits < 0) continue;

                if (band == first_band || (coder->book[band-1] >= 13 && coder->book[band-1] < HCB_NONE))
                {
                    int prev_min = 0;
                    int prev_best = -1;
                    if (band > first_band) {
                        prev_min = cost[band-1][0];
                        prev_best = path[band-1][0];
                    }
                    cost[band][book] = prev_min + bits + section_overhead;
                    path[band][book] = prev_best;
                }
                else
                {
                    for (int prev_book = 0; prev_book <= 11; prev_book++)
                    {
                        if (cost[band - 1][prev_book] >= 0x3FFFFFFF) continue;

                        int total_cost = cost[band - 1][prev_book] + bits;
                        if (book != prev_book)
                            total_cost += section_overhead;

                        if (total_cost < cost[band][book])
                        {
                            cost[band][book] = total_cost;
                            path[band][book] = prev_book;
                        }
                    }
                }
            }
        }

        /* Backward pass */
        int last_band = (group + 1) * coder->sfbn - 1;
        int min_cost = 0x3FFFFFFF;
        int best_book = 0;
        for (book = 0; book <= 11; book++) {
            if (cost[last_band][book] < min_cost) {
                min_cost = cost[last_band][book];
                best_book = book;
            }
        }

        for (band = last_band; band >= first_band; band--) {
            int is_non_spectral = (coder->book[band] >= 13 && coder->book[band] < HCB_NONE);
            if (!is_non_spectral) {
                best_books[band] = best_book;
            }
            best_book = path[band][best_book];
            if (best_book < 0) best_book = 0;
        }

        /* Final pass: actually encode spectral data into bitstream buffer */
        for (band = first_band; band < (group + 1) * coder->sfbn; band++)
        {
            int is_non_spectral = (coder->book[band] >= 13 && coder->book[band] < HCB_NONE);
            if (!is_non_spectral) {
                int chosen_book = best_books[band];
                coder->book[band] = chosen_book;

                if (chosen_book > 0) {
                    int sb = band % coder->sfbn;
                    int start = coder->sfb_offset[sb];
                    int end = coder->sfb_offset[sb + 1];
                    int len = end - start;

                    for (int w = 0; w < group_size; w++) {
                        int *qs = coder->quantized_spectra + (win_offset + w) * BLOCK_LEN_SHORT + start;
                        huffcode(qs, len, chosen_book, coder);
                    }
                }
            }
        }
        win_offset += group_size;
    }
}

int writebooks(CoderInfo *coder, BitStream *stream, int write)
{
    int bits = 0;
    int maxcnt, cntbits;
    int group;
    int bookbits = 4;

#ifdef DRM
    bookbits = 5; /* 5 bits in case of VCB11 */
#endif

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

#ifdef DRM
            /* sect_len is not transmitted in case the codebook for a */
            /* section is 11 or in the range of 16 and 31 */
            if ((book == 11) || ((book >= 16) && (book <= 32)))
                continue;
#endif

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
