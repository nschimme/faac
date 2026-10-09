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
 * Instance blocks use only the two allocator hooks, and fragmented muxing
 * must not allocate after open. Every allocation failure must be leak-free. This test
 * links libfaam's sources directly so its allocator hook is in effect.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libfaam_internal.h"
#include "../tests/faam_test_helpers.h"



#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static unsigned g_allocs, g_fail_at;
static long g_live;
static size_t g_limit = SIZE_MAX, g_bytes, g_peak, g_largest;
typedef union { max_align_t align; struct { size_t size; } v; } allocation;

void *faam_counted_alloc(size_t size)
{
    g_allocs++;
    if (g_allocs == g_fail_at || size > g_limit) return NULL;
    allocation *a = malloc(sizeof(*a) + size + 16);
    if (!a) return NULL;
    a->v.size = size;
    memset((unsigned char *)(a + 1) + size, 0xA5, 16);
    g_live++;
    g_bytes += size;
    if (g_bytes > g_peak) g_peak = g_bytes;
    if (size > g_largest) g_largest = size;
    return a + 1;
}

void faam_counted_free(void *block)
{
    if (!block) return;
    allocation *a = (allocation *)block - 1;
    for (unsigned i = 0; i < 16; i++) CHECK(((unsigned char *)block)[a->v.size + i] == 0xA5);
    g_live--;
    g_bytes -= a->v.size;
    free(a);
}

static void reset(unsigned fail)
{
    CHECK(!g_live && !g_bytes);
    g_allocs = 0;
    g_fail_at = fail;
    g_peak = g_largest = 0;
}

/* Counts positions only; payload bytes are discarded. */
typedef struct { uint64_t pos; } sink;

static int32_t sink_write(void *user, const void *data, uint32_t n) { (void)data; ((sink *)user)->pos += n; return (int32_t)n; }
static bool sink_seek(void *user, uint64_t pos) { ((sink *)user)->pos = pos; return true; }
static uint64_t sink_tell(void *user) { return ((sink *)user)->pos; }

enum { FRAMES = 3000 };

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

/* Exercise table growth, codec storage and open I/O cleanup under every
 * possible allocation failure. The sink never allocates itself. */
static int32_t failing_write(void *, const void *, uint32_t);
static bool failing_flush(void *);

static faam_status mux_run(const faam_muxer_config *cfg, int io_failure)
{
    sink out = {0};
    faam_io io = { sizeof(faam_io), &out, NULL, sink_write, sink_seek, sink_tell, NULL };
    if (io_failure == 1) io.write = failing_write;
    if (io_failure == 2) io.flush = failing_flush;
    faam_muxer *m = (faam_muxer *)0x1;
    faam_status st = faam_muxer_open(cfg, &io, &m);
    if (st != FAAM_OK) { CHECK(!m); return st; }
    uint8_t frame[256] = {0};
    unsigned opened = g_allocs;
    for (unsigned i = 0; i < FRAMES && st == FAAM_OK; i++) {
        st = faam_muxer_write_frame(m, 1, frame, sizeof(frame), 1024 + i % 2, 0, FAAM_FRAME_KEYFRAME);
#ifdef FAAM_MUXER_VIDEO
        if (cfg->num_tracks > 1 && st == FAAM_OK)
            st = faam_muxer_write_frame(m, 2, frame, sizeof(frame), 3000, (int32_t)(i % 3) * 3000, FAAM_FRAME_KEYFRAME);
#endif
    }
    if (st == FAAM_OK) st = faam_muxer_finalize(m);
    if (cfg->fragment_ms) CHECK(g_allocs == opened);
    faam_muxer_close(&m);
    CHECK(!m);
    return st;
}

static int32_t failing_write(void *user, const void *data, uint32_t n)
{ (void)user; (void)data; (void)n; return -1; }
static bool failing_flush(void *user) { (void)user; return false; }

static void mux_failures(faam_muxer_config *cfg)
{
    reset(0);
    CHECK(mux_run(cfg, 0) == FAAM_OK);
    unsigned calls = g_allocs;
    CHECK(!g_live);
    for (unsigned fail = 1; fail <= calls; fail++) {
        reset(fail);
        CHECK(mux_run(cfg, 0) == FAAM_ERR_INSUFFICIENT_MEM);
        CHECK(!g_live);
    }
    reset(0);
    CHECK(mux_run(cfg, 1) == FAAM_ERR_IO_WRITE);
    CHECK(!g_live);
    reset(0);
    CHECK(mux_run(cfg, 2) == FAAM_ERR_IO_WRITE);
    CHECK(!g_live);
}

