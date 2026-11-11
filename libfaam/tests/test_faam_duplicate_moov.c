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
 * A file with more than one top-level moov: readers parse the first and ignore the rest, so the
 * in-place writers must edit the first as well. Inputs are muxed here, then a second moov is
 * appended, so no fixture files are needed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "faam.h"
#include "mem_io.h"
#include "../../tests/faam_test_helpers.h"

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

typedef enum { LAYOUT_PROGRESSIVE, LAYOUT_FASTSTART, LAYOUT_FRAGMENTED } layout;

/* Titles have equal length so two files of one layout differ only in that text. */
static void mux(mem_file *f, layout l, const char *title)
{
    static const uint8_t asc[] = { 0x12, 0x10 }, frame[] = { 0x21, 0x10, 0x04, 0x60, 0x8c, 0x1c };
    faam_metadata_entry meta = test_text(TEST_KEY_TITLE, title);
    faam_track_config track = { .struct_size = sizeof(track), .track_type = FAAM_TRACK_AUDIO,
        .codec_id = FAAM_CODEC_AAC, .timescale = 44100, .sample_rate = 44100, .channels = 2,
        .codec_data = asc, .codec_data_len = sizeof(asc) };
    faam_muxer_config cfg;
    CHECK(faam_muxer_config_init(&cfg, sizeof(cfg)) == FAAM_OK);
    cfg.tracks = &track; cfg.num_tracks = 1; cfg.metadata = &meta; cfg.num_metadata = 1;
    if (l == LAYOUT_FASTSTART) cfg.flags = FAAM_MUXER_FASTSTART;
    if (l == LAYOUT_FRAGMENTED) cfg.fragment_ms = 50;
    memset(f, 0, sizeof(*f));
    faam_io io = mem_file_io(f);
    faam_muxer *m = NULL;
    CHECK(faam_muxer_open(&cfg, &io, &m) == FAAM_OK);
    for (unsigned i = 0; i < 8; i++) CHECK(faam_muxer_write_frame(m, 1, frame, sizeof(frame), 1024, 0, 0) == FAAM_OK);
    CHECK(faam_muxer_finalize(m) == FAAM_OK);
    CHECK(faam_muxer_close(&m) == FAAM_OK);
}

/* Offset and size of the first top-level box of a type; false when absent. */
static bool top_box(const mem_file *f, const char *type, size_t *at, size_t *size)
{
    for (size_t p = 0; p + 8 <= f->len;) {
        size_t z = mem_be32(f->buf + p);
        if (z < 8 || z > f->len - p) return false;
        if (!memcmp(f->buf + p + 4, type, 4)) { *at = p; *size = z; return true; }
        p += z;
    }
    return false;
}

static unsigned count_top(const mem_file *f, const char *type)
{
    unsigned n = 0;
    for (size_t p = 0; p + 8 <= f->len;) {
        size_t z = mem_be32(f->buf + p);
        if (z < 8 || z > f->len - p) break;
        if (!memcmp(f->buf + p + 4, type, 4)) n++;
        p += z;
    }
    return n;
}

static bool read_title(mem_file *f, char *out, size_t cap)
{
    f->pos = 0;
    faam_io io = mem_file_io(f);
    faam_demuxer *d = NULL;
    if (faam_demuxer_open(NULL, &io, &d) != FAAM_OK) return false;
    bool ok = test_get_text(d, TEST_KEY_TITLE, out, cap);
    faam_demuxer_close(&d);
    return ok;
}

static int track_count(mem_file *f)
{
    f->pos = 0;
    faam_io io = mem_file_io(f);
    faam_demuxer *d = NULL;
    if (faam_demuxer_open(NULL, &io, &d) != FAAM_OK) return -1;
    uint32_t n = 0;
    faam_status st = faam_demuxer_get_num_tracks(d, &n, NULL);
    faam_demuxer_close(&d);
    return st == FAAM_OK ? (int)n : -1;
}

/* Appends the moov of other behind everything in f. */
static void append_moov(mem_file *f, const mem_file *other)
{
    size_t at, size;
    CHECK(top_box(other, "moov", &at, &size));
    f->pos = f->len;
    mem_put(f, other->buf + at, size);
}

