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
 * Durable ABI regression tests: struct sizes, strided lists, track discovery,
 * late configuration, serialized video fields and status contracts.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "faam.h"
#include "../tests/faam_test_helpers.h"

/* Public constant groups remain 32-bit enums, even with -fshort-enums. */
_Static_assert(sizeof(enum faam_feature) == 4, "feature enum width");
_Static_assert(sizeof(enum faam_track_flag) == 4, "track_flag enum width");
_Static_assert(sizeof(enum faam_track_info_flag) == 4, "track_info_flag enum width");
_Static_assert(sizeof(enum faam_cover_type) == 4, "cover_type enum width");
_Static_assert(sizeof(enum faam_muxer_flag) == 4, "muxer_flag enum width");
_Static_assert(sizeof(enum faam_update_flag) == 4, "update_flag enum width");
_Static_assert(sizeof(enum faam_frame_flag) == 4, "frame_flag enum width");
_Static_assert(sizeof(enum faam_tag_update_flag) == 4, "tag_update_flag enum width");
_Static_assert(FAAM_FEATURE_VIDEO == 1 && FAAM_FEATURE_FRAGMENTED == 2, "feature values");
_Static_assert(FAAM_TRACK_ANNEXB == 1 && FAAM_TRACK_INBAND_PARAMS == 2, "track values");
_Static_assert(FAAM_TRACK_INFO_INBAND_PARAMS == 1, "track info values");
_Static_assert(FAAM_COVER_AUTO == 0 && FAAM_COVER_JPEG == 1 && FAAM_COVER_PNG == 2
               && FAAM_COVER_GIF == 3 && FAAM_COVER_BMP == 4, "cover values");
_Static_assert(FAAM_MUXER_M4B == 1 && FAAM_MUXER_CONSTANT_RATE == 2, "muxer values");
_Static_assert(FAAM_UPDATE_CREATION_TIME == 1 && FAAM_UPDATE_GAPLESS == 2
               && FAAM_UPDATE_CODEC_DATA == 4 && FAAM_UPDATE_LANGUAGE == 8
               && FAAM_UPDATE_AUDIO_SAMPLE_SIZE == 16, "update values");
_Static_assert(FAAM_FRAME_KEYFRAME == 1 && FAAM_TAG_UPDATE_CLEAR == 1, "operation values");
_Static_assert(sizeof(((faam_metadata *)0)->cover_type) == 1, "cover storage width");
_Static_assert(sizeof(((faam_track_config *)0)->flags) == 4, "mask storage width");

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

typedef struct {
    uint8_t *buf;
    size_t len, cap, pos;
    size_t fail_write_after; /* zero: clean writes; otherwise a failing byte limit */
} memfile;

static int32_t mem_read(void *user, void *out, uint32_t n) {
    memfile *f = (memfile *)user;
    size_t avail = f->pos < f->len ? f->len - f->pos : 0;
    if (n > avail) n = (uint32_t)avail;
    memcpy(out, f->buf + f->pos, n);
    f->pos += n;
    return (int32_t)n;
}

static int32_t mem_write(void *user, const void *data, uint32_t n) {
    memfile *f = (memfile *)user;
    if (f->fail_write_after && f->pos + n > f->fail_write_after) return -1;
    if (f->pos + n > f->cap) {
        size_t cap = f->cap ? f->cap : 4096;
        while (cap < f->pos + n) cap *= 2;
        f->buf = (uint8_t *)realloc(f->buf, cap);
        CHECK(f->buf);
        f->cap = cap;
    }
    if (f->pos > f->len) memset(f->buf + f->len, 0, f->pos - f->len);
    memcpy(f->buf + f->pos, data, n);
    f->pos += n;
    if (f->pos > f->len) f->len = f->pos;
    return (int32_t)n;
}

static bool mem_seek(void *user, uint64_t pos) {
    memfile *f = (memfile *)user;
    f->pos = (size_t)pos;
    return true;
}
static uint64_t mem_tell(void *user) { return ((memfile *)user)->pos; }

static faam_io mem_io(memfile *f) {
    faam_io io = { sizeof(faam_io), f, mem_read, mem_write, mem_seek, mem_tell, NULL };
    return io;
}

static uint32_t be32(const uint8_t *b) {
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
}
static void put32(uint8_t *b, uint32_t v) { b[0] = v >> 24; b[1] = v >> 16; b[2] = v >> 8; b[3] = (uint8_t)v; }

/* Offset of the first occurrence of a four-character atom name in [from, to), or -1. */
static long find_tag_in_range(const memfile *f, const char *tag, long from, long to) {
    size_t limit = (to > 0 && (size_t)to <= f->len) ? (size_t)to : f->len;
    for (size_t i = (size_t)from; i + 4 <= limit; i++)
        if (!memcmp(f->buf + i, tag, 4)) return (long)i;
    return -1;
}

