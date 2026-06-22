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
    along with this program.  See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  See the GNU General Public License for more details.

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

/* Escape suffix for HCB_ESC: a magnitude |q| >= LAV_ESC is sent as the pair
 * index LAV_ESC (emitted by the caller) plus this suffix - a unary prefix of
 * `preflen` ones and a zero, then the low `preflen+4` bits of x.
 * preflen counts how far x is past the 16-window, so the suffix is 2*preflen+5
 * bits. Uses CountLeadingZeros for O(1) bit-length calculation. */
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

/* Tuple fetch: safely reads up to N coefficients from [qs+ofs, len), zero-padding
 * if the band end is reached. Prevents out-of-bounds reads for misaligned SFBs. */
#define FETCH4(q, qs, ofs, len) \
    int q[4] = {0}; \
    if ((len) - (ofs) >= 4) { \
        q[0] = (qs)[(ofs)]; q[1] = (qs)[(ofs)+1]; q[2] = (qs)[(ofs)+2]; q[3] = (qs)[(ofs)+3]; \
    } else { \
        for (int j = 0; j < (len) - (ofs); j++) q[j] = (qs)[(ofs) + j]; \
    }

#define FETCH2(q, qs, ofs, len) \
    int q[2] = {0}; \
    if ((len) - (ofs) >= 2) { \
        q[0] = (qs)[(ofs)]; q[1] = (qs)[(ofs)+1]; \
    } else if ((len) - (ofs) == 1) { \
        q[0] = (qs)[(ofs)]; \
    }

/* Tuple indexing: maps signed or magnitude tuples into the polynomial space
 * of the Huffman LUTs. Signed books fold sign into the index (biased by 40)
 * to eliminate explicit sign bits for low-energy coefficients. */
#define IDX_S4(q) (DIM_S4*DIM_S4*DIM_S4*(q)[0] + DIM_S4*DIM_S4*(q)[1] + DIM_S4*(q)[2] + (q)[3] + 40)
#define IDX_S2(q) (DIM_S2*(q)[0] + (q)[1] + 40)
#define IDX_M4(q) (DIM_M4*DIM_M4*DIM_M4*abs((q)[0]) + DIM_M4*DIM_M4*abs((q)[1]) + DIM_M4*abs((q)[2]) + abs((q)[3]))
#define IDX_M2(q, b) ((b)*abs((q)[0]) + abs((q)[1]))

/* Magnitude sign accounting: appends sign bits to the bit count (always) and the
 * codeword (if coder != NULL) for magnitude-mapped codebooks. */
#define ADD_SIGNS(bits, data, q, n, coder) \
    for (int j = 0; j < (n); j++) { \
        if ((q)[j]) { \
            (bits)++; \
            if (coder) { (data) <<= 1; if ((q)[j] < 0) (data) |= 1; } \
        } \
    }

/* Traversal helpers: encapsulate the loop boilerplate for spectral processing. */
#define SIGNED_STEP(n, fetch_macro, idx_macro) \
    fetch_macro(q, qs, ofs, len); \
    idx = idx_macro; \
    blen = blens[idx]; \
    if (coder) { \
        coder->s[datacnt].data = bdata[idx]; \
        coder->s[datacnt++].len = blen; \
    } \
    bits += blen;

#define MAGNITUDE_STEP4(fetch_macro, idx_macro) \
    fetch_macro(q, qs, ofs, len); \
    if (!(q[0]|q[1]|q[2]|q[3])) { \
        blen = blens[0]; \
        if (coder) { coder->s[datacnt].data = bdata[0]; coder->s[datacnt++].len = blen; } \
    } else { \
        idx = idx_macro; blen = blens[idx]; \
        if (coder) data = bdata[idx]; \
        ADD_SIGNS(blen, data, q, 4, coder); \
        if (coder) { coder->s[datacnt].data = data; coder->s[datacnt++].len = blen; } \
    } \
    bits += blen;

#define MAGNITUDE_STEP2(fetch_macro, idx_macro) \
    fetch_macro(q, qs, ofs, len); \
    if (!(q[0]|q[1])) { \
        blen = blens[0]; \
        if (coder) { coder->s[datacnt].data = bdata[0]; coder->s[datacnt++].len = blen; } \
    } else { \
        idx = idx_macro; blen = blens[idx]; \
        if (coder) data = bdata[idx]; \
        ADD_SIGNS(blen, data, q, 2, coder); \
        if (coder) { coder->s[datacnt].data = data; coder->s[datacnt++].len = blen; } \
    } \
    bits += blen;

