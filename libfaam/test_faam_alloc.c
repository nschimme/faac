/*
 * FAAM - Freeware Advanced Audio/Video Muxer
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
 * Fragmented muxing must not allocate once the caller's arena is handed over,
 * and the arena get_state_size reports must be all it ever touches. This test
 * links libfaam's sources directly so its allocator hook is in effect.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "faam.h"
#include "../tests/faam_test_helpers.h"



static unsigned g_allocs;
static long g_live;  /* blocks handed out and not yet freed */

void *faam_counted_alloc(size_t size) { g_allocs++; g_live++; return malloc(size); }
void *faam_counted_realloc(void *block, size_t size) { g_allocs++; if (!block) g_live++; return realloc(block, size); }
void faam_counted_free(void *block) { if (block) g_live--; free(block); }

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

/* Counts positions only; payload bytes are discarded. */
typedef struct { uint64_t pos; } sink;

static int32_t sink_write(void *user, const void *data, uint32_t n) { (void)data; ((sink *)user)->pos += n; return (int32_t)n; }
static bool sink_seek(void *user, uint64_t pos) { ((sink *)user)->pos = pos; return true; }
static uint64_t sink_tell(void *user) { return ((sink *)user)->pos; }

enum { CANARY = 4096, FRAMES = 3000 };

static void add_tracks(faam_muxer_config *cfg, faam_track_config tracks[8])
{
    static const uint8_t asc[2] = { 0x12, 0x10 };
    faam_track_config a;
    memset(&a, 0, sizeof(a)); a.struct_size = sizeof(a);
    a.track_type = FAAM_TRACK_AUDIO; a.codec_id = FAAM_CODEC_AAC;
    a.timescale = a.sample_rate = 48000; a.channels = 2;
    a.codec_data = asc; a.codec_data_len = sizeof(asc);
    CHECK(test_append_track(cfg, tracks, &a, NULL) == FAAM_OK);
#ifdef FAAM_MUXER_VIDEO
    static const uint8_t avcc[] = { 1, 0x64, 0, 0x1F, 0xFF, 0xE1, 0, 2, 0x67, 0x64, 1, 0, 1, 0x68 };
    faam_track_config v;
    memset(&v, 0, sizeof(v)); v.struct_size = sizeof(v);
    v.track_type = FAAM_TRACK_VIDEO; v.codec_id = FAAM_CODEC_H264;
    v.timescale = 90000; v.width = 64; v.height = 64;
    v.codec_data = avcc; v.codec_data_len = sizeof(avcc);
    CHECK(test_append_track(cfg, tracks, &v, NULL) == FAAM_OK);
#endif
}

static void write_frames(faam_muxer *m, bool video)
{
    uint8_t frame[256];
    memset(frame, 0x5A, sizeof(frame));
    for (unsigned i = 0; i < FRAMES; i++) {
        CHECK(faam_muxer_write_frame(m, 1, frame, 40 + i % 200, 1024, 0, FAAM_FRAME_KEYFRAME) == FAAM_OK);
        if (video && i % 2 == 0)
            CHECK(faam_muxer_write_frame(m, 2, frame, 100 + i % 150, 3000, (int32_t)(i % 3) * 3000, i % 30 == 0 ? FAAM_FRAME_KEYFRAME : 0) == FAAM_OK);
    }
}

/* A growing in-memory file for both directions. */
typedef struct { uint8_t *buf; size_t len, cap, pos; } memfile;

static int32_t mem_write(void *user, const void *data, uint32_t n)
{
    memfile *f = (memfile *)user;
    if (f->pos + n > f->cap) {
        f->cap = (f->pos + n) * 2;
        f->buf = (uint8_t *)realloc(f->buf, f->cap);
        CHECK(f->buf);
    }
    memcpy(f->buf + f->pos, data, n);
    f->pos += n;
    if (f->pos > f->len) f->len = f->pos;
    return (int32_t)n;
}
static int32_t mem_read(void *user, void *out, uint32_t n)
{
    memfile *f = (memfile *)user;
    size_t avail = f->pos < f->len ? f->len - f->pos : 0;
    if (n > avail) n = (uint32_t)avail;
    memcpy(out, f->buf + f->pos, n);
    f->pos += n;
    return (int32_t)n;
}
static bool mem_seek(void *user, uint64_t pos) { ((memfile *)user)->pos = (size_t)pos; return true; }
static uint64_t mem_tell(void *user) { return ((memfile *)user)->pos; }

/* A track that repeats a sample-table box (here a second stco, made out of its stsc) is
 * corrupt, but opening and closing it must still return every block the demuxer took. */
static void test_repeated_table_box(void)
{
    faam_muxer_config cfg;
    faam_track_config cfg_tracks[8];
    CHECK(faam_muxer_config_init(&cfg, sizeof(cfg)) == FAAM_OK);
    add_tracks(&cfg, cfg_tracks);
    cfg.num_tracks = 1;
    memfile f = {0};
    faam_io io = { sizeof(faam_io), &f, mem_read, mem_write, mem_seek, mem_tell, NULL };
    faam_muxer *m;
    CHECK(faam_muxer_open(&cfg, &io, &m) == FAAM_OK);
    write_frames(m, false);
    CHECK(faam_muxer_finalize(m) == FAAM_OK);
    faam_muxer_close(&m);

    size_t at = 0;
    for (; at + 4 <= f.len; at++) if (!memcmp(f.buf + at, "stsc", 4)) break;
    CHECK(at + 4 <= f.len);
    memcpy(f.buf + at, "stco", 4);

    long before = g_live;
    f.pos = 0;
    faam_demuxer *d = NULL;
    if (faam_demuxer_open(NULL, &io, &d) == FAAM_OK) faam_demuxer_close(&d);
    CHECK(g_live == before);
    free(f.buf);
}

int main(void)
{
    test_repeated_table_box();

    faam_muxer_config cfg;
    faam_track_config cfg_tracks[8];
    CHECK(faam_muxer_config_init(&cfg, sizeof(cfg)) == FAAM_OK);
    add_tracks(&cfg, cfg_tracks);
    bool video = cfg.num_tracks == 2;

    /* Control: the progressive muxer allocates its tables, so the counter is live. */
    g_allocs = 0;
    sink out = {0};
    faam_io io = { sizeof(faam_io), &out, NULL, sink_write, sink_seek, sink_tell, NULL };
    faam_muxer *m;
    CHECK(faam_muxer_open(&cfg, &io, &m) == FAAM_OK);
    write_frames(m, video);
    CHECK(faam_muxer_finalize(m) == FAAM_OK);
    faam_muxer_close(&m);
    CHECK(g_allocs > 0);

    /* Fragmented: arena only, and the arena is never overrun. */
    cfg.fragment_ms = 500;
    uint32_t size;
    CHECK(faam_muxer_get_state_size(&cfg, &size) == FAAM_OK);
    uint8_t *arena = (uint8_t *)malloc((size_t)size + CANARY);
    CHECK(arena);
    memset(arena, 0xA5, (size_t)size + CANARY);
    g_allocs = 0;
    out.pos = 0;
    CHECK(faam_muxer_init(arena, size, &cfg, &io, &m) == FAAM_OK);
    write_frames(m, video);
    CHECK(faam_muxer_finalize(m) == FAAM_OK);
    faam_muxer_close(&m);
    CHECK(g_allocs == 0);
    for (unsigned i = 0; i < CANARY; i++) CHECK(arena[size + i] == 0xA5);
    free(arena);

    puts("libfaam allocation tests passed.");
    return 0;
}