static long find_tag(const memfile *f, const char *tag, long from) {
    return find_tag_in_range(f, tag, from, 0);
}

enum { ASC_LEN = 2 };
static const uint8_t asc_lc_44k[ASC_LEN] = { 0x12, 0x10 };

/* Durable ABI regression fixtures: real containers, opaque future fields. */
#define STATUS(call, expected) do { faam_status got_ = (call); if (got_ != (expected)) { \
    fprintf(stderr, "%s:%d: %s: observed %d, expected %d\n", __FILE__, __LINE__, #call, got_, (expected)); exit(1); } } while (0)
#define FUTURE(T) struct { T v; uint8_t extra[16]; }
#define EXTEND(dst, plain) do { memset(&(dst), 0xA5, sizeof(dst)); (dst).v = (plain); (dst).v.struct_size = sizeof(dst); } while (0)

static faam_track_config audio(void) {
    faam_track_config t = { .struct_size = sizeof(t), .track_type = FAAM_TRACK_AUDIO,
        .codec_id = FAAM_CODEC_AAC, .timescale = 44100, .sample_rate = 44100,
        .channels = 2, .codec_data = asc_lc_44k, .codec_data_len = ASC_LEN };
    return t;
}
static faam_muxer *start(memfile *f, faam_muxer_config *cfg) {
    faam_io io = mem_io(f); faam_muxer *m = NULL;
    STATUS(faam_muxer_open(cfg, &io, &m), FAAM_OK); return m;
}
static faam_demuxer *open_mem(memfile *f) {
    f->pos = 0; faam_io io = mem_io(f); faam_demuxer *d = NULL;
    STATUS(faam_demuxer_open(NULL, &io, &d), FAAM_OK); return d;
}
static void frames(faam_muxer *m, unsigned n) {
    uint8_t b[8] = {1,2,3};
    for (unsigned j = 0; j < 3; j++) for (unsigned i = 1; i <= n; i++)
        STATUS(faam_muxer_write_frame(m, i, b, sizeof(b), 1024, 0, FAAM_FRAME_KEYFRAME), FAAM_OK);
}
static void advance(faam_demuxer *d) { uint32_t bytes; STATUS(faam_demuxer_read_frame(d, NULL, 0, &bytes), FAAM_OK); }
static void finish(faam_muxer **m) {
    STATUS(faam_muxer_finalize(*m), FAAM_OK); STATUS(faam_muxer_close(m), FAAM_OK);
}
#define OUTPUT(T, baseline, call) do { \
    struct { T v; uint8_t guard[32]; } out; memset(&out, 0xA5, sizeof(out)); \
    out.v.struct_size = sizeof(T) + 32; STATUS(call, FAAM_OK); \
    CHECK(out.v.struct_size == sizeof(T) + 32); \
    for (unsigned k = 0; k < 32; k++) CHECK(out.guard[k] == 0xA5); \
    out.v.struct_size = (baseline) - 1; STATUS(call, FAAM_ERR_INVALID_ARG); \
    out.v.struct_size = 0; STATUS(call, FAAM_ERR_INVALID_ARG); \
} while (0)

static void test_outputs(void) {
    memfile f = {0}; faam_track_config t = audio();
    faam_custom_tag tag = { .struct_size = sizeof(tag), .name = "abi", .value = "value" };
    faam_metadata meta = { .struct_size = sizeof(meta), .title = "ABI", .custom_tags = &tag, .num_custom_tags = 1 };
    faam_chapter ch = { .struct_size = sizeof(ch), .title = "Start" };
    faam_muxer_config cfg; STATUS(faam_muxer_config_init(&cfg, sizeof(cfg)), FAAM_OK);
    cfg.tracks = &t; cfg.num_tracks = 1; cfg.metadata = &meta; cfg.chapters = &ch; cfg.num_chapters = 1;
    faam_muxer *m = start(&f, &cfg); frames(m, 1);
    OUTPUT(faam_library_info, FAAM_LIBRARY_INFO_BASELINE, faam_get_library_info(&out.v));
    OUTPUT(faam_muxer_info, FAAM_MUXER_INFO_BASELINE, faam_muxer_get_info(m, 1, &out.v));
    finish(&m); faam_demuxer *d = open_mem(&f);
    OUTPUT(faam_track_info, FAAM_TRACK_INFO_BASELINE, faam_demuxer_get_track_info(d, 0, &out.v));
    OUTPUT(faam_gapless_info, FAAM_GAPLESS_INFO_BASELINE, faam_demuxer_get_track_gapless(d, 1, &out.v));
    OUTPUT(faam_metadata, FAAM_METADATA_BASELINE, faam_demuxer_get_metadata(d, &out.v));
    OUTPUT(faam_chapter, FAAM_CHAPTER_BASELINE, faam_demuxer_get_chapter(d, 0, &out.v));
    OUTPUT(faam_custom_tag, FAAM_CUSTOM_TAG_BASELINE, faam_demuxer_get_custom_tag(d, 0, &out.v));
    OUTPUT(faam_frame_loc, FAAM_FRAME_LOC_BASELINE, faam_demuxer_next_frame_loc(d, &out.v));
    faam_frame_loc loc = { .struct_size = sizeof(loc) };
    while (faam_demuxer_next_frame_loc(d, &loc) == FAAM_OK) advance(d);
    CHECK(FAAM_END_OF_STREAM == 1 && FAAM_END_OF_STREAM > 0);
    for (unsigned i = 0; i < 3; i++) STATUS(faam_demuxer_next_frame_loc(d, &loc), FAAM_END_OF_STREAM);
    CHECK(faam_strerror(FAAM_END_OF_STREAM)[0]);
    faam_demuxer_close(&d); free(f.buf);
}