/* Code `len` coeffs under book `bnum`, returning bit count. coder == NULL only
 * sizes (the hot cost path); else also emits codewords to coder->s[]. len/code
 * tables are split (huff_len/huff_data, sliced by huff_offset) so the cost path
 * streams uint8 lengths only to minimize cache pressure. */
static int huffcode(int *qs /* quantized spectrum */,
                    int len,
                    int bnum,
                    CoderInfo *coder)
{
    const uint8_t *blens = huff_len + huff_offset[bnum - 1];
    const uint16_t *bdata = huff_data + huff_offset[bnum - 1];
    int bits = 0, blen, ofs, idx, datacnt = coder ? coder->datacnt : 0;
    int data = 0;

    switch (bnum)
    {
    case HCB_1:
    case HCB_2:
        for (ofs = 0; ofs < len; ofs += 4) { SIGNED_STEP(4, FETCH4, IDX_S4(q)); }
        break;
    case HCB_3:
    case HCB_4:
        for (ofs = 0; ofs < len; ofs += 4) { MAGNITUDE_STEP4(FETCH4, IDX_M4(q)); }
        break;
    case HCB_5:
    case HCB_6:
        for (ofs = 0; ofs < len; ofs += 2) { SIGNED_STEP(2, FETCH2, IDX_S2(q)); }
        break;
    case HCB_7:
    case HCB_8:
        for (ofs = 0; ofs < len; ofs += 2) { MAGNITUDE_STEP2(FETCH2, IDX_M2(q, DIM_M2_7)); }
        break;
    case HCB_9:
    case HCB_10:
        for (ofs = 0; ofs < len; ofs += 2) { MAGNITUDE_STEP2(FETCH2, IDX_M2(q, DIM_M2_12)); }
        break;
    case HCB_ESC:
        for (ofs = 0; ofs < len; ofs += 2)
        {
            FETCH2(q, qs, ofs, len);
            if (!(q[0] | q[1]))
            {
                blen = blens[0];
                if (coder) { coder->s[datacnt].data = bdata[0]; coder->s[datacnt++].len = blen; }
            }
            else
            {
                int aq0 = abs(q[0]), aq1 = abs(q[1]);
                int x0 = (aq0 > LAV_ESC) ? LAV_ESC : aq0;
                int x1 = (aq1 > LAV_ESC) ? LAV_ESC : aq1;
                idx = DIM_ESC * x0 + x1;
                blen = blens[idx];
                if (coder) data = bdata[idx];
                ADD_SIGNS(blen, data, q, 2, coder);
                if (coder) { coder->s[datacnt].data = data; coder->s[datacnt++].len = blen; }
                if (aq0 >= LAV_ESC)
                {
                    int eb = escape(aq0, &data);
                    bits += eb;
                    if (coder) { coder->s[datacnt].data = data; coder->s[datacnt++].len = eb; }
                }
                if (aq1 >= LAV_ESC)
                {
                    int eb = escape(aq1, &data);
                    bits += eb;
                    if (coder) { coder->s[datacnt].data = data; coder->s[datacnt++].len = eb; }
                }
            }
            bits += blen;
        }
        break;
    default:
        fprintf(stderr, "%s(%d) book %d out of range\n", __FILE__, __LINE__, bnum);
        return -1;
    }

    if (coder) coder->datacnt = datacnt;
    return bits;
}

/* Bit cost of coding [qs,len) under ALL spectral books in one traversal.
 * Results match multiple huffcode(..., 0) calls but minimize cache pressure by
 * streaming uint8 lengths for all books simultaneously. Fused 4-tuple traversal
 * handles both quad and pair books with a sparse-spectrum fast-path. */
