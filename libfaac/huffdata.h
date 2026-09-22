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
#include <stddef.h>

typedef struct {
    uint32_t len  : 8;    /* lengths <= 19        */
    uint32_t data : 24;   /* codes are <= 19 bits */
} hcode32_t;

extern const uint8_t book01_len[81];
extern const uint16_t book01_data[81];
extern const uint8_t book02_len[81];
extern const uint16_t book02_data[81];
extern const uint8_t book03_len[81];
extern const uint16_t book03_data[81];
extern const uint8_t book04_len[81];
extern const uint16_t book04_data[81];
extern const uint8_t book05_len[81];
extern const uint16_t book05_data[81];
extern const uint8_t book06_len[81];
extern const uint16_t book06_data[81];
extern const uint8_t book07_len[64];
extern const uint16_t book07_data[64];
extern const uint8_t book08_len[64];
extern const uint16_t book08_data[64];
extern const uint8_t book09_len[169];
extern const uint16_t book09_data[169];
extern const uint8_t book10_len[169];
extern const uint16_t book10_data[169];
extern const uint8_t book11_len[289];
extern const uint16_t book11_data[289];

extern const hcode32_t book12[2 * SF_DELTA + 1];

extern const uint8_t * const hmap_len[12];
extern const uint16_t * const hmap_data[12];

#endif /* HUFFDATA_H */
