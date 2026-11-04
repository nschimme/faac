/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

/* BitAccumulator is a faster way to write the same bits as PutBit, so for any field
 * sequence, any span boundaries and any starting bit offset the buffer and cursor must
 * match PutBit's; running past the buffer must be reported, never written. */
#include <stdio.h>
#include "bitstream.h"
#include "../../tests/ftest.h"

enum { BUF = 512 };

typedef struct { uint32_t value; int bits; } field;

static void test_matches_putbit(uint32_t seed)
{
    uint32_t rng = seed;
    field fields[200];
    unsigned count = 0;
    uint32_t total = 0;
    while (count < 200) {
        int bits = (int)(ft_rand(&rng) % 33); /* 0..32 inclusive */
        if (total + (uint32_t)bits > BUF * 8 - 64) break;
        fields[count].bits = bits;
        fields[count].value = ft_rand(&rng);
        total += (uint32_t)bits;
        count++;
    }

    uint8_t want_buf[BUF], got_buf[BUF];
    BitStream want, got;
    InitBitStream(&want, want_buf, BUF);
    InitBitStream(&got, got_buf, BUF);
    /* Unaligned start so AccumBegin has to keep an in-progress byte. */
    uint32_t lead = ft_rand(&rng) % 8;
    FT_CHECK(PutBit(&want, 0x55, (int)lead) == 0);
    FT_CHECK(PutBit(&got, 0x55, (int)lead) == 0);

    for (unsigned i = 0; i < count;) {
        unsigned span = 1 + ft_rand(&rng) % 9;
        if (span > count - i) span = count - i;
        int use_accum = (int)(ft_rand(&rng) & 1);
        BitAccumulator a;
        if (use_accum) AccumBegin(&a, &got);
        for (unsigned k = 0; k < span; k++, i++) {
            FT_CHECK(PutBit(&want, fields[i].value, fields[i].bits) == 0);
            if (use_accum) AccumPutBits(&a, fields[i].value, fields[i].bits);
            else FT_CHECK(PutBit(&got, fields[i].value, fields[i].bits) == 0);
        }
        if (use_accum) FT_CHECK(AccumEnd(&a) == 0);
    }
    FT_CHECK_INT(got.currentBit, want.currentBit);
    FT_CHECK_MEM(got_buf, want_buf, BUF);
}

/* A write past the end is reported and must not touch the bytes beyond it. */
static void test_overflow_reported(void)
{
    uint8_t buf[8], untouched[4];
    BitStream bs;
    BitAccumulator a;
    memset(buf, 0xA5, sizeof(buf));
    memcpy(untouched, buf + 4, sizeof(untouched));
    InitBitStream(&bs, buf, 4);
    memset(buf + 4, 0xA5, sizeof(untouched));
    AccumBegin(&a, &bs);
    AccumPutBits(&a, 0xFFFFFFFFu, 32);
    AccumPutBits(&a, 1, 1);
    FT_CHECK(AccumEnd(&a) == -1);
    FT_CHECK_MEM(buf + 4, untouched, sizeof(untouched));

    InitBitStream(&bs, buf, 4);
    FT_CHECK(PutBit(&bs, 0xFFFFFFFFu, 32) == 0);
    FT_CHECK(PutBit(&bs, 1, 1) == -1);
    FT_CHECK_MEM(buf + 4, untouched, sizeof(untouched));
}

int main(void)
{
    for (uint32_t seed = 1; seed <= 200; seed++)
        test_matches_putbit(seed * 2654435761u | 1);
    test_overflow_reported();
    return ft_done();
}
