/*
 * FAAC - Freeware Advanced Audio Coder
 * Huffman codebook tables reproduced from ISO/IEC 14496-3 (non-copyrightable facts)
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

#ifndef HUFFDATA_H
#define HUFFDATA_H

#include "huff2.h"

#include <stdint.h>

typedef struct {
    uint8_t len;
    uint8_t data_lo;
    uint8_t data_hi;
} hcode16_t;

#define H16(l, d) { (uint8_t)(l), (uint8_t)((d) & 0xff), (uint8_t)(((d) >> 8) & 0xff) }

typedef struct {
    uint8_t len;
    uint8_t data_lo;
    uint8_t data_mid;
    uint8_t data_hi;
} hcode32_t;

#define H32(l, d) { (uint8_t)(l), (uint8_t)((d) & 0xff), (uint8_t)(((d) >> 8) & 0xff), (uint8_t)(((d) >> 16) & 0xff) }

extern const hcode16_t book01[81];
extern const hcode16_t book02[81];
extern const hcode16_t book03[81];
extern const hcode16_t book04[81];
extern const hcode16_t book05[81];
extern const hcode16_t book06[81];
extern const hcode16_t book07[64];
extern const hcode16_t book08[64];
extern const hcode16_t book09[169];
extern const hcode16_t book10[169];
extern const hcode16_t book11[289];
extern const hcode32_t book12[2 * SF_DELTA + 1];

#endif /* HUFFDATA_H */
