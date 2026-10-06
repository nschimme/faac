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

#ifndef MP4READ_H
#define MP4READ_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint64_t offset;
    uint32_t size;
} MP4Sample;

typedef struct {
    char *name;
    char *value;
} MP4Tag;

typedef struct {
    uint8_t *asc_buf;
    uint32_t asc_len;
    uint32_t delay;
    uint32_t padding;
    uint32_t timescale;
    MP4Sample *samples;
    uint32_t num_samples;
    char major_brand[16];
    MP4Tag *tags;
    uint32_t num_tags;
    uint32_t cover_bytes;
} MP4Track;

/* False when buf is not an MP4 at all (e.g. ADTS); true with no asc_buf when
 * it is an MP4 without an AAC track. */
bool mp4_read_track_buf(const uint8_t *buf, long file_size, MP4Track *track);
void mp4_free_track(MP4Track *track);

#endif