/* All future input structs occur together, including two-element strided lists.
 * Comparing the entire file catches a library accidentally reading extension junk. */
static void test_future_inputs(void) {
    memfile plain = {0}, newer = {0};
    faam_track_config tracks[2] = { audio(), audio() };
    faam_chapter chapters[2] = { { sizeof(faam_chapter), 0, 0, "One" }, { sizeof(faam_chapter), 0, 20, "Two" } };
    faam_custom_tag tags[2] = { { sizeof(faam_custom_tag), 0, "one", "1", NULL }, { sizeof(faam_custom_tag), 0, "two", "2", "test" } };
    faam_metadata meta = { .struct_size = sizeof(meta), .title = "Future", .custom_tags = tags, .num_custom_tags = 2 };
    faam_gapless_info gap = { sizeof(gap), 10, 20, 0, 3000 };
    faam_muxer_config cfg; STATUS(faam_muxer_config_init(&cfg, sizeof(cfg)), FAAM_OK);
    cfg.tracks = tracks; cfg.num_tracks = 2; cfg.chapters = chapters; cfg.num_chapters = 2; cfg.metadata = &meta; cfg.gapless = &gap;
    faam_muxer_update_params update = { .struct_size = sizeof(update), .flags = FAAM_UPDATE_LANGUAGE, .track_id = 2, .language = "deu" };
    faam_muxer *m = start(&plain, &cfg); STATUS(faam_muxer_update(m, &update), FAAM_OK); frames(m, 2); finish(&m);
    FUTURE(faam_track_config) et[2]; FUTURE(faam_chapter) ec[2]; FUTURE(faam_custom_tag) eg[2];
    FUTURE(faam_metadata) em; FUTURE(faam_gapless_info) gapx; FUTURE(faam_muxer_config) cx;
    FUTURE(faam_muxer_update_params) ux; FUTURE(faam_io) ix; FUTURE(faam_demuxer_config) dx;
    for (unsigned i = 0; i < 2; i++) { EXTEND(et[i], tracks[i]); EXTEND(ec[i], chapters[i]); EXTEND(eg[i], tags[i]); }
    EXTEND(em, meta); em.v.custom_tags = &eg[0].v;
    EXTEND(gapx, gap); EXTEND(cx, cfg); cx.v.tracks = &et[0].v; cx.v.chapters = &ec[0].v; cx.v.metadata = &em.v; cx.v.gapless = &gapx.v;
    EXTEND(ux, update); faam_io io = mem_io(&newer); EXTEND(ix, io);
    STATUS(faam_muxer_open(&cx.v, &ix.v, &m), FAAM_OK); STATUS(faam_muxer_update(m, &ux.v), FAAM_OK); frames(m, 2); finish(&m);
    CHECK(plain.len == newer.len && !memcmp(plain.buf, newer.buf, plain.len));
    faam_demuxer_config dc = { .struct_size = sizeof(dc) }; EXTEND(dx, dc);
    newer.pos = 0; faam_demuxer *d = NULL; STATUS(faam_demuxer_open(&dx.v, &ix.v, &d), FAAM_OK); faam_demuxer_close(&d);
    et[1].v.struct_size = sizeof(faam_track_config);
    STATUS(faam_muxer_open(&cx.v, &ix.v, &m), FAAM_ERR_INVALID_ARG);
    et[1].v.struct_size = sizeof(et[1]);
#define BAD_INPUT(v, baseline, call) do { uint32_t saved = (v).struct_size; (v).struct_size = (baseline)-1; STATUS(call, FAAM_ERR_INVALID_ARG); (v).struct_size = saved; } while (0)
    BAD_INPUT(cx.v, FAAM_MUXER_CONFIG_BASELINE, faam_muxer_open(&cx.v, &ix.v, &m));
    BAD_INPUT(et[0].v, FAAM_TRACK_CONFIG_BASELINE, faam_muxer_open(&cx.v, &ix.v, &m));
    BAD_INPUT(ec[0].v, FAAM_CHAPTER_BASELINE, faam_muxer_open(&cx.v, &ix.v, &m));
    BAD_INPUT(gapx.v, FAAM_GAPLESS_INFO_BASELINE, faam_muxer_open(&cx.v, &ix.v, &m));
    BAD_INPUT(dx.v, FAAM_DEMUXER_CONFIG_BASELINE, faam_demuxer_open(&dx.v, &ix.v, &d));
    BAD_INPUT(ix.v, FAAM_IO_BASELINE, faam_muxer_open(&cx.v, &ix.v, &m));
    m = start(&newer, &cfg);
    BAD_INPUT(ux.v, FAAM_MUXER_UPDATE_PARAMS_BASELINE, faam_muxer_update(m, &ux.v));
    em.v.struct_size = FAAM_METADATA_BASELINE-1; cfg.metadata = &em.v;
    faam_muxer_close(&m); m = start(&newer, &cfg);
    STATUS(faam_muxer_finalize(m), FAAM_ERR_INVALID_ARG); em.v.struct_size = sizeof(em);
    eg[0].v.struct_size = FAAM_CUSTOM_TAG_BASELINE-1;
    STATUS(faam_muxer_finalize(m), FAAM_ERR_INVALID_ARG); faam_muxer_close(&m); eg[0].v.struct_size = sizeof(eg[0]);
    free(plain.buf); free(newer.buf);
}

