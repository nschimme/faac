/*
 * FAAC - Freeware Advanced Audio Coder
 * SBR tables reproduced from ISO/IEC 14496-3:2005 (non-copyrightable facts)
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

#include "faad_internal.h"
#include "sbr_dec_tables.h"

#ifndef FAAD_DISABLE_SBR
static const uint8_t order_t_huff_env_1_5dB[] = {
    60, 59, 61, 58, 62, 57, 63, 56, 64, 55, 65, 54, 66, 53, 67, 52,
    51, 68, 50, 69, 49, 70, 48, 47, 71, 46, 72, 45, 44, 73, 41, 42,
    43, 74, 36, 40, 76, 34, 39, 75, 37, 35, 38, 0, 1, 2, 3, 4,
    5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
    21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 77, 78, 79,
    80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95,
    96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111,
    112, 113, 114, 115, 116, 117, 118, 119, 120
};
static const uint8_t order_f_huff_env_1_5dB[] = {
    60, 59, 61, 58, 57, 62, 56, 63, 55, 64, 54, 65, 53, 66, 52, 67,
    51, 68, 50, 69, 49, 70, 71, 48, 72, 47, 73, 74, 46, 45, 75, 76,
    77, 44, 43, 42, 41, 78, 79, 40, 39, 80, 81, 36, 37, 38, 34, 32,
    82, 83, 85, 19, 35, 86, 87, 30, 33, 84, 88, 104, 9, 14, 16, 17,
    23, 27, 29, 31, 90, 97, 102, 107, 108, 0, 1, 2, 3, 4, 5, 6,
    7, 8, 10, 11, 12, 13, 15, 18, 20, 21, 22, 24, 25, 26, 28, 89,
    91, 92, 93, 94, 95, 96, 98, 99, 100, 101, 103, 105, 106, 109, 110, 111,
    112, 113, 114, 115, 116, 117, 118, 119, 120
};
static const uint8_t order_t_huff_env_bal_1_5dB[] = {
    24, 25, 23, 26, 22, 27, 21, 28, 20, 19, 29, 18, 30, 31, 17, 32,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
    48
};
static const uint8_t order_f_huff_env_bal_1_5dB[] = {
    24, 23, 25, 22, 26, 27, 21, 20, 28, 19, 29, 18, 30, 17, 31, 32,
    15, 16, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
    14, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
    48
};
static const uint8_t order_t_huff_env_3_0dB[] = {
    31, 30, 32, 29, 33, 28, 34, 27, 35, 26, 36, 25, 24, 37, 23, 38,
    22, 21, 39, 40, 41, 18, 20, 19, 17, 42, 43, 0, 1, 2, 3, 4,
    5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62
};
static const uint8_t order_f_huff_env_3_0dB[] = {
    31, 30, 32, 29, 33, 28, 34, 27, 35, 26, 36, 25, 37, 24, 38, 23,
    39, 40, 22, 21, 41, 42, 20, 19, 43, 44, 18, 16, 45, 46, 17, 49,
    13, 7, 12, 47, 48, 9, 10, 15, 51, 52, 53, 56, 8, 11, 55, 0,
    1, 2, 3, 4, 5, 6, 14, 50, 54, 57, 58, 59, 60, 61, 62
};
static const uint8_t order_t_huff_env_bal_3_0dB[] = {
    12, 13, 11, 10, 14, 15, 9, 8, 16, 7, 0, 1, 2, 3, 4, 5,
    6, 17, 18, 19, 20, 21, 22, 23, 24
};
static const uint8_t order_f_huff_env_bal_3_0dB[] = {
    12, 11, 13, 10, 14, 15, 9, 8, 16, 7, 17, 18, 0, 1, 2, 3,
    4, 5, 6, 19, 20, 21, 22, 23, 24
};
static const uint8_t order_t_huff_noise_3_0dB[] = {
    31, 32, 30, 29, 33, 28, 34, 27, 35, 26, 36, 42, 0, 1, 2, 3,
    4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
    20, 21, 22, 23, 24, 25, 37, 38, 39, 40, 41, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62
};
static const uint8_t order_t_huff_noise_bal_3_0dB[] = {
    12, 11, 13, 10, 14, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24
};
#ifndef FAAD_DISABLE_PS
static const uint8_t order_ps_huff_iid_df_fine[] = {
    30, 29, 31, 28, 32, 27, 33, 26, 34, 25, 35, 24, 36, 37, 22, 38,
    23, 40, 21, 39, 19, 41, 20, 18, 42, 16, 44, 17, 43, 15, 45, 13,
    47, 14, 46, 9, 51, 11, 49, 12, 48, 4, 5, 2, 3, 8, 52, 6,
    7, 55, 56, 53, 54, 59, 60, 57, 58, 0, 1, 10, 50
};
static const uint8_t order_ps_huff_iid_dt_fine[] = {
    30, 31, 29, 28, 32, 27, 33, 26, 34, 35, 24, 36, 25, 23, 37, 39,
    22, 38, 41, 20, 40, 21, 17, 43, 18, 42, 19, 15, 45, 16, 44, 9,
    10, 48, 49, 11, 12, 13, 47, 14, 46, 4, 56, 2, 3, 59, 60, 57,
    58, 0, 1, 5, 55, 6, 54, 7, 53, 8, 52, 50, 51
};
static const uint8_t order_ps_huff_iid_df[] = {
    14, 15, 13, 16, 12, 17, 11, 10, 18, 19, 9, 20, 8, 7, 21, 22,
    6, 23, 24, 5, 25, 4, 3, 0, 1, 2, 26, 27, 28
};
static const uint8_t order_ps_huff_iid_dt[] = {
    14, 13, 15, 12, 16, 11, 17, 10, 18, 9, 19, 8, 20, 21, 7, 22,
    6, 23, 0, 1, 2, 3, 4, 5, 24, 25, 26, 27, 28
};
static const uint8_t order_ps_huff_icc_df[] = {
    7, 8, 6, 9, 5, 10, 4, 11, 12, 3, 13, 2, 14, 1, 0
};
static const uint8_t order_ps_huff_icc_dt[] = {
    7, 8, 6, 9, 5, 10, 4, 11, 3, 12, 2, 13, 1, 0, 14
};
static const uint8_t order_ps_huff_ipd_df[] = {
    0, 1, 4, 5, 3, 6, 2, 7
};
static const uint8_t order_ps_huff_ipd_dt[] = {
    0, 1, 7, 5, 2, 6, 4, 3
};
static const uint8_t order_ps_huff_opd_df[] = {
    0, 7, 1, 3, 6, 2, 5, 4
};
static const uint8_t order_ps_huff_opd_dt[] = {
    0, 1, 7, 5, 2, 6, 4, 3
};
#endif

const SBRHuffBook sbr_books[HB_COUNT] = {
    { NULL, order_t_huff_env_1_5dB, 60, { 0, 0, 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 19, 21, 24, 28, 30, 37, 41, 49, 121, 121 } },
    { NULL, order_f_huff_env_1_5dB, 60, { 0, 0, 0, 2, 4, 6, 8, 10, 11, 14, 17, 19, 23, 27, 31, 34, 36, 41, 47, 60, 75, 121 } },
    { NULL, order_t_huff_env_bal_1_5dB, 24, { 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 9, 11, 13, 13, 13, 14, 39, 49, 49, 49, 49 } },
    { NULL, order_f_huff_env_bal_1_5dB, 24, { 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 9, 12, 13, 13, 14, 15, 17, 18, 47, 49, 49 } },
    { NULL, order_t_huff_env_3_0dB, 31, { 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 9, 11, 12, 14, 19, 20, 24, 26, 29, 63, 63 } },
    { NULL, order_f_huff_env_3_0dB, 31, { 0, 0, 1, 2, 3, 4, 5, 6, 6, 8, 10, 12, 14, 16, 17, 19, 22, 26, 30, 37, 47, 63 } },
    { NULL, order_t_huff_env_bal_3_0dB, 12, { 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 9, 9, 10, 23, 25, 25, 25, 25, 25, 25, 25 } },
    { NULL, order_f_huff_env_bal_3_0dB, 12, { 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 9, 10, 11, 17, 25, 25, 25, 25, 25, 25, 25 } },
    { NULL, order_t_huff_noise_3_0dB, 31, { 0, 0, 1, 2, 3, 4, 5, 6, 6, 8, 8, 9, 10, 10, 61, 63, 63, 63, 63, 63, 63, 63 } },
    { NULL, order_t_huff_noise_bal_3_0dB, 12, { 0, 0, 1, 2, 3, 3, 4, 5, 5, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25 } },
#ifndef FAAD_DISABLE_PS
    { ps_huff_iid_df_fine, order_ps_huff_iid_df_fine, 30, { 0, 0, 1, 1, 3, 5, 7, 9, 11, 13, 14, 17, 20, 23, 25, 29, 31, 35, 41, 61, 61, 61 } },
    { ps_huff_iid_dt_fine, order_ps_huff_iid_dt_fine, 30, { 0, 0, 1, 2, 3, 3, 5, 7, 9, 10, 13, 15, 18, 22, 27, 31, 41, 61, 61, 61, 61, 61 } },
    { ps_huff_iid_df, order_ps_huff_iid_df, 14, { 0, 0, 1, 1, 3, 5, 7, 10, 11, 12, 13, 14, 15, 15, 17, 19, 21, 22, 27, 29, 29, 29 } },
    { ps_huff_iid_dt, order_ps_huff_iid_dt, 14, { 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 17, 17, 21, 29 } },
    { ps_huff_icc_df, order_ps_huff_icc_df, 7, { 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 15, 15, 15, 15, 15, 15 } },
    { ps_huff_icc_dt, order_ps_huff_icc_dt, 7, { 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 15, 15, 15, 15, 15, 15 } },
    { ps_huff_ipd_df, order_ps_huff_ipd_df, 0, { 0, 0, 1, 1, 2, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8 } },
    { ps_huff_ipd_dt, order_ps_huff_ipd_dt, 0, { 0, 0, 1, 1, 3, 6, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8 } },
    { ps_huff_opd_df, order_ps_huff_opd_df, 0, { 0, 0, 1, 1, 3, 6, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8 } },
    { ps_huff_opd_dt, order_ps_huff_opd_dt, 0, { 0, 0, 1, 1, 3, 6, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8 } },
#endif
};
#endif
