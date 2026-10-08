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

#ifndef ANNEXB_H
#define ANNEXB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ANNEXB_H264,
    ANNEXB_H265
} annexb_codec;

/* Room for the avcC or hvcC an annexb_build_config() call may produce. */
#define ANNEXB_CONFIG_MAX 1024

/* Builds the avcC (H.264) or hvcC (H.265) record from the first SPS and PPS (and VPS) of an
   Annex-B stream. Returns NULL, or what is missing or wrong; *out_len is set on success. */
const char *annexb_build_config(annexb_codec codec, const uint8_t *data, size_t len,
                                uint8_t *out, uint32_t out_cap, uint32_t *out_len);

/* One access unit as the MP4 stores it: every NAL prefixed by its 4-byte big-endian length.
   sample is only valid during the call. Return false to stop. */
typedef bool (*annexb_sample_fn)(void *user, const uint8_t *sample, uint32_t len, bool key);

typedef enum {
    ANNEXB_OK = 0,
    ANNEXB_NO_MEMORY, /* the sample buffer could not grow, or an access unit passes 4 GiB */
    ANNEXB_STOPPED    /* the callback returned false */
} annexb_status;

/* Splits an Annex-B stream into access units, one callback each. A picture is one sample
   however many slices it has; the parameter sets, AUD and SEI that precede it belong to it.
   Parameter sets or SEI after the last slice, with no slice following, are dropped. */
annexb_status annexb_split(annexb_codec codec, const uint8_t *data, size_t len,
                           annexb_sample_fn fn, void *user);

#ifdef __cplusplus
}
#endif

#endif /* ANNEXB_H */
