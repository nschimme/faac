/*
 * FAAC - Freeware Advanced Audio Coder
 * SBR tables reproduced from ISO/IEC 14496-3 (non-copyrightable facts)
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

/* SBR tables: QMF prototype filter, frequency-band offsets, Huffman tables.
 * All values are normative data from ISO/IEC 14496-3:2005. */

#ifndef SBR_TABLES_H
#define SBR_TABLES_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef float sbrfloat;

typedef struct {
    uint32_t code : 24;
    uint32_t len  : 8;
} SBRHuffEntry;

#define F_HUFF_ENV_1_5DB_OFFSET  60
#define F_HUFF_ENV_1_5DB_NSYMS   121
#define F_HUFF_ENV_3_0DB_OFFSET  31
#define F_HUFF_ENV_3_0DB_NSYMS   63

extern const sbrfloat qmf_c[640];
extern const signed char sbr_offset[6][16];
extern const SBRHuffEntry f_huff_env_1_5dB[F_HUFF_ENV_1_5DB_NSYMS];
extern const SBRHuffEntry f_huff_env_3_0dB[F_HUFF_ENV_3_0DB_NSYMS];

/* Parametric stereo delta tables, indexed by delta + offset. Gated so a
 * `parametric-stereo=false` build does not carry 352 bytes of rodata for a
 * feature it cannot reach. */
#if !defined(FAAC_PARAMETRIC_STEREO) || FAAC_PARAMETRIC_STEREO
#define PS_HUFF_IID_OFFSET  14
#define PS_HUFF_IID_NSYMS   29
#define PS_HUFF_ICC_OFFSET  7
#define PS_HUFF_ICC_NSYMS   15

extern const SBRHuffEntry ps_huff_iid_df[PS_HUFF_IID_NSYMS];
extern const SBRHuffEntry ps_huff_iid_dt[PS_HUFF_IID_NSYMS];
extern const SBRHuffEntry ps_huff_icc_df[PS_HUFF_ICC_NSYMS];
extern const SBRHuffEntry ps_huff_icc_dt[PS_HUFF_ICC_NSYMS];
#endif

#ifdef __cplusplus
}
#endif

#endif /* SBR_TABLES_H */
