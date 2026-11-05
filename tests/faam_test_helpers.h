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

/* Shared test scaffolding for caller-owned track lists and indexed chapters. */
#ifndef FAAM_TEST_HELPERS_H
#define FAAM_TEST_HELPERS_H

/* A build without muxer-chapters rejects chapter input and reads none back; n chapters become 0. */
#ifndef FAAM_MUXER_CHAPTERS
#error "build tests with faam_args so FAAM_MUXER_CHAPTERS is 0 or 1"
#endif
#define TEST_CHAPTERS(n) (FAAM_MUXER_CHAPTERS ? (n) : 0)
#define TEST_HAVE_CHAPTERS (TEST_CHAPTERS(1) != 0)
#define TEST_CHAPTER_UPDATE_STATUS (TEST_HAVE_CHAPTERS ? FAAM_OK : FAAM_ERR_NOT_BUILT)

#define TEST_KEY_TITLE FAAM_FOURCC(0xa9, 'n', 'a', 'm')
#define TEST_KEY_ARTIST FAAM_FOURCC(0xa9, 'A', 'R', 'T')

static inline faam_metadata_entry test_text(uint32_t key, const char *s)
{
    faam_metadata_entry e = { .struct_size = sizeof(e), .key = key, .data_type = FAAM_METADATA_UTF8,
                              .data = s, .data_bytes = (uint32_t)strlen(s) };
    return e;
}

/* Copy the first value of a key as text; false when the key is absent. */
static inline bool test_get_text(faam_demuxer *d, uint32_t key, char *out, size_t cap)
{
    uint32_t meta_count = 0;
    if (faam_demuxer_get_num_metadata(d, &meta_count) != FAAM_OK) return false;
    for (uint32_t i = 0; i < meta_count; i++) {
        faam_metadata_entry e = { .struct_size = sizeof(e) };
        if (faam_demuxer_get_metadata_entry(d, i, &e) != FAAM_OK || e.key != key) continue;
        snprintf(out, cap, "%.*s", (int)e.data_bytes, (const char *)e.data);
        return true;
    }
    return false;
}

/* Set the title and, when given, the artist through the patch API. */
static inline faam_status test_set_tags(const faam_io *io, const char *title, const char *artist, uint32_t flags)
{
    faam_metadata_entry e[2];
    uint32_t n = 0;
    if (title) e[n++] = test_text(TEST_KEY_TITLE, title);
    if (artist) e[n++] = test_text(TEST_KEY_ARTIST, artist);
    return faam_update_metadata_stream(io, e, n, flags);
}

static inline int32_t test_sink_write(void *user, const void *data, uint32_t size)
{
    (void)data; *(uint64_t *)user += size; return (int32_t)size;
}
static inline bool test_sink_seek(void *user, uint64_t pos)
{
    *(uint64_t *)user = pos; return true;
}
static inline uint64_t test_sink_tell(void *user) { return *(uint64_t *)user; }

/* Exercise track validation through open, where the new API validates content. */
static inline faam_status test_append_track(faam_muxer_config *cfg,
                                           faam_track_config tracks[8],
                                           const faam_track_config *track,
                                           uint32_t *id)
{
    if (cfg->num_tracks >= 8) return FAAM_ERR_INVALID_ARGUMENT;
    tracks[cfg->num_tracks] = *track;
    cfg->tracks = tracks;
    cfg->num_tracks++;
    uint64_t pos = 0;
    faam_io io = { .struct_size = sizeof(io), .user_data = &pos, .read = NULL, .write = test_sink_write, .seek = test_sink_seek, .tell = test_sink_tell, .flush = NULL };
    faam_muxer *m = NULL;
    faam_status st = faam_muxer_open(cfg, &io, &m);
    if (st == FAAM_OK && id) st = faam_muxer_get_track_id(m, cfg->num_tracks - 1, id);
    faam_muxer_close(&m);
    if (st != FAAM_OK) { cfg->num_tracks--; return st; }
    return FAAM_OK;
}

static inline faam_status test_read_chapters(faam_demuxer *d, faam_chapter *chapters,
                                            uint32_t capacity, uint32_t *count)
{
    faam_status st = faam_demuxer_get_num_chapters(d, count);
    if (st != FAAM_OK) return st;
    for (uint32_t i = 0; chapters && i < capacity && i < *count; i++) {
        chapters[i].struct_size = sizeof(chapters[i]);
        st = faam_demuxer_get_chapter(d, i, &chapters[i]);
        if (st != FAAM_OK) return st;
    }
    return FAAM_OK;
}
#endif