static faam_status demux_run(memfile *f, bool update)
{
    f->pos = 0;
    faam_io io = { sizeof(faam_io), f, mem_read, mem_write, mem_seek, mem_tell, NULL };
    faam_demuxer *d = (faam_demuxer *)0x1;
    faam_status st = faam_demuxer_open(NULL, &io, &d);
    if (st != FAAM_OK) { CHECK(!d); return st; }
    if (update) {
        faam_metadata meta = { .struct_size = sizeof(meta), .title = "replacement" };
        st = faam_update_tags_stream(&io, &meta, 0);
    } else {
        faam_frame_loc loc = { .struct_size = sizeof(loc) };
        while ((st = faam_demuxer_next_frame_loc(d, &loc)) == FAAM_OK) {
            uint32_t skipped;
            st = faam_demuxer_read_frame(d, NULL, 0, &skipped);
            if (st != FAAM_OK) break;
        }
        if (st == FAAM_END_OF_STREAM) st = FAAM_OK;
    }
    faam_demuxer_close(&d);
    CHECK(!d);
    return st;
}

static void demux_failures(faam_muxer_config *cfg, bool update)
{
    memfile source = {0};
    faam_io io = { sizeof(faam_io), &source, mem_read, mem_write, mem_seek, mem_tell, NULL };
    faam_muxer *m = NULL;
    reset(0);
    CHECK(faam_muxer_open(cfg, &io, &m) == FAAM_OK);
    write_frames(m, cfg->num_tracks > 1);
    CHECK(faam_muxer_finalize(m) == FAAM_OK);
    faam_muxer_close(&m);
    unsigned calls = 0;
    for (unsigned fail = 0; fail <= calls; fail++) {
        memfile f = { .len = source.len, .cap = source.len };
        f.buf = malloc(f.cap); CHECK(f.buf);
        memcpy(f.buf, source.buf, f.len);
        reset(fail);
        faam_status st = demux_run(&f, update);
        if (!fail) { CHECK(st == FAAM_OK); calls = g_allocs; }
        else {
            if (st != FAAM_ERR_INSUFFICIENT_MEM) fprintf(stderr, "demux failure %u/%u, update=%d, fragment=%u, status=%d\n", fail, calls, update, cfg->fragment_ms, st);
            CHECK(st == FAAM_ERR_INSUFFICIENT_MEM);
        }
        CHECK(!g_live);
        free(f.buf);
    }
    free(source.buf);
}

static void test_growth(void)
{
    reset(0);
    unsigned char *old = AllocMemory(64); CHECK(old);
    memset(old, 0x37, 64);
    g_fail_at = g_allocs + 1;
    CHECK(!faam_grow_memory(old, 64, 128));
    for (unsigned i = 0; i < 64; i++) CHECK(old[i] == 0x37);
    g_fail_at = 0;
    unsigned char *grown = faam_grow_memory(old, 64, 128); CHECK(grown);
    for (unsigned i = 0; i < 64; i++) CHECK(grown[i] == 0x37);
    FreeMemory(grown);
    CHECK(!g_live);
}

int main(void)
{
    test_growth();
    reset(0);
    test_repeated_table_box();
    CHECK(!g_live);

    faam_muxer_config cfg;
    faam_track_config tracks[8];
    CHECK(faam_muxer_config_init(&cfg, sizeof(cfg)) == FAAM_OK);
    add_tracks(&cfg, tracks);
    /* AAC esds accepts an opaque decoder config; exercise its owned extension. */
    uint8_t codec[512] = { 0x12, 0x10 };
    tracks[0].codec_data = codec;
    tracks[0].codec_data_len = sizeof(codec);
    faam_custom_tag tags[32];
    for (unsigned i = 0; i < 32; i++) tags[i] = (faam_custom_tag){ .struct_size = sizeof(faam_custom_tag), .mean = "com.example", .name = "key", .value = "value" };
    faam_metadata meta = { .struct_size = sizeof(meta), .num_custom_tags = 32, .custom_tags = tags };
    uint8_t cover[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    meta.cover_art = cover; meta.cover_bytes = sizeof(cover); meta.cover_type = FAAM_COVER_PNG;
    cfg.metadata = &meta;
    faam_chapter chapters[2] = {
        { .struct_size = sizeof(faam_chapter), .title = "first" },
        { .struct_size = sizeof(faam_chapter), .start_ms = 1000, .title = "second" },
    };
    cfg.chapters = chapters; cfg.num_chapters = 2;
    mux_failures(&cfg);
    demux_failures(&cfg, false);
    demux_failures(&cfg, true);
#ifdef FAAM_MUXER_FRAGMENTED
    cfg.fragment_ms = 10000;
    mux_failures(&cfg);
    demux_failures(&cfg, false);
    reset(0);
    g_limit = 6000; /* Former combined index alone needed at least 12,800 bytes. */
    CHECK(mux_run(&cfg, 0) == FAAM_OK);
    CHECK(g_largest <= g_limit && !g_live);
    printf("fragmented: largest request %zu bytes, peak live %zu bytes\n", g_largest, g_peak);
    g_limit = SIZE_MAX;
#endif
    puts("libfaam allocation tests passed.");
    return 0;
}
