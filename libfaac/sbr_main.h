/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Jules
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
 *
 */

#ifndef SBR_MAIN_H
#define SBR_MAIN_H

#include "faac_real.h"
#include "sbr_qmf.h"

#define SBR_BPC 2000

typedef struct {
    int sbr_grid;
    int sbr_header_present;
    int env_data[2][4][16]; /* [ch][env][band] - quantized */
    int num_env;
    int num_bands;
} sbr_bitstream_data_t;

typedef struct {
    sbr_qmf_analysis_t qmf[2]; /* Mono or Stereo */
    int frame_count;
    int num_channels;
    unsigned long sample_rate;
    sbr_bitstream_data_t bs_data;
} sbr_info_t;

sbr_info_t *sbr_encode_init(int num_channels, unsigned long sample_rate);
void sbr_encode_close(sbr_info_t *sbr);
void sbr_encode_frame(sbr_info_t *sbr, faac_real *input[], int block_type[]);

#endif /* SBR_MAIN_H */
