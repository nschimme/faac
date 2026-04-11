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
#include "util.h"

/**
 * Encode an AAC escape sequence (Codebook 11) per ISO/IEC 14496-3.
 *
 * Architecture: Uses a high-performance bit-manipulation approach based on
 * Count Leading Zeros (CLZ) to determine the magnitude range N. This
 * avoids the cycle-heavy iterative shifts used in legacy implementations.
 *
 * Constraint: Magnitudes are clamped to 13 bits (8191) to maintain compliance
 * with the AAC-LC quantized spectral range.
 */
static int escape(int x, int *code)
{
    /* Validation: ensure spectral magnitude is within the AAC spec's escape coding limit.
     * Values >= 8192 cannot be represented and would result in bitstream corruption. */
    if (UNLIKELY(x > MAX_HUFF_ESC_VAL))
    {
        fprintf(stderr, "%s(%d): x_quant > %d\n", __FILE__, __LINE__, MAX_HUFF_ESC_VAL);
        return 0;
    }

    /* Determine the magnitude range (N).
     * The leading one is at position N+4 (0-indexed).
     * CLZ(x) gives the number of leading zeros in a 32-bit word.
     * Example: for x=16 (10000b), CLZ is 27, preflen becomes 0. */
    int preflen = 27 - CLZ((unsigned)x);
    int base = 1 << (preflen + 4);

    /* Construct the codeword: prefix ones, a zero separator, and the remaining bits. */
    *code = ((((1 << preflen) - 1) << 1) << (preflen + 4)) | (x - base);

    /* Total length is N (ones) + 1 (zero) + (N+4) (value) = 2N + 5 */
    return (preflen << 1) + 5;
}

#define arrlen(array) (sizeof(array) / sizeof(*array))