/* Both moov carry the same track layout, so only the title tells them apart. */
static void edit_lands_in_first_moov(layout l)
{
    mem_file first, second;
    mux(&first, l, "AAAAA");
    mux(&second, l, "BBBBB");
    append_moov(&first, &second);
    CHECK(count_top(&first, "moov") == 2);

    size_t at, size;
    CHECK(top_box(&second, "moov", &at, &size));
    uint8_t *tail = (uint8_t *)malloc(size);
    memcpy(tail, second.buf + at, size);

    char title[32];
    CHECK(read_title(&first, title, sizeof(title)) && !strcmp(title, "AAAAA"));

    faam_io io = mem_file_io(&first);
    first.pos = 0;
    CHECK(test_set_tags(&io, "A much longer replacement title", "Artist", 0) == FAAM_OK);
    CHECK(read_title(&first, title, sizeof(title)) && !strcmp(title, "A much longer replacement title"));
    CHECK(track_count(&first) == 1);
    CHECK(count_top(&first, "moov") == 2);
    /* The ignored moov keeps its bytes; the first one grew and moved it. */
    CHECK(first.len >= size && !memcmp(first.buf + first.len - size, tail, size));

    if (TEST_HAVE_CHAPTERS) {
        faam_chapter ch[2] = { { .struct_size = sizeof(ch[0]), .start_ms = 0, .title = "One" },
                               { .struct_size = sizeof(ch[0]), .start_ms = 100, .title = "Two" } };
        io = mem_file_io(&first);
        first.pos = 0;
        CHECK(faam_update_chapters_stream(&io, ch, 2, 0) == FAAM_OK);
        first.pos = 0;
        faam_demuxer *d = NULL;
        CHECK(faam_demuxer_open(NULL, &io, &d) == FAAM_OK);
        uint32_t n = 0;
        CHECK(faam_demuxer_get_num_chapters(d, &n) == FAAM_OK && n == 2);
        faam_demuxer_close(&d);
        /* Chapter samples now trail the file, so the ignored moov is no longer last. */
        CHECK(count_top(&first, "moov") == 2);
        CHECK(read_title(&first, title, sizeof(title)) && !strcmp(title, "A much longer replacement title"));
    }
    free(tail);
    free(first.buf);
    free(second.buf);
}

/* An unreadable trailing moov must not take the track away when the real one is edited. */
static void empty_trailing_moov(void)
{
    mem_file f;
    mux(&f, LAYOUT_PROGRESSIVE, "AAAAA");
    f.pos = f.len;
    mem_put32(&f, 8);
    mem_put(&f, "moov", 4);
    CHECK(track_count(&f) == 1);

    faam_io io = mem_file_io(&f);
    if (TEST_HAVE_CHAPTERS) {
        faam_chapter ch = { .struct_size = sizeof(ch), .start_ms = 0, .title = "One" };
        f.pos = 0;
        CHECK(faam_update_chapters_stream(&io, &ch, 1, 0) == FAAM_OK);
        CHECK(track_count(&f) >= 1);
    }
    f.pos = 0;
    CHECK(test_set_tags(&io, "Edited", NULL, 0) == FAAM_OK);
    char title[32];
    CHECK(read_title(&f, title, sizeof(title)) && !strcmp(title, "Edited"));
    free(f.buf);
}

/* Only the moov that is edited needs a 32-bit size; an ignored one may use a 64-bit header. */
static void wide_trailing_moov(void)
{
    mem_file f;
    mux(&f, LAYOUT_PROGRESSIVE, "AAAAA");
    f.pos = f.len;
    mem_put32(&f, 1);
    mem_put(&f, "moov", 4);
    mem_put64(&f, 16);

    faam_io io = mem_file_io(&f);
    f.pos = 0;
    CHECK(test_set_tags(&io, "Edited", NULL, 0) == FAAM_OK);
    char title[32];
    CHECK(read_title(&f, title, sizeof(title)) && !strcmp(title, "Edited"));
    free(f.buf);
}

/* A single moov is edited as before. */
static void single_moov(layout l)
{
    mem_file f;
    mux(&f, l, "AAAAA");
    CHECK(count_top(&f, "moov") == 1);
    faam_io io = mem_file_io(&f);
    f.pos = 0;
    CHECK(test_set_tags(&io, "Edited", NULL, 0) == FAAM_OK);
    char title[32];
    CHECK(read_title(&f, title, sizeof(title)) && !strcmp(title, "Edited"));
    CHECK(track_count(&f) == 1);
    free(f.buf);
}

int main(void)
{
    edit_lands_in_first_moov(LAYOUT_PROGRESSIVE);
    edit_lands_in_first_moov(LAYOUT_FASTSTART);
    single_moov(LAYOUT_PROGRESSIVE);
    single_moov(LAYOUT_FASTSTART);
#if FAAM_MUXER_FRAGMENTED
    edit_lands_in_first_moov(LAYOUT_FRAGMENTED);
    single_moov(LAYOUT_FRAGMENTED);
#endif
    empty_trailing_moov();
    wide_trailing_moov();
    return 0;
}
