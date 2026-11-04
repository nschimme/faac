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

#ifndef ADTS_H
#define ADTS_H

#include <stdbool.h>
#include <stdint.h>

#include "asc_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ADTS_HEADER_SIZE 7 /* without CRC */
#define ADTS_MAX_FRAME 0x1FFF

/* What an ADTS header says about the stream it starts. */
typedef struct {
    uint8_t profile;     /* the AAC object type minus one: 1 is AAC-LC */
    uint8_t sr_idx;      /* Table 1.16 index of the core rate */
    uint8_t channel_cfg; /* channelConfiguration, 0 meaning "in the stream" */
    uint8_t raw_blocks;  /* number_of_raw_data_blocks_in_frame */
} adts_params;

/* The 12-bit sync with layer 0, and a sample-rate index that names a rate. */
bool adts_header_ok(const uint8_t *h);
uint32_t adts_frame_length(const uint8_t *h);
uint32_t adts_header_length(const uint8_t *h); /* 9 when a CRC follows */
void adts_parse_header(const uint8_t *h, adts_params *out);

/* How a track's AudioSpecificConfig reads as an ADTS header: the core object type (an
   SBR/PS-signalled stream is described by its underlying AAC-LC, SBR and PS ride inside
   the frames), the core sample rate and the channelConfiguration. Returns NULL, or why
   ADTS cannot say it.
   lenient is how faad has always extracted: an object type outside Main..LTP is written as
   LC and the channel field is the configuration masked to 3 bits, so only a rate with no
   index fails. Without it each of those is an error, which is what a muxer round trip needs
   rather than a header that names another stream. */
const char *adts_params_from_asc(const AscInfo *asc, bool lenient, adts_params *out);

/* A stream whose layout is a program config element repeats it so a decoder that joins late learns
   the layout; the encoder writes it in the first frame and every ADTS_PCE_REPEAT-th after it. */
#define ADTS_PCE_REPEAT 32

/* How many bytes of that element (pce_len) to write ahead of raw_data_block number `frame`: none
   off the repeat, or when the block already opens with one. */
uint32_t adts_pce_bytes(uint32_t pce_len, uint64_t frame, const uint8_t *block, uint32_t block_len);

/* The 7-byte header for a frame carrying payload_bytes of raw_data_block. */
void adts_write_header(const adts_params *p, uint32_t payload_bytes, uint8_t hdr[ADTS_HEADER_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* ADTS_H */
