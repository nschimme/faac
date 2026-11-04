/*
 * FAAM - Freeware Advanced Audio Muxer
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

/*
 * libFuzzer harness for faam_update_metadata_stream and faam_update_chapters_stream.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "faam.h"

typedef struct {
    uint8_t *buf;
    size_t size;
    size_t cap;
    size_t pos;
} rw_mem_stream;

static int32_t rw_read(void *user_data, void *buf, uint32_t bytes_to_read)
{
    rw_mem_stream *s = (rw_mem_stream *)user_data;
    if (s->pos >= s->size) return 0;
    size_t rem = s->size - s->pos;
    uint32_t to_copy = (uint32_t)(rem < bytes_to_read ? rem : bytes_to_read);
    memcpy(buf, s->buf + s->pos, to_copy);
    s->pos += to_copy;
    return (int32_t)to_copy;
}

static int32_t rw_write(void *user_data, const void *buf, uint32_t bytes_to_write)
{
    rw_mem_stream *s = (rw_mem_stream *)user_data;
    if (s->pos + bytes_to_write > s->cap) {
        size_t new_cap = (s->pos + bytes_to_write) * 2 + 1024;
        if (new_cap > 10 * 1024 * 1024) return -1; /* Limit in-memory allocation */
        uint8_t *p = realloc(s->buf, new_cap);
        if (!p) return -1;
        s->buf = p;
        s->cap = new_cap;
    }
    if (s->pos > s->size) memset(s->buf + s->size, 0, s->pos - s->size);
    memcpy(s->buf + s->pos, buf, bytes_to_write);
    s->pos += bytes_to_write;
    if (s->pos > s->size) s->size = s->pos;
    return (int32_t)bytes_to_write;
}

static bool rw_seek(void *user_data, uint64_t offset)
{
    rw_mem_stream *s = (rw_mem_stream *)user_data;
    if (offset > 10 * 1024 * 1024) return false;
    s->pos = (size_t)offset;
    return true;
}

static uint64_t rw_tell(void *user_data)
{
    rw_mem_stream *s = (rw_mem_stream *)user_data;
    return (uint64_t)s->pos;
}

/* Track count of a file the demuxer accepts, or -1. Only files that carry a track are held to
 * the "still readable after an edit" rule: damaged input that happens to open with no tracks
 * can legitimately become unreadable once an edit makes its structure consistent. */
