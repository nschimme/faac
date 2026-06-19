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

#ifdef __GNUC__
#define clz(x) __builtin_clz(x)
#else
static int clz(unsigned int x)
{
    int n = 0;
    if (x == 0) return 32;
    if (x <= 0x0000FFFF) { n += 16; x <<= 16; }
    if (x <= 0x00FFFFFF) { n += 8; x <<= 8; }
    if (x <= 0x0FFFFFFF) { n += 4; x <<= 4; }
    if (x <= 0x3FFFFFFF) { n += 2; x <<= 2; }
    if (x <= 0x7FFFFFFF) { n += 1; x <<= 1; }
    return n;
}
#endif

static int escape(int x, int *code)
{
    int preflen;
    int base;

    if (x > MAX_HUFF_ESC_VAL)
    {
        fprintf(stderr, "%s(%d): x_quant > %d\n", __FILE__, __LINE__, MAX_HUFF_ESC_VAL);
        return 0;
    }

    preflen = 31 - clz(x) - 4;
    base = 1 << (preflen + 4);

    *code = (1 << (preflen + 1)) - 2; // preflen 1s and a 0
    *code <<= (preflen + 4);
    *code |= (x - base);

    return (preflen << 1) + 5;
}

static int huffcode(int *qs /* quantized spectrum */,
                    int len,
                    int bnum,
                    CoderInfo *coder)
{
    const uint8_t *blens = huff_len + huff_offset[bnum - 1];
    const uint16_t *bdata = huff_data + huff_offset[bnum - 1];
    int bits = 0, blen;
    int ofs, *qp;
    int data = 0;
    int idx;
    int datacnt;

    if (coder)
        datacnt = coder->datacnt;
    else
        datacnt = 0;

    switch (bnum)
    {
    case 1:
    case 2:
        for(ofs = 0; ofs < len; ofs += 4)
        {
            int q0, q1, q2, q3;
            if (len - ofs >= 4) {
                qp = qs + ofs;
                q0 = qp[0]; q1 = qp[1]; q2 = qp[2]; q3 = qp[3];
            } else {
                int q[4] = {0};
                for (int j = 0; j < len - ofs; j++) q[j] = qs[ofs + j];
                q0 = q[0]; q1 = q[1]; q2 = q[2]; q3 = q[3];
            }

            if (!(q0 | q1 | q2 | q3)) {
                blen = blens[40];
                if (coder) {
                    coder->s[datacnt].data = bdata[40];
                    coder->s[datacnt++].len = blen;
                }
                bits += blen;
                continue;
            }
            idx = 27 * q0 + 9 * q1 + 3 * q2 + q3 + 40;
            if (idx < 0 || idx >= 81) return -1;
            blen = blens[idx];
            if (coder)
            {
                data = bdata[idx];
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
            int q0, q1, q2, q3;
            if (len - ofs >= 4) {
                qp = qs + ofs;
                q0 = qp[0]; q1 = qp[1]; q2 = qp[2]; q3 = qp[3];
            } else {
                int q[4] = {0};
                for (int j = 0; j < len - ofs; j++) q[j] = qs[ofs + j];
                q0 = q[0]; q1 = q[1]; q2 = q[2]; q3 = q[3];
            }

            if (!(q0 | q1 | q2 | q3)) {
                blen = blens[0];
                if (coder) {
                    coder->s[datacnt].data = bdata[0];
                    coder->s[datacnt++].len = blen;
                }
                bits += blen;
                continue;
            }
            int aq0 = abs(q0), aq1 = abs(q1), aq2 = abs(q2), aq3 = abs(q3);
            idx = 27 * aq0 + 9 * aq1 + 3 * aq2 + aq3;
            if (idx < 0 || idx >= 81) return -1;
            blen = blens[idx];
            if (!coder)
            {
                if(q0) blen++;
                if(q1) blen++;
                if(q2) blen++;
                if(q3) blen++;
            }
            else
            {
                data = bdata[idx];
                if(q0) { blen++; data <<= 1; if (q0 < 0) data |= 1; }
                if(q1) { blen++; data <<= 1; if (q1 < 0) data |= 1; }
                if(q2) { blen++; data <<= 1; if (q2 < 0) data |= 1; }
                if(q3) { blen++; data <<= 1; if (q3 < 0) data |= 1; }
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
            int q0, q1;
            if (len - ofs >= 2) {
                qp = qs + ofs;
                q0 = qp[0]; q1 = qp[1];
            } else {
                q0 = qs[ofs]; q1 = 0;
            }

            if (!(q0 | q1)) {
                blen = blens[40];
                if (coder) {
                    coder->s[datacnt].data = bdata[40];
                    coder->s[datacnt++].len = blen;
                }
                bits += blen;
                continue;
            }
            idx = 9 * q0 + q1 + 40;
            if (idx < 0 || idx >= 81) return -1;
            blen = blens[idx];
            if (coder)
            {
                data = bdata[idx];
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
            int q0, q1;
            if (len - ofs >= 2) {
                qp = qs + ofs;
                q0 = qp[0]; q1 = qp[1];
            } else {
                q0 = qs[ofs]; q1 = 0;
            }

            if (!(q0 | q1)) {
                blen = blens[0];
                if (coder) {
                    coder->s[datacnt].data = bdata[0];
                    coder->s[datacnt++].len = blen;
                }
                bits += blen;
                continue;
            }
            int aq0 = abs(q0), aq1 = abs(q1);
            idx = 8 * aq0 + aq1;
            if (idx < 0 || idx >= 64) return -1;
            blen = blens[idx];
            if (!coder)
            {
                if(q0) blen++;
                if(q1) blen++;
            }
            else
            {
                data = bdata[idx];
                if(q0) { blen++; data <<= 1; if (q0 < 0) data |= 1; }
                if(q1) { blen++; data <<= 1; if (q1 < 0) data |= 1; }
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
            int q0, q1;
            if (len - ofs >= 2) {
                qp = qs + ofs;
                q0 = qp[0]; q1 = qp[1];
            } else {
                q0 = qs[ofs]; q1 = 0;
            }

            if (!(q0 | q1)) {
                blen = blens[0];
                if (coder) {
                    coder->s[datacnt].data = bdata[0];
                    coder->s[datacnt++].len = blen;
                }
                bits += blen;
                continue;
            }
            int aq0 = abs(q0), aq1 = abs(q1);
            idx = 13 * aq0 + aq1;
            if (idx < 0 || idx >= 169) return -1;
            blen = blens[idx];
            if (!coder)
            {
                if(q0) blen++;
                if(q1) blen++;
            }
            else
            {
                data = bdata[idx];
                if(q0) { blen++; data <<= 1; if (q0 < 0) data |= 1; }
                if(q1) { blen++; data <<= 1; if (q1 < 0) data |= 1; }
                coder->s[datacnt].data = data;
                coder->s[datacnt++].len = blen;
            }
            bits += blen;
        }
        break;
    case HCB_ESC:
        for(ofs = 0; ofs < len; ofs += 2)
        {
            int q0, q1, aq0, aq1, x0, x1;
            if (len - ofs >= 2) {
                qp = qs + ofs;
                q0 = qp[0]; q1 = qp[1];
            } else {
                q0 = qs[ofs]; q1 = 0;
            }

            if (!(q0 | q1)) {
                blen = blens[0];
                if (coder) {
                    coder->s[datacnt].data = bdata[0];
                    coder->s[datacnt++].len = blen;
                }
                bits += blen;
                continue;
            }
            aq0 = abs(q0); aq1 = abs(q1);

            x0 = (aq0 > 16) ? 16 : aq0;
            x1 = (aq1 > 16) ? 16 : aq1;
            idx = 17 * x0 + x1;
            if (idx < 0 || idx >= 289) return -1;

            blen = blens[idx];
            if (!coder)
            {
                if(q0) blen++;
                if(q1) blen++;
            }
            else
            {
                data = bdata[idx];
                if(q0) { blen++; data <<= 1; if (q0 < 0) data |= 1; }
                if(q1) { blen++; data <<= 1; if (q1 < 0) data |= 1; }
                coder->s[datacnt].data = data;
                coder->s[datacnt++].len = blen;
            }
            bits += blen;

            if (aq0 >= 16)
            {
                blen = escape(aq0, &data);
                if (coder)
                {
                    coder->s[datacnt].data = data;
                    coder->s[datacnt++].len = blen;
                }
                bits += blen;
            }

            if (aq1 >= 16)
            {
                blen = escape(aq1, &data);
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


static inline int escape_len(int x)
{
    int preflen = 31 - clz(x) - 4;
    return (preflen << 1) + 5;
}

/* Multi-book Huffman bit counter.
 * Calculates spectral bit costs for all 11 AAC Huffman codebooks in a single pass.
 * Constraints:
 * - Books 1-4 (quads) require section length to be a multiple of 4.
 * - Books 5-11 (pairs) require section length to be a multiple of 2.
 * - Max quantized value must stay within each book's range.
 */
void huff_count_all_books(const int * __restrict qs, int len, int * __restrict costs, int * __restrict maxq_out)
{
    int i, b;
    int maxq = 0;
    int c[12] = {0};
    int can12, can34, can56, can78, can910;

    for (i = 0; i < len; i++) {
        int aq = abs(qs[i]);
        if (aq > maxq) maxq = aq;
    }
    *maxq_out = maxq;

    if (maxq == 0) {
        for (i = 1; i < 12; i++) costs[i] = 0;
        return;
    }

    /* Tuple size and range constraints from AAC spec. */
    can12 = (maxq <= 1) && (len % 4 == 0);
    can34 = (maxq <= 2) && (len % 4 == 0);
    can56 = (maxq <= 4) && (len % 2 == 0);
    can78 = (maxq <= 7) && (len % 2 == 0);
    can910 = (maxq <= 12) && (len % 2 == 0);

    const uint8_t *l1 = huff_len + huff_offset[0];
    const uint8_t *l2 = huff_len + huff_offset[1];
    const uint8_t *l3 = huff_len + huff_offset[2];
    const uint8_t *l4 = huff_len + huff_offset[3];
    const uint8_t *l5 = huff_len + huff_offset[4];
    const uint8_t *l6 = huff_len + huff_offset[5];
    const uint8_t *l7 = huff_len + huff_offset[6];
    const uint8_t *l8 = huff_len + huff_offset[7];
    const uint8_t *l9 = huff_len + huff_offset[8];
    const uint8_t *l10 = huff_len + huff_offset[9];
    const uint8_t *l11 = huff_len + huff_offset[10];

    for (i = 0; i < len; i += 4)
    {
        int q0 = 0, q1 = 0, q2 = 0, q3 = 0;
        int n = (len - i > 4) ? 4 : len - i;
        q0 = qs[i];
        if (n > 1) q1 = qs[i+1];
        if (n > 2) q2 = qs[i+2];
        if (n > 3) q3 = qs[i+3];

        if (!(q0 | q1 | q2 | q3))
        {
            if (can12) { c[1] += l1[40]; c[2] += l2[40]; }
            if (can34) { c[3] += l3[0];  c[4] += l4[0];  }
            if (can56) { c[5] += (n/2) * l5[40]; c[6] += (n/2) * l6[40]; }
            if (can78) { c[7] += (n/2) * l7[0];  c[8] += (n/2) * l8[0];  }
            if (can910) { c[9] += (n/2) * l9[0];  c[10] += (n/2) * l10[0]; }
            c[11] += (n/2) * l11[0];
            continue;
        }

        int aq0 = abs(q0), aq1 = abs(q1), aq2 = abs(q2), aq3 = abs(q3);
        int s0 = (q0 != 0), s1 = (q1 != 0), s2 = (q2 != 0), s3 = (q3 != 0);

        if (can12) {
            int idx = 27 * q0 + 9 * q1 + 3 * q2 + q3 + 40;
            c[1] += l1[idx];
            c[2] += l2[idx];
        }

        if (can34) {
            int idx = 27 * aq0 + 9 * aq1 + 3 * aq2 + aq3;
            int s = s0 + s1 + s2 + s3;
            c[3] += l3[idx] + s;
            c[4] += l4[idx] + s;
        }

        // Books 5-11 (2-tuples)
        for (b = 0; b < n; b += 2)
        {
            int tq0 = (b == 0) ? q0 : q2;
            int tq1 = (b == 0) ? q1 : q3;
            int taq0 = (b == 0) ? aq0 : aq2;
            int taq1 = (b == 0) ? aq1 : aq3;
            int ts0 = (b == 0) ? s0 : s2;
            int ts1 = (b == 0) ? s1 : s3;

            if (can56) {
                int idx = 9 * tq0 + tq1 + 40;
                c[5] += l5[idx];
                c[6] += l6[idx];
            }

            if (can78) {
                int idx = 8 * taq0 + taq1;
                c[7] += l7[idx] + ts0 + ts1;
                c[8] += l8[idx] + ts0 + ts1;
            }

            if (can910) {
                int idx = 13 * taq0 + taq1;
                c[9] += l9[idx] + ts0 + ts1;
                c[10] += l10[idx] + ts0 + ts1;
            }

            int x0 = (taq0 > 16) ? 16 : taq0;
            int x1 = (taq1 > 16) ? 16 : taq1;
            c[11] += l11[17 * x0 + x1] + ts0 + ts1;
            if (taq0 >= 16) c[11] += escape_len(taq0);
            if (taq1 >= 16) c[11] += escape_len(taq1);
        }
    }

    if (!can12) c[1] = c[2] = 1000000;
    if (!can34) c[3] = c[4] = 1000000;
    if (!can56) c[5] = c[6] = 1000000;
    if (!can78) c[7] = c[8] = 1000000;
    if (!can910) c[9] = c[10] = 1000000;
    if (len % 2 != 0) c[11] = 1000000;

    for (i = 1; i < 12; i++) costs[i] = c[i];
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
    int *costs = coder->band_costs[coder->bandcnt];
    int maxq;
    int bookmin, lenmin;

    if (len == 0)
    {
        coder->book[coder->bandcnt] = HCB_ZERO;
        coder->blen[coder->bandcnt] = 0;
        for (int i = 1; i < 12; i++) costs[i] = 0;
        return 0;
    }

    huff_count_all_books(qs, len, costs, &maxq);

    bookmin = book_for_maxq(maxq);
    if (bookmin == HCB_ZERO)
    {
        lenmin = 0;
    }
    else if (bookmin == HCB_ESC)
    {
        lenmin = costs[HCB_ESC];
    }
    else
    {
        /* range-pair: keep whichever of {base, base+1} codes the band shorter */
        lenmin = costs[bookmin];
        int len2 = costs[bookmin + 1];
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
            int costs[12];

            if (!is_spectral(coder->book[band]))
            {
                band++;
                continue;
            }

            sec_start = band;
            sec_spec = coder->blen[band];
            sec_tier = book_tier(coder->book[band]);
            for (int i = 1; i < 12; i++) costs[i] = coder->band_costs[band][i];
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

                // Update fused cost for the whole span
                for (int i = 1; i < 12; i++) costs[i] += coder->band_costs[band][i];

                best_book = base;
                best_cost = costs[base];
                if (base != HCB_ESC)
                {
                    int c2 = costs[base + 1];
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
                {
                    // Roll back costs if merge is rejected
                    for (int i = 1; i < 12; i++) costs[i] -= coder->band_costs[band][i];
                    break;
                }
            }
        }
    }
}

/* Emit Huffman symbols for all spectral bands in band order, filling coder->s[]
 * for WriteSpectralData(). Adjacent bands with the same book are combined into
 * a single huffcode call to maximize throughput. */
void emit_spectral(CoderInfo *coder)
{
    int b;

    coder->datacnt = 0;
    for (b = 0; b < coder->bandcnt; )
    {
        int book = coder->book[b];
        if (is_spectral(book))
        {
            int ofs = coder->qoffset[b];
            int len = coder->qlen[b];
            b++;
            while (b < coder->bandcnt && coder->book[b] == book)
            {
                len += coder->qlen[b];
                b++;
            }
            huffcode(coder->qspec + ofs, len, book, coder);
        }
        else
        {
            b++;
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
            length = book12_len[SF_DELTA + diff];

            bits += length;

            lastis += diff;

            if (write)
                PutBit(stream, book12_data[SF_DELTA + diff], length);
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

            length = book12_len[SF_DELTA + diff];
            bits += length;
            lastpns += diff;

            if (write)
                PutBit(stream, book12_data[SF_DELTA + diff], length);
        }
        else if ((book != HCB_ZERO) && (book != HCB_NONE))
        {
            diff = coder->sf[cnt] - lastsf;
            diff = clamp_sf_diff(diff);
            length = book12_len[SF_DELTA + diff];

            bits += length;
            lastsf += diff;

            if (write)
                PutBit(stream, book12_data[SF_DELTA + diff], length);
        }

    }
    return bits;
}