/**
 * Huffman Encoding Engine (Entropy Coding Stage).
 *
 * This module is architected for maximum throughput by structurally decoupling
 * the Rate Estimation path from the Bitstream Encoding path.
 *
 * Engineering Strategy:
 * 1. Branch Elimination: Inner loops are specialized to eliminate conditional
 *    checks on the 'coder' pointer, which avoids thousands of pipeline stalls
 *    per frame during the iterative Rate-Distortion search.
 * 2. Invariant Safety Hoisting: Critical safety bounds (DATASIZE) and codebook
 *    range validations are performed once per scale-factor band, rather than
 *    per tuple, reducing hot-path overhead to near-zero.
 * 3. Loop Vectorization: Specialized tuple loops facilitate compiler
 *    auto-vectorization and optimal register allocation.
 */
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
    int ofs, data = 0;
    int idx;

    /* Codebook range check hoisted */
    if (bnum > 0 && bnum <= 10)
    {
        static const int book_maxq[] = {0, 1, 1, 2, 2, 4, 4, 7, 7, 12, 12};
        if (UNLIKELY(maxq > book_maxq[bnum]))
            return -1;
    }

    book = hmap[bnum];

    if (coder)
    {
        /* --- ENCODING PATH --- */
        int datacnt = coder->datacnt;

        /* Buffer Safety: Pre-calculate worst-case codeword count (3 codewords per 2 lines).
         * Hoisting this check avoids per-tuple branching and ensures atomicity of the band write. */
        if (UNLIKELY(datacnt + (len / 2) * 3 > DATASIZE))
        {
            fprintf(stderr, "DATASIZE exceeded: truncation occurred!\n");
            return -1;
        }

        switch (bnum)
        {
        case 1:
        case 2:
            for(ofs = 0; ofs < len; ofs += 4)
            {
                idx = 27 * qs[ofs] + 9 * qs[ofs+1] + 3 * qs[ofs+2] + qs[ofs+3] + 40;
                blen = book[idx].len;
                bits += blen;
                coder->s[datacnt].data = book[idx].data;
                coder->s[datacnt++].len = blen;
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

                blen += (q0 != 0) + (q1 != 0) + (q2 != 0) + (q3 != 0);
                data = (data << (q0 != 0)) | (q0 < 0);
                data = (data << (q1 != 0)) | (q1 < 0);
                data = (data << (q2 != 0)) | (q2 < 0);
                data = (data << (q3 != 0)) | (q3 < 0);

                bits += blen;
                coder->s[datacnt].data = data;
                coder->s[datacnt++].len = blen;
            }
            break;
        case 5:
        case 6:
            for(ofs = 0; ofs < len; ofs += 2)
            {
                idx = 9 * qs[ofs] + qs[ofs+1] + 40;
                blen = book[idx].len;
                bits += blen;
                coder->s[datacnt].data = book[idx].data;
                coder->s[datacnt++].len = blen;
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

                blen += (q0 != 0) + (q1 != 0);
                data = (data << (q0 != 0)) | (q0 < 0);
                data = (data << (q1 != 0)) | (q1 < 0);

                bits += blen;
                coder->s[datacnt].data = data;
                coder->s[datacnt++].len = blen;
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

                blen += (q0 != 0) + (q1 != 0);
                data = (data << (q0 != 0)) | (q0 < 0);
                data = (data << (q1 != 0)) | (q1 < 0);

                bits += blen;
                coder->s[datacnt].data = data;
                coder->s[datacnt++].len = blen;
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

                blen += (q0 != 0) + (q1 != 0);
                data = (data << (q0 != 0)) | (q0 < 0);
                data = (data << (q1 != 0)) | (q1 < 0);

                bits += blen;
                coder->s[datacnt].data = data;
                coder->s[datacnt++].len = blen;

                if (a0 >= 16)
                {
                    blen = escape(a0, &data);
                    bits += blen;
                    coder->s[datacnt].data = data;
                    coder->s[datacnt++].len = blen;
                }
                if (a1 >= 16)
                {
                    blen = escape(a1, &data);
                    bits += blen;
                    coder->s[datacnt].data = data;
                    coder->s[datacnt++].len = blen;
                }
            }
            break;
        default:
            return -1;
        }
        coder->datacnt = datacnt;
    }
    else
    {
        /* --- ESTIMATION PATH --- */
        /* Used during rate-distortion optimization loops. Optimized for pure bit-counting throughput. */
        switch (bnum)
        {
        case 1:
        case 2:
            for(ofs = 0; ofs < len; ofs += 4)
                bits += book[27 * qs[ofs] + 9 * qs[ofs+1] + 3 * qs[ofs+2] + qs[ofs+3] + 40].len;
            break;
        case 3:
        case 4:
            for(ofs = 0; ofs < len; ofs += 4)
            {
                int q0 = qs[ofs], q1 = qs[ofs+1], q2 = qs[ofs+2], q3 = qs[ofs+3];
                bits += book[27 * abs(q0) + 9 * abs(q1) + 3 * abs(q2) + abs(q3)].len;
                bits += (q0 != 0) + (q1 != 0) + (q2 != 0) + (q3 != 0);
            }
            break;
        case 5:
        case 6:
            for(ofs = 0; ofs < len; ofs += 2)
                bits += book[9 * qs[ofs] + qs[ofs+1] + 40].len;
            break;
        case 7:
        case 8:
            for(ofs = 0; ofs < len; ofs += 2)
            {
                int q0 = qs[ofs], q1 = qs[ofs+1];
                bits += book[8 * abs(q0) + abs(q1)].len;
                bits += (q0 != 0) + (q1 != 0);
            }
            break;
        case 9:
        case 10:
            for(ofs = 0; ofs < len; ofs += 2)
            {
                int q0 = qs[ofs], q1 = qs[ofs+1];
                bits += book[13 * abs(q0) + abs(q1)].len;
                bits += (q0 != 0) + (q1 != 0);
            }
            break;
        case HCB_ESC:
            for(ofs = 0; ofs < len; ofs += 2)
            {
                int q0 = qs[ofs], q1 = qs[ofs+1];
                int a0 = abs(q0), a1 = abs(q1);
                bits += book[17 * min(a0, 16) + min(a1, 16)].len;
                bits += (q0 != 0) + (q1 != 0);
                if (a0 >= 16) bits += escape(a0, &data);
                if (a1 >= 16) bits += escape(a1, &data);
            }
            break;
        default:
            return -1;
        }
    }

    return bits;
}


/**
 * Select the optimal Huffman codebook for a spectral band.
 */
int huffbook(CoderInfo *coder,
             int *qs /* quantized spectrum */,
             int len)
{
    int cnt;
    /* Track max quantized value to hoist range safety checks out of huffcode loops */
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