static void huff_count_all_books(const int *qs, int len, int maxq, int *costs)
{
    int ofs, k;
    static const int book_lav[12] = {0, LAV_1, LAV_1, LAV_2, LAV_2, LAV_4, LAV_4, LAV_7, LAV_7, LAV_12, LAV_12, 65535};

    for (k = 1; k <= 11; k++)
    {
        /* Sentinel for books whose range doesn't cover |maxq| */
        costs[k] = (maxq > book_lav[k]) ? -1 : 0;
    }

    if (maxq == 0)
    {
        /* Zeros cost exactly 1 codeword per tuple (quad or pair) */
        if (costs[1] >= 0) costs[1] = ((len + 3) >> 2) * huff_len[huff_offset[0] + 40];
        if (costs[2] >= 0) costs[2] = ((len + 3) >> 2) * huff_len[huff_offset[1] + 40];
        if (costs[3] >= 0) costs[3] = ((len + 3) >> 2) * huff_len[huff_offset[2]];
        if (costs[4] >= 0) costs[4] = ((len + 3) >> 2) * huff_len[huff_offset[3]];
        for (k = 5; k <= 11; k++)
        {
            if (costs[k] >= 0)
            {
                int zidx = (k <= 6) ? 40 : 0;
                costs[k] = ((len + 1) >> 1) * huff_len[huff_offset[k-1] + zidx];
            }
        }
        return;
    }

    /* Fast cached pointers to codeword length tables for books 1..11 */
    const uint8_t *bl[12];
    for (k = 1; k <= 11; k++) bl[k] = huff_len + huff_offset[k-1];

    for (ofs = 0; ofs < len; ofs += 4)
    {
        FETCH4(q, qs, ofs, len);

        if (!(q[0] | q[1] | q[2] | q[3]))
        {
            /* 4 zeros: costs 1 quad codeword or 2 pair codewords */
            if (costs[1] >= 0) costs[1] += bl[1][40];
            if (costs[2] >= 0) costs[2] += bl[2][40];
            if (costs[3] >= 0) costs[3] += bl[3][0];
            if (costs[4] >= 0) costs[4] += bl[4][0];
            if (costs[5] >= 0) costs[5] += 2*bl[5][40];
            if (costs[6] >= 0) costs[6] += 2*bl[6][40];
            if (costs[7] >= 0) costs[7] += 2*bl[7][0];
            if (costs[8] >= 0) costs[8] += 2*bl[8][0];
            if (costs[9] >= 0) costs[9] += 2*bl[9][0];
            if (costs[10] >= 0) costs[10] += 2*bl[10][0];
            if (costs[11] >= 0) costs[11] += 2*bl[11][0];
            continue;
        }

        int s0 = (q[0] != 0), s1 = (q[1] != 0), s2 = (q[2] != 0), s3 = (q[3] != 0);
        int a0 = abs(q[0]), a1 = abs(q[1]), a2 = abs(q[2]), a3 = abs(q[3]);

        /* Quad books (1-4) */
        if (costs[1] >= 0 || costs[2] >= 0 || costs[3] >= 0 || costs[4] >= 0)
        {
            int idx = IDX_S4(q);
            if (costs[1] >= 0) costs[1] += bl[1][idx];
            if (costs[2] >= 0) costs[2] += bl[2][idx];
            if (costs[3] >= 0 || costs[4] >= 0)
            {
                int aidx = IDX_M4(q);
                int signbits = s0 + s1 + s2 + s3;
                if (costs[3] >= 0) costs[3] += bl[3][aidx] + signbits;
                if (costs[4] >= 0) costs[4] += bl[4][aidx] + signbits;
            }
        }

        /* Pair books (5-11) */
        int idx_s_01 = DIM_S2*q[0] + q[1] + 40, idx_s_23 = DIM_S2*q[2] + q[3] + 40;
        int idx_m_01_7 = DIM_M2_7*a0 + a1, idx_m_23_7 = DIM_M2_7*a2 + a3;
        int idx_m_01_12 = DIM_M2_12*a0 + a1, idx_m_23_12 = DIM_M2_12*a2 + a3;
        int sign_01 = s0 + s1, sign_23 = s2 + s3;

        if (costs[5] >= 0) costs[5] += bl[5][idx_s_01] + bl[5][idx_s_23];
        if (costs[6] >= 0) costs[6] += bl[6][idx_s_01] + bl[6][idx_s_23];
        if (costs[7] >= 0) costs[7] += bl[7][idx_m_01_7] + sign_01 + bl[7][idx_m_23_7] + sign_23;
        if (costs[8] >= 0) costs[8] += bl[8][idx_m_01_7] + sign_01 + bl[8][idx_m_23_7] + sign_23;
        if (costs[9] >= 0) costs[9] += bl[9][idx_m_01_12] + sign_01 + bl[9][idx_m_23_12] + sign_23;
        if (costs[10] >= 0) costs[10] += bl[10][idx_m_01_12] + sign_01 + bl[10][idx_m_23_12] + sign_23;
        if (costs[11] >= 0)
        {
            int dummy;
            int x0 = (a0 > 16) ? 16 : a0, x1 = (a1 > 16) ? 16 : a1;
            int x2 = (a2 > 16) ? 16 : a2, x3 = (a3 > 16) ? 16 : a3;
            costs[11] += bl[11][DIM_ESC*x0 + x1] + sign_01;
            if (a0 >= 16) costs[11] += escape(a0, &dummy);
            if (a1 >= 16) costs[11] += escape(a1, &dummy);
            costs[11] += bl[11][DIM_ESC*x2 + x3] + sign_23;
            if (a2 >= 16) costs[11] += escape(a2, &dummy);
            if (a3 >= 16) costs[11] += escape(a3, &dummy);
        }
    }
}

