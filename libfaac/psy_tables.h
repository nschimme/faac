/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2001 Menno Bakker
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

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef PSY_TABLES_H
#define PSY_TABLES_H

#include "faac_real.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct {
    float sfb_bark_long[64];
    float sfb_ath_long[64];
    float sfb_bark_short[64];
    float sfb_ath_short[64];
} FaacPsyContext;

float FaacBarkFromHz(float hz);

void FaacInitPsyContext(FaacPsyContext *psy, int sr_idx,
                       int num_cb_long, int *cb_width_long,
                       int num_cb_short, int *cb_width_short);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* PSY_TABLES_H */
