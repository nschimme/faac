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
 * libFuzzer harness for libfaam demuxer standalone over in-memory faam_io.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "faam.h"

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
} mem_stream;

static int32_t mem_read(void *user_data, void *buf, uint32_t bytes_to_read)
{
    mem_stream *s = (mem_stream *)user_data;
    if (s->pos >= s->size) return 0;
    size_t rem = s->size - s->pos;
    uint32_t to_copy = (uint32_t)(rem < bytes_to_read ? rem : bytes_to_read);
    memcpy(buf, s->data + s->pos, to_copy);
    s->pos += to_copy;
    return (int32_t)to_copy;
}

static bool mem_seek(void *user_data, uint64_t offset)
{
    mem_stream *s = (mem_stream *)user_data;
    if (offset > s->size) return false;
    s->pos = (size_t)offset;
    return true;
}

static uint64_t mem_tell(void *user_data)
{
    mem_stream *s = (mem_stream *)user_data;
    return (uint64_t)s->pos;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 2) return 0;

    faam_library_info lib;
    memset(&lib, 0, sizeof(lib));
    lib.struct_size = sizeof(lib);
    if (faam_get_library_info(&lib) != FAAM_OK || lib.struct_size < sizeof(lib))
        __builtin_trap();
    if (lib.flags & ~(FAAM_LIB_VIDEO | FAAM_LIB_FRAGMENTED))
        __builtin_trap();

    mem_stream ms = { data, size, 0 };
    faam_io io = { .struct_size = sizeof(faam_io), .user_data = &ms, .read = mem_read, .write = NULL, .seek = mem_seek, .tell = mem_tell, .flush = NULL };

    faam_demuxer_config cfg;
    if (faam_demuxer_config_init(&cfg, sizeof(cfg)) != FAAM_OK)
        __builtin_trap();

    /* The reserved flags must be refused before any I/O; every input still parses below. */
    faam_demuxer_config bad = cfg;
    bad.flags = ((uint32_t)data[1] << 8) | 1;
    faam_demuxer *refused = NULL;
    if (faam_demuxer_open(&bad, &io, &refused) != FAAM_ERR_INVALID_ARGUMENT || refused)
        __builtin_trap();

    faam_demuxer *d = NULL;
    faam_status st = faam_demuxer_open(&cfg, &io, &d);
    if (st != FAAM_OK || !d) {
        return 0;
    }

    uint32_t held = 0, total = 0;
    if (faam_demuxer_get_num_tracks(d, &held, &total) != FAAM_OK)
        __builtin_trap();
    if (total < held)
        __builtin_trap();

    char brand[5] = { 0 };
    (void)faam_demuxer_get_major_brand(d, brand);

    faam_gapless_info gapless;
    memset(&gapless, 0, sizeof(gapless));
    gapless.struct_size = sizeof(gapless);
    (void)faam_demuxer_get_track_gapless(d, 0, &gapless);

    uint32_t meta_count = 0;
    if (faam_demuxer_get_num_metadata(d, &meta_count) == FAAM_OK) {
        if (meta_count > (1u << 20))
            __builtin_trap();
        for (uint32_t i = 0; i < meta_count && i < 2048; i++) {
            faam_metadata_entry entry;
            memset(&entry, 0, sizeof(entry));
            entry.struct_size = sizeof(entry);
            if (faam_demuxer_get_metadata_entry(d, i, &entry) != FAAM_OK)
                __builtin_trap();
            if (entry.struct_size < sizeof(entry))
                __builtin_trap();
        }
    }

    uint32_t chapter_count = 0;
    if (faam_demuxer_get_num_chapters(d, &chapter_count) == FAAM_OK) {
        if (chapter_count > 255)
            __builtin_trap();
        for (uint32_t i = 0; i < chapter_count && i < 16; i++) {
            faam_chapter chapter;
            memset(&chapter, 0, sizeof(chapter));
            chapter.struct_size = sizeof(chapter);
            (void)faam_demuxer_get_chapter(d, i, &chapter);
        }
        /* One past the end must be rejected rather than read out of bounds. */
        faam_chapter extra;
        memset(&extra, 0, sizeof(extra));
        extra.struct_size = sizeof(extra);
        (void)faam_demuxer_get_chapter(d, chapter_count, &extra);
    }

    for (uint32_t i = 0; i < held && i < 8; i++) {
        faam_track_info ti;
        memset(&ti, 0, sizeof(ti));
        ti.struct_size = sizeof(ti);
        if (faam_demuxer_get_track_info(d, i, &ti) != FAAM_OK)
            __builtin_trap();
        if (ti.struct_size < sizeof(ti))
            __builtin_trap();
        if (ti.flags & ~FAAM_TRACK_INFO_INBAND_PARAMS)
            __builtin_trap();

        faam_gapless_info track_gap;
        memset(&track_gap, 0, sizeof(track_gap));
        track_gap.struct_size = sizeof(track_gap);
        (void)faam_demuxer_get_track_gapless(d, ti.track_id, &track_gap);

        uint32_t codec_len = 0;
        if (faam_demuxer_get_codec_data(d, ti.track_id, NULL, 0, &codec_len) == FAAM_OK &&
            codec_len <= (1u << 20)) {
            uint8_t stack_buf[256];
            uint8_t *buf = codec_len <= sizeof(stack_buf) ? stack_buf : malloc(codec_len);
            if (buf) {
                uint32_t got = 0;
                (void)faam_demuxer_get_codec_data(d, ti.track_id, buf, codec_len, &got);
                if (got > codec_len)
                    __builtin_trap();
                if (buf != stack_buf)
                    free(buf);
            }
        }
    }

    faam_frame_loc loc;
    memset(&loc, 0, sizeof(loc));
    loc.struct_size = sizeof(loc);
    uint8_t frame_buf[4096];
    int frame_count = 0;

    while (frame_count < 128) {
        memset(&loc, 0, sizeof(loc));
        loc.struct_size = sizeof(loc);
        st = faam_demuxer_next_frame_loc(d, &loc);
        if (st != FAAM_OK) break;
        if (loc.struct_size < sizeof(loc))
            __builtin_trap();
        if (loc.flags & ~FAAM_FRAME_KEYFRAME)
            __builtin_trap();

        uint32_t read_bytes = 0;
        if (data[0] & 2) {
            /* Skip without reading, proving the cursor advances. */
            if (faam_demuxer_read_frame(d, NULL, 0, &read_bytes) != FAAM_OK)
                __builtin_trap();
        } else if (loc.frame_bytes > sizeof(frame_buf) && loc.frame_bytes <= (1u << 20)) {
            /* Undersized first, then retry at the required size. */
            uint32_t needed = 0;
            st = faam_demuxer_read_frame(d, frame_buf, sizeof(frame_buf), &needed);
            if (st == FAAM_ERR_OUTPUT_TOO_SMALL) {
                if (needed != loc.frame_bytes)
                    __builtin_trap();
                uint8_t *big = malloc(needed);
                if (big) {
                    st = faam_demuxer_read_frame(d, big, needed, &read_bytes);
                    free(big);
                    if (st != FAAM_OK)
                        break;
                    if (read_bytes != loc.frame_bytes)
                        __builtin_trap();
                } else {
                    st = faam_demuxer_read_frame(d, NULL, 0, &read_bytes);
                }
            }
            if (st != FAAM_OK)
                break;
        } else {
            uint32_t cap = loc.frame_bytes < sizeof(frame_buf) ? loc.frame_bytes : sizeof(frame_buf);
            st = faam_demuxer_read_frame(d, frame_buf, cap, &read_bytes);
            if (st == FAAM_OK && read_bytes != loc.frame_bytes)
                __builtin_trap();
            if (st != FAAM_OK && st != FAAM_ERR_OUTPUT_TOO_SMALL)
                break;
            if (st == FAAM_ERR_OUTPUT_TOO_SMALL) {
                /* Consume it without buffering so the loop makes progress. */
                (void)faam_demuxer_read_frame(d, NULL, 0, &read_bytes);
            }
        }
        frame_count++;
    }

    faam_demuxer_close(&d);
    return 0;
}
