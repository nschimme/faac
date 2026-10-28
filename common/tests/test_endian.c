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

#include <stdint.h>
#include <string.h>

#include "endian.h"

int main(void)
{
    uint8_t bytes[10] = {0};
    const uint32_t probe = 0x01020304;
    uint8_t first;
    const uint8_t expected[] = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0};

    if (read_u16_be(expected + 1) != 0x3456 ||
        read_u32_be(expected + 1) != 0x3456789a ||
        read_u64_be(expected) != UINT64_C(0x123456789abcdef0))
        return 1;

    write_u16_be(bytes + 1, 0x3456);
    write_u32_be(bytes + 3, 0x789abcde);
    write_u64_be(bytes + 1, UINT64_C(0x123456789abcdef0));
    if (memcmp(bytes + 1, expected, sizeof(expected)) != 0)
        return 1;

    /* The compile-time answer must agree with the real byte order of memory. */
    memcpy(&first, &probe, 1);
    if (host_is_big_endian() != (first == 0x01) ||
        (bool)FAAC_BIG_ENDIAN != host_is_big_endian() ||
        htole32(probe) != (first == 0x01 ? UINT32_C(0x04030201) : probe))
        return 1;

    return 0;
}