static void test_track_limits_ids(void) {
    faam_library_info info = { .struct_size = sizeof(info) }; STATUS(faam_get_library_info(&info), FAAM_OK); CHECK(info.max_tracks == 8);
    faam_track_config t[9]; for (unsigned i = 0; i < 9; i++) t[i] = audio();
    faam_muxer_config cfg; STATUS(faam_muxer_config_init(&cfg, sizeof(cfg)), FAAM_OK); cfg.tracks = t;
    memfile f = {0}; faam_io io = mem_io(&f); faam_muxer *m = NULL;
    for (unsigned n = 0; n <= 9; n += 9) {
        cfg.num_tracks = n;
        STATUS(faam_muxer_open(&cfg, &io, &m), FAAM_ERR_INVALID_ARG); CHECK(m == NULL);
    }
    cfg.num_tracks = 8; m = start(&f, &cfg);
    for (unsigned i = 0; i < 8; i++) { uint32_t id; STATUS(faam_muxer_get_track_id(m, i, &id), FAAM_OK); CHECK(id == i+1); }
    finish(&m); t[0].track_id = 42; m = start(&f, &cfg);
    uint32_t id; STATUS(faam_muxer_get_track_id(m, 0, &id), FAAM_OK); CHECK(id == 42); faam_muxer_close(&m);
    t[1].track_id = 42; STATUS(faam_muxer_open(&cfg, &io, &m), FAAM_ERR_INVALID_ARG); free(f.buf);
}

