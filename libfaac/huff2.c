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


/* Lowest base codebook whose range-pair covers |maxq| (keeps the decoder in
 * range, cf. the escape/out-of-range guards). Returns the lower book of the
 * pair; HCB_ZERO for an empty band, HCB_ESC for the escape book. Shared by
 * huffbook() (per-band selection) and section_optimize() (merge re-costing). */
static int book_for_maxq(int maxq)
{
    if (maxq < 1)  return HCB_ZERO;
    if (maxq < 2)  return 1;
    if (maxq < 3)  return 3;
    if (maxq < 5)  return 5;
    if (maxq < 8)  return 7;
    if (maxq < 13) return 9;
    return HCB_ESC;
}

/* Select the cheapest codebook for one band and cache its spectral bit cost in
 * coder->blen[]; symbols are NOT emitted here (deferred to emit_spectral after
 * section_optimize has finalised the books). */
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
        /* range-pair: keep whichever of {base, base+1} codes the band shorter */
        int len2;
        lenmin = huffcode(qs, len, bookmin, 0);
        len2 = huffcode(qs, len, bookmin + 1, 0);
        if (len2 < lenmin)
        {
            bookmin++;
            lenmin = len2;
        }
    }

    coder->book[coder->bandcnt] = bookmin;
    coder->blen[coder->bandcnt] = lenmin;

    return 0;
}

/* Range tier of a spectral book (higher tier covers larger |coef|); 0 for
 * HCB_ZERO and non-spectral books. */
static int book_tier(int book)
{
    switch (book)
    {
    case 1: case 2:  return 1;
    case 3: case 4:  return 2;
    case 5: case 6:  return 3;
    case 7: case 8:  return 4;
    case 9: case 10: return 5;
    case HCB_ESC:    return 6;
    default:         return 0;
    }
}

static int tier_base_book(int tier)
{
    switch (tier)
    {
    case 1: return 1;
    case 2: return 3;
    case 3: return 5;
    case 4: return 7;
    case 5: return 9;
    case 6: return HCB_ESC;
    default: return HCB_ZERO;
    }
}

/* A band carries Huffman-coded spectral data iff its book is in [1, HCB_ESC].
 * HCB_ZERO/PNS/intensity carry none and act as hard section boundaries. */
static int is_spectral(int book)
{
    return book >= 1 && book <= HCB_ESC;
}

/* Bit cost of coding the contiguous span of bands [first..last] as one section
 * under a single book. The span's quantized coeffs are packed back-to-back in
 * coder->qspec[], so a single huffcode() count over the whole run is exact. */
static int span_cost(CoderInfo *coder, int first, int last, int book)
{
    int ofs = coder->qoffset[first];
    int len = coder->qoffset[last] + coder->qlen[last] - ofs;
    return huffcode(coder->qspec + ofs, len, book, 0);
}

/* Greedy section merge ("codebook hysteresis"): walk each group left to right
 * and absorb the next spectral band into the open section whenever re-coding the
 * span under a shared covering book costs fewer extra spectral bits than the
 * section header it would save. Lossless: it only re-assigns spectral books to
 * spectral bands, never touching scalefactors or band classes. */
void section_optimize(CoderInfo *coder)
{
    int shortwin = (coder->block_type == ONLY_SHORT_WINDOW);
    int maxcnt = shortwin ? 7 : 31;   /* section run length writebooks() can encode */
    int header = 4 + (shortwin ? 3 : 5); /* bits saved by dropping one section header */
    int g;

    for (g = 0; g < coder->groups.n; g++)
    {
        int band = g * coder->sfbn;
        int maxband = band + coder->sfbn;

        while (band < maxband)
        {
            int sec_start, sec_spec, sec_tier;

            if (!is_spectral(coder->book[band]))
            {
                band++;
                continue;
            }

            sec_start = band;
            sec_spec = coder->blen[band];
            sec_tier = book_tier(coder->book[band]);
            band++;

            /* Cap the run at maxcnt so the saved-header model stays exact. */
            while (band < maxband
                   && is_spectral(coder->book[band])
                   && (band - sec_start) < maxcnt)
            {
                int cand_tier = sec_tier;
                int t = book_tier(coder->book[band]);
                int base, best_book, best_cost, k;

                if (t > cand_tier)
                    cand_tier = t;
                base = tier_base_book(cand_tier);

                best_book = base;
                best_cost = span_cost(coder, sec_start, band, base);
                if (base != HCB_ESC)
                {
                    int c2 = span_cost(coder, sec_start, band, base + 1);
                    if (c2 < best_cost)
                    {
                        best_cost = c2;
                        best_book = base + 1;
                    }
                }

                if (best_cost - sec_spec - coder->blen[band] < header)
                {
                    for (k = sec_start; k <= band; k++)
                        coder->book[k] = best_book;
                    sec_spec = best_cost;
                    sec_tier = cand_tier;
                    band++;
                }
                else
                    break;
            }
        }
    }
}

/* Emit Huffman symbols for all spectral bands in band order, filling coder->s[]
 * for WriteSpectralData(). Per-band emission of a merged section yields the same
 * bits as one combined call (spectral data is a continuous tuple sequence). */
void emit_spectral(CoderInfo *coder)
{
    int b;

    coder->datacnt = 0;
    for (b = 0; b < coder->bandcnt; b++)
    {
        if (is_spectral(coder->book[b]))
            huffcode(coder->qspec + coder->qoffset[b], coder->qlen[b],
                     coder->book[b], coder);
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