static int tracks_of(const faam_io *io, rw_mem_stream *s)
{
    s->pos = 0;
    faam_demuxer *d = NULL;
    if (faam_demuxer_open(NULL, io, &d) != FAAM_OK || !d) return -1;
    uint32_t n = 0;
    faam_status st = faam_demuxer_get_num_tracks(d, &n, NULL);
    faam_demuxer_close(&d);
    return st == FAAM_OK ? (int)n : -1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 24) return 0;

    faam_library_info lib;
    memset(&lib, 0, sizeof(lib));
    lib.struct_size = sizeof(lib);
    if (faam_get_library_info(&lib) != FAAM_OK || lib.struct_size < sizeof(lib))
        __builtin_trap();
    if (lib.flags & ~(FAAM_LIB_VIDEO | FAAM_LIB_FRAGMENTED))
        __builtin_trap();

    /* data[0] is configuration; data[1..5] seed one extra metadata entry. */
    uint8_t cfg_byte = data[0];
    uint32_t fuzz_key = ((uint32_t)data[1] << 24) | ((uint32_t)data[2] << 16) |
                        ((uint32_t)data[3] << 8) | data[4];
    uint8_t fuzz_type = data[5];
    const uint8_t *file_data = data + 6;
    size_t file_len = size - 6;

    rw_mem_stream ms;
    ms.cap = file_len + 4096;
    ms.buf = malloc(ms.cap);
    if (!ms.buf) return 0;
    memcpy(ms.buf, file_data, file_len);
    ms.size = file_len;
    ms.pos = 0;

    faam_io io = { .struct_size = sizeof(faam_io), .user_data = &ms, .read = rw_read, .write = rw_write, .seek = rw_seek, .tell = rw_tell, .flush = NULL };

    /* Construct fuzz-derived metadata */
    char title_buf[32];
    snprintf(title_buf, sizeof(title_buf), "FuzzTitle_%u", cfg_byte);

    faam_metadata_entry entries[8];
    uint32_t num_entries = 0;
    entries[num_entries++] = (faam_metadata_entry){
        .struct_size = sizeof(faam_metadata_entry),
        .key = FAAM_METADATA_KEY_TITLE,
        .data_type = FAAM_METADATA_UTF8,
        .data = title_buf,
        .data_bytes = (uint32_t)strlen(title_buf),
    };
    if (cfg_byte & 1) {
        static const char artist[] = "FuzzArtist";
        entries[num_entries++] = (faam_metadata_entry){
            .struct_size = sizeof(faam_metadata_entry),
            .key = FAAM_METADATA_KEY_ARTIST,
            .data_type = FAAM_METADATA_UTF8,
            .data = artist,
            .data_bytes = sizeof(artist) - 1,
        };
    }
    if (cfg_byte & 2) {
        static const uint8_t genre_code[] = { 0, 17 };
        entries[num_entries++] = (faam_metadata_entry){
            .struct_size = sizeof(faam_metadata_entry),
            .key = FAAM_METADATA_KEY_GENRE_CODE,
            .data_type = FAAM_METADATA_INTEGER,
            .data = genre_code,
            .data_bytes = sizeof(genre_code),
        };
    }
    if (cfg_byte & 4) {
        static const uint8_t compilation = 1;
        entries[num_entries++] = (faam_metadata_entry){
            .struct_size = sizeof(faam_metadata_entry),
            .key = FAAM_METADATA_KEY_COMPILATION,
            .data_type = FAAM_METADATA_INTEGER,
            .data = &compilation,
            .data_bytes = sizeof(compilation),
        };
    }

    /* An entry whose key, type and payload come from the input. */
    static const uint8_t text_payload[] = "fuzz";
    static const uint8_t int_payload[] = { 0x00, 0x2a, 0x01 };
    const void *payload = fuzz_type & 1 ? text_payload : int_payload;
    uint32_t payload_len = fuzz_type & 1 ? sizeof(text_payload) - 1 : sizeof(int_payload);
    const char *mean = NULL, *name = NULL;
    if (fuzz_key == FAAM_METADATA_KEY_FREEFORM) {
        mean = "com.apple.iTunes";
        name = "fuzz";
    }
    entries[num_entries++] = (faam_metadata_entry){
        .struct_size = sizeof(faam_metadata_entry),
        .key = fuzz_key,
        .data_type = fuzz_type % 30,
        .locale = 0,
        .data = (fuzz_type & 0x80) ? NULL : payload,
        .data_bytes = (fuzz_type & 0x80) ? 0 : payload_len,
        .flags = (fuzz_type & 0x40) ? FAAM_METADATA_REMOVE : 0,
        .mean = mean,
        .name = name,
    };

    int tracks = tracks_of(&io, &ms);
    ms.pos = 0;
    uint32_t update_flags = (cfg_byte & 8) ? FAAM_METADATA_UPDATE_CLEAR : 0;
    faam_status st = faam_update_metadata_stream(&io, entries, num_entries, update_flags);
    if (st == FAAM_OK && tracks > 0 && tracks_of(&io, &ms) != tracks) abort();

    /* Construct fuzz-derived chapters */
    faam_chapter chaps[2];
    for (uint32_t i = 0; i < 2; i++) chaps[i].struct_size = sizeof(chaps[i]);
    chaps[0].start_ms = 0;
    chaps[0].title = "Intro";
    chaps[1].start_ms = (cfg_byte * 100);
    chaps[1].title = "Part 2";

    ms.pos = 0;
    tracks = tracks_of(&io, &ms);
    ms.pos = 0;
    st = faam_update_chapters_stream(&io, chaps, 2, 0);
    /* Chapters add a track of their own, so only losing one is wrong. */
    if (st == FAAM_OK && tracks > 0 && tracks_of(&io, &ms) < tracks) abort();

    /* Reserved flags are rejected and an empty list is a no-op. */
    ms.pos = 0;
    if (faam_update_chapters_stream(&io, NULL, 0, 1) == FAAM_OK) abort();
    ms.pos = 0;
    if (faam_update_metadata_stream(&io, NULL, 0, 0x80000000u) == FAAM_OK) abort();

    free(ms.buf);
    return 0;
}
