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
    uint32_t id;
    uint32_t fourcc;     /* sample entry type, e.g. 'mp4a' */
    char kind;           /* 'a' audio, 'v' video, 'o' other */
    bool decodable;      /* AAC audio with a config: faad can decode it */
} MP4TrackSummary;

enum { MP4_MAX_TRACK_SUMMARIES = 16 };

typedef struct {
    uint32_t track_id;   /* the track decoded */
    uint32_t num_tracks; /* tracks in the file */
    uint32_t num_listed; /* entries in summaries: the tracks the demuxer holds, up to the array size */
    MP4TrackSummary summaries[MP4_MAX_TRACK_SUMMARIES]; /* the first of them */
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
 * it is an MP4 without an AAC track, or without the AAC track want_track_id (0: the
 * first one). */
bool mp4_read_track_buf(const uint8_t *buf, uint64_t file_size, uint32_t want_track_id, MP4Track *track);
void mp4_free_track(MP4Track *track);

#endif
