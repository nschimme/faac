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

#ifndef DRC_H
#define DRC_H

#include <stdbool.h>
#include <stdint.h>
#include "bitstream.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EXT_DYNAMIC_RANGE 0xB

typedef struct {
    bool enabled;
    float gain_db;              /* dynamic_range_ctl gain in dB */
    bool prog_ref_level_present;
    float prog_ref_level_db;    /* program reference level in dBFS */
} DRCInfo;

/*
 * Writes the DRC fill element (ID_FIL + count + EXT_DYNAMIC_RANGE payload)
 * to the bitstream if enabled in drc. Returns the number of bits written.
 */
int DrcGetBits(const DRCInfo *drc, BitStream *bs);

#ifdef __cplusplus
}
#endif

#endif /* DRC_H */