/* Lowest base codebook whose range-pair covers |maxq|. */
static int book_for_maxq(int maxq)
{
    if (maxq < 1) return HCB_ZERO;
    if (maxq < LAV_1 + 1) return HCB_1;
    if (maxq < LAV_2 + 1) return HCB_3;
    if (maxq < LAV_4 + 1) return HCB_5;
    if (maxq < LAV_7 + 1) return HCB_7;
    if (maxq < LAV_12 + 1) return HCB_9;
    return HCB_ESC;
}

/* Select the cheapest codebook for one band and cache bit costs for ALL spectral
 * books in coder->band_costs[][]. This fused O(N) costing allows section_optimize
 * to re-cost merged sections in O(1) by simply summing table entries. */
int huffbook(CoderInfo *coder,
             int *qs /* quantized spectrum */,
             int len)
{
    int bandcnt = coder->bandcnt;
    int maxq = 0, i, best_book = HCB_ZERO, min_len = -1;

    if (len == 0)
    {
        coder->book[bandcnt] = HCB_ZERO;
        coder->blen[bandcnt] = 0;
        coder->maxq[bandcnt] = 0;
        for (i = 1; i <= 11; i++) coder->band_costs[bandcnt][i] = 0;
        return 0;
    }

    for (i = 0; i < len; i++)
    {
        int aq = abs(qs[i]);
        if (aq > maxq) maxq = aq;
    }
    coder->maxq[bandcnt] = maxq;

    huff_count_all_books(qs, len, maxq, coder->band_costs[bandcnt]);

    /* Pick the cheapest legal book for this band (base or base+1) */
    int base = book_for_maxq(maxq);
    if (base == HCB_ESC)
    {
        best_book = HCB_ESC;
        min_len = coder->band_costs[bandcnt][HCB_ESC];
    }
    else if (base != HCB_ZERO)
    {
        int c0 = coder->band_costs[bandcnt][base];
        int c1 = coder->band_costs[bandcnt][base+1];
        if (c1 >= 0 && (c0 < 0 || c1 < c0))
        {
            best_book = base + 1;
            min_len = c1;
        }
        else
        {
            best_book = base;
            min_len = c0;
        }
    }

    coder->book[bandcnt] = best_book;
    coder->blen[bandcnt] = (min_len < 0) ? 0 : min_len;

    return 0;
}

/* Sentinel cost for a book whose range doesn't cover the span. */
#define COST_INF (1 << 28)

/* Greedy merge ("codebook hysteresis"): per group, absorb the next
 * spectral band into the open section whenever recoding the span under a shared
 * covering book costs fewer extra bits than the section header it saves. Lossless.
 * Now O(N) overall as span recosting is done via O(1) table lookups. */
