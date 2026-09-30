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

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef float sbrfloat;

/* Time-delta envelope codes, central entries only: a wider jump always costs
 * less as frequency deltas, so the encoder never needs the rest. */
#define T_HUFF_ENV_LAV    6

/* Envelope and balance delta codes at both resolutions: 121 + 63 + 49 + 25
 * frequency-delta entries, each book followed by its 13 time-delta ones. */
#define SBR_ENV_CODES_LEN (121 + 63 + 49 + 25 + 4 * (2 * T_HUFF_ENV_LAV + 1))

extern const sbrfloat qmf_c[640];
extern const int8_t sbr_offset[6][16];
extern const uint32_t sbr_env_codes[SBR_ENV_CODES_LEN];

#ifdef __cplusplus
}
#endif

#endif /* SBR_TABLES_H */
