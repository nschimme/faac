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

/* Standalone regression for BitStream count-only writes. */
#include "bitstream.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "count-writer test failed at line %d: %s\n", \
                __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static int check_counting_sequence(void)
{
    uint8_t bytes[256];
    BitStream real, dry;
    uint32_t expected_bits = 0;

    InitBitStream(&real, bytes, sizeof bytes);
    InitBitStream(&dry, NULL, sizeof bytes);

    /* Exercise every supported width, including zero and full-width fields. */
    for (int width = 0; width <= 32; width++) {
        uint32_t value = 0x9e3779b9U ^ ((uint32_t)width * 0x45d9f3bU);
        int real_result = PutBit(&real, value, width);
        int dry_result = PutBit(&dry, value, width);

        CHECK(real_result == 0);
        CHECK(dry_result == real_result);
        expected_bits += (uint32_t)width;
        CHECK(real.currentBit == expected_bits);
        CHECK(dry.currentBit == expected_bits);
    }

    /* Include spans that cross byte boundaries from unaligned positions. */
    {
        static const int widths[] = { 3, 13, 1, 31, 7, 32, 5, 17 };
        for (unsigned i = 0; i < sizeof widths / sizeof widths[0]; i++) {
            uint32_t value = 0xd3a5c79bU ^ (i * 0x10204081U);
            int real_result = PutBit(&real, value, widths[i]);
            int dry_result = PutBit(&dry, value, widths[i]);

            CHECK(real_result == 0);
            CHECK(dry_result == real_result);
            expected_bits += (uint32_t)widths[i];
            CHECK(real.currentBit == expected_bits);
            CHECK(dry.currentBit == expected_bits);
        }
    }

    return 0;
}

static int check_overrun(void)
{
    uint8_t bytes[2];
    BitStream real, dry;

    InitBitStream(&dry, NULL, 0);
    CHECK(PutBit(&dry, 0, 0) == 0);
    CHECK(PutBit(&dry, 1, 1) == -1);
    CHECK(dry.currentBit == 0);

    InitBitStream(&real, bytes, sizeof bytes);
    InitBitStream(&dry, NULL, sizeof bytes);

    CHECK(PutBit(&real, 0x1fffU, 13) == 0);
    CHECK(PutBit(&dry, 0x1fffU, 13) == 0);
    CHECK(real.currentBit == 13);
    CHECK(dry.currentBit == 13);

    /* A rejected write must return the same status and leave both cursors put. */
    CHECK(PutBit(&real, 0xfU, 4) == -1);
    CHECK(PutBit(&dry, 0xfU, 4) == -1);
    CHECK(real.currentBit == 13);
    CHECK(dry.currentBit == 13);

    CHECK(PutBit(&real, 1U, 3) == 0);
    CHECK(PutBit(&dry, 1U, 3) == 0);
    CHECK(real.currentBit == 16);
    CHECK(dry.currentBit == 16);
    return 0;
}

static int check_real_bytes(void)
{
    static const uint8_t expected[] = { 0xb3, 0xa6 };
    uint8_t bytes[sizeof expected];
    BitStream bs;

    InitBitStream(&bs, bytes, sizeof bytes);
    CHECK(PutBit(&bs, 0x5U, 3) == 0);
    CHECK(PutBit(&bs, 0x13U, 5) == 0);
    CHECK(PutBit(&bs, 0xa6U, 8) == 0);
    CHECK(bs.currentBit == 16);
    CHECK(memcmp(bytes, expected, sizeof expected) == 0);
    return 0;
}

int main(void)
{
    if (check_counting_sequence() != 0) return 1;
    if (check_overrun() != 0) return 1;
    if (check_real_bytes() != 0) return 1;
    return 0;
}