void section_optimize(CoderInfo *coder)
{
    int shortwin = (coder->block_type == ONLY_SHORT_WINDOW);
    int maxcnt = shortwin ? 7 : 31;
    int header = 4 + (shortwin ? 3 : 5);
    int g;

    for (g = 0; g < coder->groups.n; g++)
    {
        int band = g * coder->sfbn;
        int maxband = band + coder->sfbn;

        while (band < maxband)
        {
            int start_band = band;
            int current_book = coder->book[band];
            int current_spec_sum = coder->blen[band];
            int maxq = coder->maxq[band];
            int run_costs[12]; /* bit cost for the current run under books 1..11 */

            if (current_book == HCB_PNS || current_book == HCB_INTENSITY || current_book == HCB_INTENSITY2)
            {
                band++;
                continue;
            }

            for (int k = 1; k <= 11; k++) run_costs[k] = coder->band_costs[band][k];

            band++;
            while (band < maxband)
            {
                int next_book = coder->book[band];
                int next_maxq = coder->maxq[band];
                int merged_book, merged_cost;
                int delta, header_saved;

                if (next_book == HCB_PNS || next_book == HCB_INTENSITY || next_book == HCB_INTENSITY2)
                    break;

                if ((band - start_band) >= maxcnt)
                    break;

                int span_maxq = (maxq > next_maxq) ? maxq : next_maxq;
                int base = book_for_maxq(span_maxq);

                /* Calculate bits needed to code the WHOLE run (including 'band')
                 * under the cheapest legal book in the current span's tier. */
                int n0 = -1, n1 = -1;
                if (base == HCB_ESC)
                {
                    merged_cost = (run_costs[HCB_ESC] >= 0 && coder->band_costs[band][HCB_ESC] >= 0) ?
                                  run_costs[HCB_ESC] + coder->band_costs[band][HCB_ESC] : -1;
                    merged_book = HCB_ESC;
                }
                else
                {
                    n0 = (run_costs[base] >= 0 && coder->band_costs[band][base] >= 0) ?
                         run_costs[base] + coder->band_costs[band][base] : -1;
                    n1 = (run_costs[base+1] >= 0 && coder->band_costs[band][base+1] >= 0) ?
                         run_costs[base+1] + coder->band_costs[band][base+1] : -1;

                    if (n1 >= 0 && (n0 < 0 || n1 < n0))
                    {
                        merged_cost = n1;
                        merged_book = base + 1;
                    }
                    else
                    {
                        merged_cost = n0;
                        merged_book = base;
                    }
                }

                delta = (merged_cost >= 0) ? merged_cost - (current_spec_sum + coder->blen[band]) : COST_INF;
                header_saved = header;

                if (merged_cost >= 0 && delta < header_saved)
                {
                    for (int k = start_band; k <= band; k++) coder->book[k] = merged_book;
                    for (int k = 1; k <= 11; k++)
                    {
                        if (run_costs[k] >= 0 && coder->band_costs[band][k] >= 0)
                            run_costs[k] += coder->band_costs[band][k];
                        else
                            run_costs[k] = -1;
                    }
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
        if (book >= 1 && book <= HCB_ESC)
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

/* Write (or size) the section layout: 4-bit book + run length per maximal run of
 * equal adjacent books. The count field is cntbits wide (5 long / 3 short), so a
 * run past maxcnt splits into repeated full sections. greedy merge grows the
 * runs; this just RLE-encodes the final books[]. */
int writebooks(CoderInfo *coder, BitStream *stream, int write)
{
    int bits = 0;
    int maxcnt, cntbits;
    int group;
    int bookbits = 4;

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
            int book = coder->book[band++];
            int bookcnt = 1;
            if (write)
            {
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
                {
                    PutBit(stream, maxcnt, cntbits);
                }
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

        if ((book == HCB_INTENSITY) || (book == HCB_INTENSITY2))
        {
            diff = coder->sf[cnt] - lastis;
            diff = clamp_sf_diff(diff);
            length = huff_len_delta[SF_DELTA + diff];

            bits += length;

            lastis += diff;

            if (write)
                PutBit(stream, huff_data_delta[SF_DELTA + diff], length);
        }
        else if (book == HCB_PNS)
        {
            diff = coder->sf[cnt] - lastpns;

            if (initpns)
            {
                /* First PNS band has no prior energy to delta against, so the
                 * spec sends a raw 9-bit value (diff + 256) instead of an HCB_DELTA
                 * code; later PNS bands delta off this one. */
                initpns = 0;

                length = 9;
                bits += length;
                lastpns += diff;

                if (write)
                    PutBit(stream, diff + 256, length);
                continue;
            }

            diff = clamp_sf_diff(diff);

            length = huff_len_delta[SF_DELTA + diff];
            bits += length;
            lastpns += diff;

            if (write)
                PutBit(stream, huff_data_delta[SF_DELTA + diff], length);
        }
        else if ((book != HCB_ZERO) && (book != HCB_NONE))
        {
            diff = coder->sf[cnt] - lastsf;
            diff = clamp_sf_diff(diff);
            length = huff_len_delta[SF_DELTA + diff];

            bits += length;
            lastsf += diff;

            if (write)
                PutBit(stream, huff_data_delta[SF_DELTA + diff], length);
        }

    }
    return bits;
}