static void test_odd_and_many(void) {
    memfile f = {0}; faam_track_config t[8]; for (unsigned i = 0; i < 8; i++) t[i] = audio();
    faam_muxer_config cfg; STATUS(faam_muxer_config_init(&cfg, sizeof(cfg)), FAAM_OK); cfg.tracks = t; cfg.num_tracks = 1;
    faam_muxer *m = start(&f, &cfg); frames(m, 1); finish(&m);
    long entry = find_tag(&f, "mp4a", 0), handler = find_tag(&f, "soun", 0); CHECK(entry > 0 && handler > 0);
    memcpy(f.buf+entry, "Opus", 4);
    for (unsigned other = 0; other < 2; other++) {
        if (other) memcpy(f.buf+handler, "text", 4);
        faam_demuxer *d = open_mem(&f); faam_track_info ti = { .struct_size = sizeof(ti) };
        STATUS(faam_demuxer_get_track_info(d, 0, &ti), FAAM_OK); CHECK(ti.codec_id == FAAM_CODEC_GENERIC && ti.fourcc == 0x4f707573);
        CHECK(ti.track_type == (other ? FAAM_TRACK_OTHER : FAAM_TRACK_AUDIO));
        faam_frame_loc loc = { .struct_size = sizeof(loc) }; unsigned n = 0;
        while (faam_demuxer_next_frame_loc(d, &loc) == FAAM_OK) { n++; advance(d); }
        CHECK(n == 3); faam_demuxer_close(&d);
    }
    free(f.buf); memset(&f, 0, sizeof(f)); cfg.num_tracks = 8; m = start(&f, &cfg); frames(m, 8); finish(&m);
    long moov = find_tag(&f, "moov", 0)-4, trak = find_tag(&f, "trak", 0)-4;
    uint32_t ts = be32(f.buf+trak), ms = be32(f.buf+moov); CHECK((size_t)moov+ms == f.len);
    /* Replace all eight trak boxes with twelve copies of the first, preserving
     * its sample offsets (mdat precedes moov). Ties in file offset are legal. */
    size_t end = trak; for (unsigned i = 0; i < 8; i++) { CHECK(!memcmp(f.buf+end+4,"trak",4)); end += be32(f.buf+end); }
    memfile g = {0}; mem_write(&g, f.buf, (uint32_t)trak);
    for (unsigned i = 0; i < 12; i++) {
        size_t base = g.pos; mem_write(&g, f.buf+trak, ts);
        long tk = find_tag(&g, "tkhd", (long)base); CHECK(tk > 0);
        put32(g.buf+tk+(g.buf[tk+4] == 1 ? 24 : 16), 21+i);
    }
    mem_write(&g, f.buf+end, (uint32_t)(f.len-end)); put32(g.buf+moov, ms- (uint32_t)(end-trak)+12*ts);
    faam_demuxer *d = open_mem(&g); uint32_t held,total;
    STATUS(faam_demuxer_get_num_tracks(d,&held,&total),FAAM_OK); CHECK(held == 8 && total == 12);
    STATUS(faam_demuxer_get_num_tracks(d,&held,NULL),FAAM_OK);
    faam_track_info ti = { .struct_size = sizeof(ti) }; STATUS(faam_demuxer_get_track_info(d,8,&ti),FAAM_ERR_INVALID_ARG);
    for (unsigned i = 0; i < 8; i++) { STATUS(faam_demuxer_get_track_info(d,i,&ti),FAAM_OK); CHECK(ti.track_id == 21+i); }
    faam_frame_loc loc = { .struct_size = sizeof(loc) }; uint64_t last = 0; unsigned counts[8] = {0};
    while (faam_demuxer_next_frame_loc(d,&loc) == FAAM_OK) { CHECK(loc.file_offset >= last); last=loc.file_offset; CHECK(loc.track_id>=21 && loc.track_id<29); counts[loc.track_id-21]++; advance(d); }
    for (unsigned i=0;i<8;i++) CHECK(counts[i] == 3);
    faam_demuxer_close(&d); free(g.buf); free(f.buf);
}

/* Check the serialized declaration independently of demux signature detection. */
static void check_cover(memfile *f, const faam_metadata *meta,
                        uint8_t cover_type, uint8_t data_type) {
    long covr = find_tag(f, "covr", 0);
    CHECK(covr >= 4 && (size_t)covr + 20 <= f->len);
    CHECK(be32(f->buf + covr - 4) == 24 + meta->cover_bytes);
    CHECK((size_t)covr - 4 + be32(f->buf + covr - 4) <= f->len);
    CHECK(be32(f->buf + covr + 4) == 16 + meta->cover_bytes);
    CHECK(!memcmp(f->buf + covr + 8, "data", 4));
    CHECK(f->buf[covr + 15] == data_type);
    CHECK(be32(f->buf + covr + 12) == data_type);
    CHECK(!memcmp(f->buf + covr + 20, meta->cover_art, meta->cover_bytes));
    faam_demuxer *d = open_mem(f);
    faam_metadata got = { .struct_size = sizeof(got) };
    STATUS(faam_demuxer_get_metadata(d, &got), FAAM_OK);
    CHECK(got.cover_type == cover_type);
    CHECK(got.cover_bytes == meta->cover_bytes);
    CHECK(got.cover_art && !memcmp(got.cover_art, meta->cover_art, meta->cover_bytes));
    faam_demuxer_close(&d);
}

