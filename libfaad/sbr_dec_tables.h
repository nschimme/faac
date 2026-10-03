/*
 * FAAD - Freeware Advanced Audio Decoder
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

#ifndef SBR_DEC_TABLES_H
#define SBR_DEC_TABLES_H

#include "sbr_tables.h"

extern const float sbr_noise_table[512][2];

/* Parametric stereo codebooks (ISO/IEC 14496-3 §8.6). */
#define PS_HUFF_IID_DF_FINE_OFFSET 30
#define PS_HUFF_IID_DF_FINE_NSYMS  61
extern const SBRHuffEntry ps_huff_iid_df_fine[PS_HUFF_IID_DF_FINE_NSYMS];
#define PS_HUFF_IID_DT_FINE_OFFSET 30
#define PS_HUFF_IID_DT_FINE_NSYMS  61
extern const SBRHuffEntry ps_huff_iid_dt_fine[PS_HUFF_IID_DT_FINE_NSYMS];
#define PS_HUFF_IID_DF_OFFSET 14
#define PS_HUFF_IID_DF_NSYMS  29
extern const SBRHuffEntry ps_huff_iid_df[PS_HUFF_IID_DF_NSYMS];
#define PS_HUFF_IID_DT_OFFSET 14
#define PS_HUFF_IID_DT_NSYMS  29
extern const SBRHuffEntry ps_huff_iid_dt[PS_HUFF_IID_DT_NSYMS];
#define PS_HUFF_ICC_DF_OFFSET 7
#define PS_HUFF_ICC_DF_NSYMS  15
extern const SBRHuffEntry ps_huff_icc_df[PS_HUFF_ICC_DF_NSYMS];
#define PS_HUFF_ICC_DT_OFFSET 7
#define PS_HUFF_ICC_DT_NSYMS  15
extern const SBRHuffEntry ps_huff_icc_dt[PS_HUFF_ICC_DT_NSYMS];
#define PS_HUFF_IPD_DF_OFFSET 0
#define PS_HUFF_IPD_DF_NSYMS  8
extern const SBRHuffEntry ps_huff_ipd_df[PS_HUFF_IPD_DF_NSYMS];
#define PS_HUFF_IPD_DT_OFFSET 0
#define PS_HUFF_IPD_DT_NSYMS  8
extern const SBRHuffEntry ps_huff_ipd_dt[PS_HUFF_IPD_DT_NSYMS];
#define PS_HUFF_OPD_DF_OFFSET 0
#define PS_HUFF_OPD_DF_NSYMS  8
extern const SBRHuffEntry ps_huff_opd_df[PS_HUFF_OPD_DF_NSYMS];
#define PS_HUFF_OPD_DT_OFFSET 0
#define PS_HUFF_OPD_DT_NSYMS  8
extern const SBRHuffEntry ps_huff_opd_dt[PS_HUFF_OPD_DT_NSYMS];

#endif
