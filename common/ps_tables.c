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

#include "ps_tables.h"

const SBRHuffEntry ps_huff_iid_df[PS_HUFF_IID_DF_NSYMS] = {
    /*  -14 */ { 0x0001fffbu, 17 },
    /*  -13 */ { 0x0001fffcu, 17 },
    /*  -12 */ { 0x0001fffdu, 17 },
    /*  -11 */ { 0x0001fffau, 17 },
    /*  -10 */ { 0x0000fffcu, 16 },
    /*   -9 */ { 0x00007ffcu, 15 },
    /*   -8 */ { 0x00001ffdu, 13 },
    /*   -7 */ { 0x000003feu, 10 },
    /*   -6 */ { 0x000001feu,  9 },
    /*   -5 */ { 0x0000007eu,  7 },
    /*   -4 */ { 0x0000003cu,  6 },
    /*   -3 */ { 0x0000001du,  5 },
    /*   -2 */ { 0x0000000du,  4 },
    /*   -1 */ { 0x00000005u,  3 },
    /*    0 */ { 0x00000000u,  1 },
    /*    1 */ { 0x00000004u,  3 },
    /*    2 */ { 0x0000000cu,  4 },
    /*    3 */ { 0x0000001cu,  5 },
    /*    4 */ { 0x0000003du,  6 },
    /*    5 */ { 0x0000003eu,  6 },
    /*    6 */ { 0x000000feu,  8 },
    /*    7 */ { 0x000007feu, 11 },
    /*    8 */ { 0x00001ffcu, 13 },
    /*    9 */ { 0x00003ffcu, 14 },
    /*   10 */ { 0x00003ffdu, 14 },
    /*   11 */ { 0x00007ffdu, 15 },
    /*   12 */ { 0x0001fffeu, 17 },
    /*   13 */ { 0x0003fffeu, 18 },
    /*   14 */ { 0x0003ffffu, 18 },
};

const SBRHuffEntry ps_huff_icc_df[PS_HUFF_ICC_DF_NSYMS] = {
    /*   -7 */ { 0x00003fffu, 14 },
    /*   -6 */ { 0x00003ffeu, 14 },
    /*   -5 */ { 0x00000ffeu, 12 },
    /*   -4 */ { 0x000003feu, 10 },
    /*   -3 */ { 0x0000007eu,  7 },
    /*   -2 */ { 0x0000001eu,  5 },
    /*   -1 */ { 0x00000006u,  3 },
    /*    0 */ { 0x00000000u,  1 },
    /*    1 */ { 0x00000002u,  2 },
    /*    2 */ { 0x0000000eu,  4 },
    /*    3 */ { 0x0000003eu,  6 },
    /*    4 */ { 0x000000feu,  8 },
    /*    5 */ { 0x000001feu,  9 },
    /*    6 */ { 0x000007feu, 11 },
    /*    7 */ { 0x00001ffeu, 13 },
};

const int8_t ps_iid_db_default[15] = { -25, -18, -14, -10, -7, -4, -2, 0, 2, 4, 7, 10, 14, 18, 25 };

const float ps_icc_invq[8] = { 1.0f, 0.937f, 0.84118f, 0.60092f, 0.36764f, 0.0f, -0.589f, -1.0f };

const SBRHuffEntry ps_huff_ipd_df[PS_HUFF_IPD_DF_NSYMS] = {
    /*    0 */ { 0x00000001u,  1 },
    /*    1 */ { 0x00000000u,  3 },
    /*    2 */ { 0x00000006u,  4 },
    /*    3 */ { 0x00000004u,  4 },
    /*    4 */ { 0x00000002u,  4 },
    /*    5 */ { 0x00000003u,  4 },
    /*    6 */ { 0x00000005u,  4 },
    /*    7 */ { 0x00000007u,  4 },
};
const SBRHuffEntry ps_huff_opd_df[PS_HUFF_OPD_DF_NSYMS] = {
    /*    0 */ { 0x00000001u,  1 },
    /*    1 */ { 0x00000001u,  3 },
    /*    2 */ { 0x00000006u,  4 },
    /*    3 */ { 0x00000004u,  4 },
    /*    4 */ { 0x0000000fu,  5 },
    /*    5 */ { 0x0000000eu,  5 },
    /*    6 */ { 0x00000005u,  4 },
    /*    7 */ { 0x00000000u,  3 },
};