static void test_metadata(void) {
    bool cover_ok = true;
    static const uint8_t png[] = {137,80,78,71,13,10,26,10,0}, jpg[] = {255,216,255,224,0};
    for (unsigned mode=0;mode<4;mode++) {
#ifndef FAAM_MUXER_FRAGMENTED
        if (mode == 3) continue;
#endif
        memfile f={0}; faam_track_config t=audio(); faam_metadata meta={ .struct_size=sizeof(meta) };
        faam_custom_tag tag={ .struct_size=sizeof(tag), .name="default-mean", .value="yes" };
        meta.custom_tags=&tag; meta.num_custom_tags=1;
        meta.cover_art=mode==2?jpg:png; meta.cover_bytes=mode==2?sizeof(jpg):sizeof(png);
        meta.cover_type=mode==1?FAAM_COVER_JPEG:FAAM_COVER_AUTO;
        if (mode==3) meta.title="At init";
        faam_muxer_config cfg; STATUS(faam_muxer_config_init(&cfg,sizeof(cfg)),FAAM_OK);
        cfg.tracks=&t; cfg.num_tracks=1; cfg.metadata=&meta; if(mode==3) cfg.fragment_ms=100;
        faam_muxer *m=start(&f,&cfg); meta.title="After init"; frames(m,1);
        if(mode!=3) { meta.struct_size=0; STATUS(faam_muxer_finalize(m),FAAM_ERR_INVALID_ARG); meta.struct_size=sizeof(meta); }
        finish(&m); faam_demuxer *d=open_mem(&f); faam_metadata got={ .struct_size=sizeof(got) };
        STATUS(faam_demuxer_get_metadata(d,&got),FAAM_OK);
        CHECK(got.title && !strcmp(got.title,mode==3?"At init":"After init"));
        if (got.cover_type != (mode==1||mode==2?FAAM_COVER_JPEG:FAAM_COVER_PNG)) fprintf(stderr, "cover mode %u: observed %u, expected %u\n", mode, got.cover_type, mode==1||mode==2?FAAM_COVER_JPEG:FAAM_COVER_PNG);
        cover_ok &= got.cover_type==(mode==1||mode==2?FAAM_COVER_JPEG:FAAM_COVER_PNG);
        CHECK(got.cover_bytes==meta.cover_bytes && !memcmp(got.cover_art,meta.cover_art,meta.cover_bytes));
        faam_custom_tag gt={ .struct_size=sizeof(gt) }; STATUS(faam_demuxer_get_custom_tag(d,0,&gt),FAAM_OK);
        CHECK(gt.mean && !strcmp(gt.mean,"com.apple.iTunes")); faam_demuxer_close(&d); free(f.buf);
    }
    CHECK(cover_ok);

    static const uint8_t gif87[] = {'G','I','F','8','7','a',0};
    static const uint8_t gif89[] = {'G','I','F','8','9','a',0};
    static const uint8_t bmp[] = {'B','M',0,0,0,0};
    static const uint8_t junk[] = {1,2,3,4,5,6,7,8};
    static const struct {
        const uint8_t *bytes;
        uint32_t size;
        uint8_t requested, declared, data_type;
    } covers[] = {
        {png, sizeof(png), FAAM_COVER_AUTO, FAAM_COVER_PNG, 14},
        {png, sizeof(png), FAAM_COVER_JPEG, FAAM_COVER_JPEG, 13},
        {jpg, sizeof(jpg), FAAM_COVER_AUTO, FAAM_COVER_JPEG, 13},
        {gif87, sizeof(gif87), FAAM_COVER_AUTO, FAAM_COVER_GIF, 12},
        {gif89, sizeof(gif89), FAAM_COVER_AUTO, FAAM_COVER_GIF, 12},
        {bmp, sizeof(bmp), FAAM_COVER_BMP, FAAM_COVER_BMP, 27},
        {bmp, sizeof(bmp), FAAM_COVER_AUTO, FAAM_COVER_JPEG, 13},
        {gif89, sizeof(gif89), FAAM_COVER_PNG, FAAM_COVER_PNG, 14},
        {junk, sizeof(junk), FAAM_COVER_AUTO, FAAM_COVER_JPEG, 13},
        {gif87, sizeof(gif87), FAAM_COVER_GIF, FAAM_COVER_GIF, 12},
    };
    for (unsigned i = 0; i < sizeof(covers)/sizeof(covers[0]); i++) {
        memfile f = {0}; faam_track_config t = audio();
        faam_metadata meta = { .struct_size = sizeof(meta),
            .cover_art = covers[i].bytes, .cover_bytes = covers[i].size,
            .cover_type = covers[i].requested };
        faam_muxer_config cfg;
        STATUS(faam_muxer_config_init(&cfg, sizeof(cfg)), FAAM_OK);
        cfg.tracks = &t; cfg.num_tracks = 1; cfg.metadata = &meta;
        faam_muxer *m = start(&f, &cfg); frames(m, 1); finish(&m);
        check_cover(&f, &meta, covers[i].declared, covers[i].data_type);

        /* Replace existing artwork through the retrofit writer. */
        meta.cover_art = gif89; meta.cover_bytes = sizeof(gif89);
        meta.cover_type = FAAM_COVER_AUTO;
        f.pos = 0; faam_io io = mem_io(&f);
        STATUS(faam_update_tags_stream(&io, &meta, 0), FAAM_OK);
        check_cover(&f, &meta, FAAM_COVER_GIF, 12);
        free(f.buf);
    }

    memfile f = {0}; faam_track_config t = audio();
    faam_metadata meta = { .struct_size = sizeof(meta),
        .cover_art = gif89, .cover_bytes = sizeof(gif89), .cover_type = 99 };
    faam_muxer_config cfg;
    STATUS(faam_muxer_config_init(&cfg, sizeof(cfg)), FAAM_OK);
    cfg.tracks = &t; cfg.num_tracks = 1; cfg.metadata = &meta;
    faam_muxer *m = start(&f, &cfg); frames(m, 1);
    /* mux.c preserves the ilst writer error through finalize and close. */
    STATUS(faam_muxer_finalize(m), FAAM_ERR_INVALID_ARG);
    STATUS(faam_muxer_close(&m), FAAM_ERR_INVALID_ARG);
    CHECK(m == NULL); free(f.buf);
}

