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
 * libFuzzer harness for faam roundtrip:
 * fuzz-derived track config/frames/metadata/gapless -> faam muxer -> in-memory file -> demuxer;
 * assert frame count, sizes, bytes, gapless and metadata survive. A failed assertion must abort().
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
        size_t new_cap = (s->pos + bytes_to_write) * 2 + 4096;
        if (new_cap > 10 * 1024 * 1024) return -1;
        uint8_t *p = realloc(s->buf, new_cap);
        if (!p) return -1;
        s->buf = p;
        s->cap = new_cap;
    }
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

static bool rw_flush(void *user_data)
{
    (void)user_data;
    return true;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 16) return 0;

    faam_library_info lib;
    memset(&lib, 0, sizeof(lib));
    lib.struct_size = sizeof(lib);
    if (faam_get_library_info(&lib) != FAAM_OK || lib.struct_size < sizeof(lib))
        __builtin_trap();
    if (lib.flags & ~(FAAM_LIB_VIDEO | FAAM_LIB_FRAGMENTED))
        __builtin_trap();

    uint8_t mode = data[0];
    uint8_t frame_count_target = (data[1] % 8) + 1;
    const uint8_t *payload = data + 2;
    size_t payload_len = size - 2;

    faam_muxer_config cfg;
    faam_muxer_config_init(&cfg, sizeof(cfg));

    cfg.fragment_ms = (mode & 1) ? 1000 : 0;
    if (mode & 2) cfg.flags |= FAAM_MUXER_M4B;
    if (mode & 8) cfg.flags |= FAAM_MUXER_CONSTANT_RATE;
    /* faststart rewrites the file and is invalid with fragmentation */
    if ((mode & 4) && !cfg.fragment_ms) cfg.flags |= FAAM_MUXER_FASTSTART;

    faam_gapless_info gap; gap.struct_size = sizeof(gap);
    gap.encoder_delay = 1024;
    gap.end_padding = 640;
    gap.total_samples = 240000;
    cfg.gapless = &gap;

    char title_str[16];
    snprintf(title_str, sizeof(title_str), "Title_%u", mode);
    faam_metadata_entry meta_entry = {
        .struct_size = sizeof(faam_metadata_entry),
        .key = FAAM_METADATA_KEY_TITLE,
        .data_type = FAAM_METADATA_UTF8,
        .data = title_str,
        .data_bytes = (uint32_t)strlen(title_str),
    };

    cfg.metadata = &meta_entry; cfg.num_metadata = 1;

    static const uint8_t asc_sample[2] = { 0x11, 0x90 }; /* 48kHz stereo LC */

    faam_track_config trk;
    memset(&trk, 0, sizeof(trk));
    trk.struct_size = sizeof(trk);
    trk.track_type = FAAM_TRACK_AUDIO;
    trk.codec_id = FAAM_CODEC_AAC;
    trk.timescale = 48000;
    trk.sample_rate = 48000;
    trk.channels = 2;
    trk.codec_data = asc_sample;
    trk.codec_data_len = sizeof(asc_sample);

    uint32_t track_id = 0;
    cfg.tracks = &trk;
    cfg.num_tracks = 1;

    rw_mem_stream ms;
    ms.cap = 16384;
    ms.buf = malloc(ms.cap);
    if (!ms.buf) return 0;
    ms.size = 0;
    ms.pos = 0;

    faam_io io = { .struct_size = sizeof(faam_io), .user_data = &ms, .read = rw_read, .write = rw_write, .seek = rw_seek, .tell = rw_tell, .flush = rw_flush };

    faam_muxer *m = NULL;
    if (faam_muxer_open(&cfg, &io, &m) != FAAM_OK || !m) {
        free(ms.buf);
        return 0;
    }

    if (faam_muxer_get_track_id(m, 0, &track_id) != FAAM_OK) abort();

    /* Late configuration the progressive muxer accepts before finalize. */
    if (!cfg.fragment_ms) {
        faam_muxer_update_params update;
        memset(&update, 0, sizeof(update));
        update.struct_size = sizeof(update);
        update.flags = FAAM_UPDATE_LANGUAGE | FAAM_UPDATE_CREATION_TIME;
        update.track_id = track_id;
        update.creation_time = 1700000000u + mode;
        memcpy(update.language, "eng", 4);
        if (faam_muxer_update(m, &update) != FAAM_OK) abort();
    }

    uint32_t written_frames = 0;
    uint32_t frame_sizes[16];
    size_t frame_offsets_in_payload[16];

    size_t p_pos = 0;
    for (uint32_t i = 0; i < frame_count_target && p_pos < payload_len; i++) {
        uint32_t f_size = (payload[p_pos] % 128) + 1;
        p_pos++;
        if (p_pos + f_size > payload_len) f_size = (uint32_t)(payload_len - p_pos);
        if (f_size == 0) break;

        frame_sizes[i] = f_size;
        frame_offsets_in_payload[i] = p_pos;

        faam_status st = faam_muxer_write_frame(m, track_id, payload + p_pos, f_size, 1024, 0, FAAM_FRAME_KEYFRAME);
        if (st != FAAM_OK) break;

        p_pos += f_size;
        written_frames++;
    }

    if (written_frames == 0) {
        faam_muxer_close(&m);
        free(ms.buf);
        return 0;
    }

    if (!cfg.fragment_ms) {
        faam_muxer_info mi;
        memset(&mi, 0, sizeof(mi));
        mi.struct_size = sizeof(mi);
        if (faam_muxer_get_info(m, track_id, &mi) != FAAM_OK) abort();
        if (mi.struct_size < sizeof(mi) || mi.frame_count != written_frames) abort();
    }

    if (faam_muxer_finalize(m) != FAAM_OK) {
        faam_muxer_close(&m);
        free(ms.buf);
        return 0;
    }

    faam_muxer_close(&m);

    /* Assert demuxing retrieves the frame count, sizes, bytes, gapless and metadata */
    ms.pos = 0;
    faam_demuxer *d = NULL;
    if (faam_demuxer_open(NULL, &io, &d) != FAAM_OK || !d) {
        abort();
    }

    faam_gapless_info demux_gap; demux_gap.struct_size = sizeof(demux_gap);
    if (faam_demuxer_get_track_gapless(d, 0, &demux_gap) == FAAM_OK) {
        if (demux_gap.encoder_delay != gap.encoder_delay || demux_gap.end_padding != gap.end_padding) {
            abort();
        }
    }

    uint32_t demux_meta_count = 0;
    if (faam_demuxer_get_num_metadata(d, &demux_meta_count) == FAAM_OK) {
        /* Gapless muxing adds an iTunSMPB entry next to the title. */
        bool found_title = false;
        for (uint32_t i = 0; i < demux_meta_count; i++) {
            faam_metadata_entry demux_entry = { .struct_size = sizeof(demux_entry) };
            if (faam_demuxer_get_metadata_entry(d, i, &demux_entry) != FAAM_OK) abort();
            if (demux_entry.key != FAAM_METADATA_KEY_TITLE) continue;
            if (demux_entry.data_type != FAAM_METADATA_UTF8 ||
                demux_entry.data_bytes != strlen(title_str) ||
                memcmp(demux_entry.data, title_str, demux_entry.data_bytes) != 0) abort();
            found_title = true;
        }
        if (!found_title) abort();
    }

    uint32_t demuxed_count = 0;
    faam_frame_loc loc; loc.struct_size = sizeof(loc);
    uint8_t read_buf[256];

    while (faam_demuxer_next_frame_loc(d, &loc) == FAAM_OK) {
        if (demuxed_count >= written_frames) {
            abort();
        }

        if (loc.frame_bytes != frame_sizes[demuxed_count]) {
            abort();
        }

        uint32_t read_bytes = 0;
        if (faam_demuxer_read_frame(d, read_buf, sizeof(read_buf), &read_bytes) != FAAM_OK) {
            abort();
        }

        if (read_bytes != loc.frame_bytes) {
            abort();
        }

        const uint8_t *orig = payload + frame_offsets_in_payload[demuxed_count];
        if (memcmp(read_buf, orig, read_bytes) != 0) {
            abort();
        }

        demuxed_count++;
    }

    if (demuxed_count != written_frames) {
        abort();
    }

    faam_demuxer_close(&d);
    free(ms.buf);
    return 0;
}