static void test_updates_close_flags(void) {
    memfile f={0}; faam_track_config t=audio(); faam_muxer_config cfg;
    STATUS(faam_muxer_config_init(&cfg,sizeof(cfg)),FAAM_OK); cfg.tracks=&t; cfg.num_tracks=1;
    faam_muxer *m=start(&f,&cfg);
    uint8_t data[FAAM_CODEC_DATA_MAX]; for(unsigned i=0;i<sizeof(data);i++) data[i]=(uint8_t)i; data[0]=0x12; data[1]=0x10;
    faam_gapless_info gap={ sizeof(gap), 10, 20, 0, 3000 };
    faam_muxer_update_params p={ .struct_size=sizeof(p), .track_id=1, .creation_time=1700000000,
        .codec_data=data, .codec_data_len=sizeof(data), .language="deu", .audio_sample_size=24, .gapless=&gap };
    const uint32_t flags[]={FAAM_UPDATE_CREATION_TIME,FAAM_UPDATE_GAPLESS,FAAM_UPDATE_CODEC_DATA,FAAM_UPDATE_LANGUAGE,FAAM_UPDATE_AUDIO_SAMPLE_SIZE};
    for(unsigned i=0;i<5;i++) { p.flags=flags[i]; STATUS(faam_muxer_update(m,&p),FAAM_OK); }
    p.flags=0x80000000; STATUS(faam_muxer_update(m,&p),FAAM_ERR_INVALID_ARG);
    p.flags=FAAM_UPDATE_LANGUAGE; p.track_id=99; STATUS(faam_muxer_update(m,&p),FAAM_ERR_NO_TRACK); p.track_id=1;
    p.flags=FAAM_UPDATE_CREATION_TIME|FAAM_UPDATE_CODEC_DATA; p.creation_time=123; p.codec_data_len=FAAM_CODEC_DATA_MAX+1;
    STATUS(faam_muxer_update(m,&p),FAAM_ERR_INVALID_ARG);
    uint8_t frame[8]={0}; STATUS(faam_muxer_write_frame(m,1,frame,8,1024,0,2),FAAM_ERR_INVALID_ARG); frames(m,1);
    STATUS(faam_muxer_finalize(m),FAAM_OK); p.flags=FAAM_UPDATE_LANGUAGE; STATUS(faam_muxer_update(m,&p),FAAM_ERR_INVALID_ARG); STATUS(faam_muxer_close(&m),FAAM_OK);
    long mv=find_tag(&f,"mvhd",0), mp=find_tag(&f,"mp4a",0); CHECK(mv>0 && mp>0);
    CHECK(be32(f.buf+mv+8)==1700000000U+2082844800U); /* rejected update did not apply creation time */
    CHECK(f.buf[mp+22]==0 && f.buf[mp+23]==24);
    faam_demuxer *d=open_mem(&f); uint8_t got[FAAM_CODEC_DATA_MAX]; uint32_t len;
    STATUS(faam_demuxer_get_codec_data(d,1,got,sizeof(got),&len),FAAM_OK); CHECK(len==sizeof(data) && !memcmp(got,data,len));
    faam_track_info ti={ .struct_size=sizeof(ti) }; STATUS(faam_demuxer_get_track_info(d,0,&ti),FAAM_OK); CHECK(!strcmp(ti.language,"deu"));
    faam_gapless_info g={ .struct_size=sizeof(g) }; STATUS(faam_demuxer_get_track_gapless(d,1,&g),FAAM_OK); CHECK(g.encoder_delay==10 && g.end_padding==20 && g.total_samples==3000);
    faam_demuxer_close(&d); faam_io io=mem_io(&f); faam_metadata meta={ .struct_size=sizeof(meta) }; faam_chapter ch={ .struct_size=sizeof(ch), .title="One" };
    STATUS(faam_update_tags_stream(&io,&meta,2),FAAM_ERR_INVALID_ARG); STATUS(faam_update_chapters_stream(&io,&ch,1,1),FAAM_ERR_INVALID_ARG);
    free(f.buf); memset(&f,0,sizeof(f)); m=start(&f,&cfg); f.fail_write_after=f.len+4;
    STATUS(faam_muxer_write_frame(m,1,frame,8,1024,0,0),FAAM_ERR_IO_WRITE);
    STATUS(faam_muxer_finalize(m),FAAM_ERR_IO_WRITE); STATUS(faam_muxer_close(&m),FAAM_ERR_IO_WRITE); CHECK(!m);
    STATUS(faam_muxer_close(&m),FAAM_OK); STATUS(faam_muxer_close(NULL),FAAM_OK); free(f.buf);
    memset(&f,0,sizeof(f)); m=start(&f,&cfg); frames(m,1); f.fail_write_after=f.len+4;
    STATUS(faam_muxer_finalize(m),FAAM_ERR_IO_WRITE); STATUS(faam_muxer_close(&m),FAAM_ERR_IO_WRITE); free(f.buf);
#ifdef FAAM_MUXER_FRAGMENTED
    memset(&f,0,sizeof(f)); cfg.fragment_ms=100; m=start(&f,&cfg); STATUS(faam_muxer_update(m,&p),FAAM_ERR_UNSUPPORTED); finish(&m); free(f.buf);
#endif
}

static void test_video(void) {
    static const uint8_t avcc[]={1,100,0,31,255,225,0,2,103,100,1,0,1,104};
    faam_track_config t={ .struct_size=sizeof(t), .track_type=FAAM_TRACK_VIDEO, .codec_id=FAAM_CODEC_H264,
        .timescale=90000, .width=320, .height=240, .codec_data=avcc, .codec_data_len=sizeof(avcc) };
    faam_muxer_config cfg; STATUS(faam_muxer_config_init(&cfg,sizeof(cfg)),FAAM_OK); cfg.tracks=&t; cfg.num_tracks=1;
#ifndef FAAM_MUXER_VIDEO
    memfile f={0}; faam_io io=mem_io(&f); faam_muxer *m=NULL;
    STATUS(faam_muxer_open(&cfg,&io,&m),FAAM_ERR_NOT_BUILT); free(f.buf);
#else
    bool inband_ok = true;
    static const uint8_t hvcc[23]={1,1,0,0,0,0,0,0,0,0,0,0,120,240,0,252,253,248,248,0,0,15,0};
    for(unsigned codec=0;codec<2;codec++) for(unsigned inband=0;inband<2;inband++) for(unsigned rot=0;rot<4;rot++) {
        memfile f={0}; t.codec_id=codec?FAAM_CODEC_H265:FAAM_CODEC_H264; t.codec_data=codec?hvcc:avcc; t.codec_data_len=codec?sizeof(hvcc):sizeof(avcc);
        t.flags=inband?FAAM_TRACK_INBAND_PARAMS:0; t.rotation_degrees=rot*90;
        faam_muxer *m=start(&f,&cfg); uint8_t nal[]={0,0,0,2,0x65,0x88};
        STATUS(faam_muxer_write_frame(m,1,nal,sizeof(nal),3000,0,FAAM_FRAME_KEYFRAME),FAAM_OK);
        STATUS(faam_muxer_write_frame(m,1,nal,sizeof(nal),3000,0,0),FAAM_OK); finish(&m);
        if(rot==1) { long tk=find_tag(&f,"tkhd",0); CHECK(tk>0); const uint8_t *matrix=f.buf+tk+44;
            CHECK(be32(matrix)==0 && be32(matrix+4)==0x10000 && be32(matrix+12)==0xffff0000 && be32(matrix+16)==0); }
        faam_demuxer *d=open_mem(&f); faam_track_info ti={ .struct_size=sizeof(ti) };
        STATUS(faam_demuxer_get_track_info(d,0,&ti),FAAM_OK); CHECK(ti.rotation_degrees==rot*90 && ti.codec_id==t.codec_id);
        CHECK(ti.fourcc==(codec?(inband?0x68657631:0x68766331):(inband?0x61766333:0x61766331)));
        if (ti.flags != (inband?FAAM_TRACK_INFO_INBAND_PARAMS:0)) fprintf(stderr, "codec %u inband %u rotation %u: flags observed %u, expected %u\n", codec, inband, rot*90, ti.flags, inband?FAAM_TRACK_INFO_INBAND_PARAMS:0);
        inband_ok &= ti.flags==(inband?FAAM_TRACK_INFO_INBAND_PARAMS:0);
        faam_frame_loc loc={ .struct_size=sizeof(loc) }; STATUS(faam_demuxer_next_frame_loc(d,&loc),FAAM_OK); CHECK(loc.is_keyframe); advance(d);
        STATUS(faam_demuxer_next_frame_loc(d,&loc),FAAM_OK); CHECK(!loc.is_keyframe); faam_demuxer_close(&d); free(f.buf);
    }
    for(unsigned rot=45;rot<=360;rot+=315) { memfile f={0}; faam_io io=mem_io(&f); faam_muxer *m=NULL; t.rotation_degrees=rot;
        STATUS(faam_muxer_open(&cfg,&io,&m),FAAM_ERR_INVALID_ARG); free(f.buf); }
    CHECK(inband_ok);
#endif
}

int main(int argc, char **argv) {
    /* Optional case name lets a failing contract be diagnosed independently. */
#define RUN(name) do { if (argc == 1 || !strcmp(argv[1], #name)) test_##name(); } while (0)
    RUN(outputs); RUN(future_inputs); RUN(track_limits_ids); RUN(odd_and_many);
    RUN(metadata); RUN(updates_close_flags); RUN(video);
    puts("libfaam ABI tests passed."); return 0;
}
