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
 * API contract tests: status codes, short reads, derived gapless totals, udta
 * creation, chapter limits, creation times and the language rule.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libfaam_internal.h"
#include "../tests/faam_test_helpers.h"

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

typedef struct {
    uint8_t *buf;
    size_t len, cap, pos;
    uint32_t max_read;  /* a trickling source: at most this many bytes per call */
    bool fail_read;     /* the read callback returns -1 */
    int fail_read_after;/* >0: fail once this many read calls happened */
    int reads;
    int seeks;          /* seek calls so far */
    int fail_seek_at;   /* >0: the seek call with this number fails */
} memfile;

static int32_t mem_read(void *user, void *out, uint32_t n) {
    memfile *f = (memfile *)user;
    f->reads++;
    if (f->fail_read || (f->fail_read_after && f->reads > f->fail_read_after)) return -1;
    size_t avail = f->pos < f->len ? f->len - f->pos : 0;
    if (n > avail) n = (uint32_t)avail;
    if (f->max_read && n > f->max_read) n = f->max_read;
    memcpy(out, f->buf + f->pos, n);
    f->pos += n;
    return (int32_t)n;
}

static int32_t mem_write(void *user, const void *data, uint32_t n) {
    memfile *f = (memfile *)user;
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
    if (f->fail_seek_at && ++f->seeks == f->fail_seek_at) return false;
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
static uint64_t be64(const uint8_t *b) { return ((uint64_t)be32(b) << 32) | be32(b + 4); }
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

typedef struct {
    uint32_t frames;
    uint32_t ticks;           /* per frame */
    faam_gapless_info gapless;
    uint32_t creation_time;
    const char *language;     /* NULL: leave the config default */
    uint32_t num_chapters;
    const faam_chapter *chapters;
    bool set_gapless_late;    /* use FAAM_UPDATE_GAPLESS instead of the config */
} mux_opts;

static void frame_bytes(uint32_t i, uint8_t *out, uint32_t *len) {
    *len = 10 + i % 5;
    for (uint32_t k = 0; k < *len; k++) out[k] = (uint8_t)(i * 7 + k);
}

static void mux_file(memfile *out, const mux_opts *o) {
    faam_muxer_config cfg;
    faam_track_config cfg_tracks[8];
    CHECK(faam_muxer_config_init(&cfg, sizeof(cfg)) == FAAM_OK);
    cfg.creation_time = o->creation_time;
    cfg.chapters = o->chapters;
    cfg.num_chapters = o->num_chapters;
    faam_gapless_info gapless = o->gapless;
    gapless.struct_size = sizeof(gapless);
    if (!o->set_gapless_late) cfg.gapless = &gapless;
    faam_track_config tc = { .struct_size = sizeof(tc) };
    tc.track_type = FAAM_TRACK_AUDIO; tc.codec_id = FAAM_CODEC_AAC; tc.track_id = 1;
    tc.timescale = tc.sample_rate = 44100; tc.channels = 2;
    tc.codec_data = asc_lc_44k; tc.codec_data_len = ASC_LEN;
    if (o->language) strncpy(tc.language, o->language, 4);
    CHECK(test_append_track(&cfg, cfg_tracks, &tc, NULL) == FAAM_OK);
    faam_io io = mem_io(out);
    faam_muxer *m = NULL;
    CHECK(faam_muxer_open(&cfg, &io, &m) == FAAM_OK);
    if (o->set_gapless_late) CHECK(faam_muxer_update(m, &(faam_muxer_update_params){ .struct_size = sizeof(faam_muxer_update_params), .flags = FAAM_UPDATE_GAPLESS, .gapless = &gapless }) == FAAM_OK);
    for (uint32_t i = 0; i < o->frames; i++) {
        uint8_t f[16]; uint32_t n;
        frame_bytes(i, f, &n);
        CHECK(faam_muxer_write_frame(m, 1, f, n, o->ticks, 0, FAAM_FRAME_KEYFRAME) == FAAM_OK);
    }
    CHECK(faam_muxer_finalize(m) == FAAM_OK);
    faam_muxer_close(&m);
}

static void check_frames(memfile *f, uint32_t frames, uint32_t max_read) {
    memfile r = *f;
    r.pos = 0; r.max_read = max_read; r.reads = 0; r.fail_read = false; r.fail_read_after = 0;
    faam_io io = mem_io(&r);
    faam_demuxer *d = NULL;
    CHECK(faam_demuxer_open(NULL, &io, &d) == FAAM_OK);
    for (uint32_t i = 0; i < frames; i++) {
        uint8_t exp[16], got[16]; uint32_t n, gn = 0;
        frame_bytes(i, exp, &n);
        CHECK(faam_demuxer_read_frame(d, got, sizeof(got), &gn) == FAAM_OK);
        CHECK(gn == n && !memcmp(got, exp, n));
    }
    CHECK(faam_demuxer_read_frame(d, NULL, 0, &(uint32_t){0}) == FAAM_END_OF_STREAM);
    faam_demuxer_close(&d);
}

static faam_demuxer *open_mem(memfile *f, uint32_t max_read) {
    f->pos = 0; f->max_read = max_read;
    faam_io io = mem_io(f);
    faam_demuxer *d = NULL;
    CHECK(faam_demuxer_open(NULL, &io, &d) == FAAM_OK);
    return d;
}

/* ---- 1. status contract ---- */

static void test_status_contract(void) {
    memfile f = {0};
    faam_io full = mem_io(&f);
    faam_muxer_config cfg;
    faam_track_config cfg_tracks[8];
    CHECK(faam_muxer_config_init(&cfg, sizeof(cfg)) == FAAM_OK);
    uint32_t empty_size;
    CHECK(faam_muxer_get_state_size(&cfg, &empty_size) == FAAM_ERR_INVALID_ARG);
    CHECK(FAAM_END_OF_STREAM > 0);
    faam_track_config tc = { .struct_size = sizeof(tc) };
    tc.track_type = FAAM_TRACK_AUDIO; tc.codec_id = FAAM_CODEC_AAC; tc.timescale = tc.sample_rate = 44100; tc.channels = 2;
    CHECK(test_append_track(&cfg, cfg_tracks, &tc, NULL) == FAAM_OK);
    uint32_t need;
    CHECK(faam_muxer_get_state_size(&cfg, &need) == FAAM_OK);
    void *mem = malloc(need);
    faam_muxer *m = (faam_muxer *)0x1;

    CHECK(faam_muxer_init(mem, need, &cfg, &full, NULL) == FAAM_ERR_INVALID_ARG);
    CHECK(faam_muxer_init(mem, need, NULL, &full, &m) == FAAM_ERR_INVALID_ARG && !m);
    m = (faam_muxer *)0x1;
    CHECK(faam_muxer_init(mem, need, &cfg, NULL, &m) == FAAM_ERR_INVALID_ARG && !m);
    m = (faam_muxer *)0x1;
    CHECK(faam_muxer_init(NULL, need, &cfg, &full, &m) == FAAM_ERR_INVALID_ARG && !m);
    m = (faam_muxer *)0x1;
    CHECK(faam_muxer_init(mem, need - 1, &cfg, &full, &m) == FAAM_ERR_INSUFFICIENT_MEM && !m);

    /* Missing callbacks are UNSUPPORTED, and fail before the file gets a single byte. */
    faam_io no_write = full, no_seek = full, no_tell = full;
    no_write.write = NULL; no_seek.seek = NULL; no_tell.tell = NULL;
    const faam_io *bad[3] = { &no_write, &no_seek, &no_tell };
    for (int i = 0; i < 3; i++) {
        m = (faam_muxer *)0x1;
        CHECK(faam_muxer_init(mem, need, &cfg, bad[i], &m) == FAAM_ERR_UNSUPPORTED && !m);
        m = (faam_muxer *)0x1;
        CHECK(faam_muxer_open(&cfg, bad[i], &m) == FAAM_ERR_UNSUPPORTED && !m);
    }
    CHECK(f.len == 0);
    m = (faam_muxer *)0x1;
    CHECK(faam_muxer_open(NULL, &full, &m) == FAAM_ERR_INVALID_ARG && !m);

    /* read is not needed by the muxer. */
    faam_io write_only = full; write_only.read = NULL;
    CHECK(faam_muxer_open(&cfg, &write_only, &m) == FAAM_OK);
    faam_muxer_close(&m);

    /* A NULL chapter title is rejected up front, not dereferenced at finalize. */
    faam_chapter nulltitle[1] = { { sizeof(faam_chapter), 0, 0, NULL } };
    faam_muxer_config bad_cfg = cfg;
    bad_cfg.chapters = nulltitle; bad_cfg.num_chapters = 1;
    CHECK(faam_muxer_get_state_size(&bad_cfg, &need) == FAAM_OK);
    m = (faam_muxer *)0x1;
    CHECK(faam_muxer_open(&bad_cfg, &full, &m) == FAAM_ERR_INVALID_ARG && !m);

#ifndef FAAM_MUXER_FRAGMENTED
    faam_muxer_config frag = cfg; frag.fragment_ms = 1000;
    CHECK(faam_muxer_get_state_size(&frag, &need) == FAAM_ERR_NOT_BUILT);
    m = (faam_muxer *)0x1;
    CHECK(faam_muxer_init(mem, 1 << 20, &frag, &full, &m) == FAAM_ERR_NOT_BUILT && !m);
    m = (faam_muxer *)0x1;
    CHECK(faam_muxer_open(&frag, &full, &m) == FAAM_ERR_NOT_BUILT && !m);
#endif
    free(mem);

    /* Demuxer. */
    uint32_t dneed;
    CHECK(faam_demuxer_get_state_size(NULL, &dneed) == FAAM_OK);
    void *dmem = malloc(dneed);
    faam_demuxer *d = (faam_demuxer *)0x1;
    CHECK(faam_demuxer_init(dmem, dneed, NULL, &full, NULL) == FAAM_ERR_INVALID_ARG);
    CHECK(faam_demuxer_init(NULL, dneed, NULL, &full, &d) == FAAM_ERR_INVALID_ARG && !d);
    d = (faam_demuxer *)0x1;
    CHECK(faam_demuxer_init(dmem, dneed, NULL, NULL, &d) == FAAM_ERR_INVALID_ARG && !d);
    d = (faam_demuxer *)0x1;
    CHECK(faam_demuxer_init(dmem, dneed - 1, NULL, &full, &d) == FAAM_ERR_INSUFFICIENT_MEM && !d);
    faam_io no_read = full, no_dseek = full;
    no_read.read = NULL; no_dseek.seek = NULL;
    d = (faam_demuxer *)0x1;
    CHECK(faam_demuxer_init(dmem, dneed, NULL, &no_read, &d) == FAAM_ERR_UNSUPPORTED && !d);
    d = (faam_demuxer *)0x1;
    CHECK(faam_demuxer_init(dmem, dneed, NULL, &no_dseek, &d) == FAAM_ERR_UNSUPPORTED && !d);
    d = (faam_demuxer *)0x1;
    CHECK(faam_demuxer_open(NULL, &no_read, &d) == FAAM_ERR_UNSUPPORTED && !d);
    d = (faam_demuxer *)0x1;
    CHECK(faam_demuxer_open(NULL, NULL, &d) == FAAM_ERR_INVALID_ARG && !d);
    /* An input with no moov still opens, with zero tracks. */
    memfile junk = {0};
    mem_write(&junk, "\0\0\0\x10" "free" "\0\0\0\0\0\0\0\0", 16);
    d = open_mem(&junk, 0);
    uint32_t nt = 9;
    CHECK(faam_demuxer_get_num_tracks(d, &nt, NULL) == FAAM_OK && nt == 0);
    faam_demuxer_close(&d);
    free(junk.buf);
    free(dmem);

    /* In-place updates. */
    faam_metadata meta = { .struct_size = sizeof(meta) }; meta.title = "t";
    faam_chapter ch[1] = { { sizeof(faam_chapter), 0, 0, "a" } };
    CHECK(faam_update_tags_stream(NULL, &meta, 0) == FAAM_ERR_INVALID_ARG);
    CHECK(faam_update_chapters_stream(NULL, ch, 1, 0) == FAAM_ERR_INVALID_ARG);
    CHECK(faam_update_chapters_stream(&full, ch, 256, 0) == FAAM_ERR_INVALID_ARG);
    CHECK(faam_update_chapters_stream(&full, nulltitle, 1, 0) == FAAM_ERR_INVALID_ARG);
    faam_io variants[4] = { full, full, full, full };
    variants[0].read = NULL; variants[1].write = NULL; variants[2].seek = NULL; variants[3].tell = NULL;
    for (int i = 0; i < 4; i++) {
        CHECK(faam_update_tags_stream(&variants[i], &meta, 0) == FAAM_ERR_UNSUPPORTED);
        CHECK(faam_update_chapters_stream(&variants[i], ch, 1, 0) == FAAM_ERR_UNSUPPORTED);
    }
    free(f.buf);
}

/* ---- 3. short and failing reads ---- */

static void test_short_reads(void) {
    memfile f = {0};
    faam_chapter ch[2] = { { sizeof(faam_chapter), 0, 0, "One" }, { sizeof(faam_chapter), 0, 5000, "Two" } };
    mux_opts o = { .frames = 40, .ticks = 1024, .num_chapters = 2, .chapters = ch };
    mux_file(&f, &o);

    for (uint32_t step = 1; step <= 7; step += 2) {
        check_frames(&f, 40, step);
        faam_demuxer *d = open_mem(&f, step);
        uint32_t n = 0;
        faam_chapter got[4];
        for (uint32_t i = 0; i < 4; i++) got[i].struct_size = sizeof(got[i]);
        CHECK(test_read_chapters(d, got, 4, &n) == FAAM_OK && n == 2);
        CHECK(!strcmp(got[1].title, "Two") && got[1].start_ms == 5000);
        faam_demuxer_close(&d);
    }

    /* The in-place rewriters retry short reads too. */
    memfile u = f; u.buf = (uint8_t *)malloc(f.len); memcpy(u.buf, f.buf, f.len); u.cap = f.len;
    u.max_read = 3; u.pos = 0;
    faam_io io = mem_io(&u);
    faam_metadata meta = { .struct_size = sizeof(meta) }; meta.title = "Trickled title that is rather longer than any earlier one"; meta.artist = "A";
    CHECK(faam_update_tags_stream(&io, &meta, 0) == FAAM_OK);
    faam_chapter more[3] = { { sizeof(faam_chapter), 0, 0, "x" }, { sizeof(faam_chapter), 0, 1, "y" }, { sizeof(faam_chapter), 0, 2, "zz" } };
    u.pos = 0;
    CHECK(faam_update_chapters_stream(&io, more, 3, 0) == FAAM_OK);
    check_frames(&u, 40, 5);
    faam_demuxer *d = open_mem(&u, 5);
    faam_metadata got_meta; got_meta.struct_size = sizeof(got_meta); uint32_t n = 0;
    CHECK(faam_demuxer_get_metadata(d, &got_meta) == FAAM_OK && !strcmp(got_meta.artist, "A"));
    CHECK(test_read_chapters(d, NULL, 0, &n) == FAAM_OK && n == 3);
    faam_demuxer_close(&d);
    free(u.buf);

    /* A failing read callback is an error, not an empty file. */
    memfile bad = f; bad.pos = 0; bad.fail_read = true;
    faam_io bio = mem_io(&bad);
    faam_demuxer *bd = (faam_demuxer *)0x1;
    CHECK(faam_demuxer_open(NULL, &bio, &bd) == FAAM_ERR_IO_READ && !bd);
    memfile later = f; later.pos = 0; later.fail_read_after = 4;
    faam_io lio = mem_io(&later);
    CHECK(faam_demuxer_open(NULL, &lio, &bd) == FAAM_ERR_IO_READ && !bd);

    /* An in-place update that cannot read the file leaves it untouched. */
    memfile ro = f; ro.buf = (uint8_t *)malloc(f.len); memcpy(ro.buf, f.buf, f.len); ro.cap = f.len;
    for (int after = 0; after < 6; after++) {
        ro.pos = 0; ro.reads = 0; ro.max_read = 0; ro.fail_read = false; ro.fail_read_after = after + 1;
        faam_io rio = mem_io(&ro);
        faam_metadata rm = { .struct_size = sizeof(rm) }; rm.title = "never written";
        CHECK(faam_update_tags_stream(&rio, &rm, 0) == FAAM_ERR_IO_READ);
        ro.pos = 0; ro.reads = 0;
        CHECK(faam_update_chapters_stream(&rio, ch, 2, 0) == FAAM_ERR_IO_READ);
        CHECK(ro.len == f.len && !memcmp(ro.buf, f.buf, f.len));
    }
    free(ro.buf);

    /* ...and so is one that fails while a frame is read. */
    memfile fr = f; fr.pos = 0;
    faam_io frio = mem_io(&fr);
    faam_demuxer *fd = NULL;
    CHECK(faam_demuxer_open(NULL, &frio, &fd) == FAAM_OK);
    fr.fail_read = true;
    uint8_t buf[16]; uint32_t len;
    CHECK(faam_demuxer_read_frame(fd, buf, sizeof(buf), &len) == FAAM_ERR_IO_READ);
    faam_demuxer_close(&fd);
    free(f.buf);
}

/* ---- 4. gapless totals ---- */

static bool has_elst(const memfile *f) { return find_tag(f, "elst", 0) >= 0; }

static void test_gapless(void) {
    /* The CLI path: delay and padding, no total. */
    memfile f = {0};
    mux_opts o = { .frames = 118, .ticks = 1024, .gapless = { sizeof(faam_gapless_info), 1040, 1840, 0, 0 } };
    mux_file(&f, &o);
    uint64_t expect = 118ULL * 1024 - 1040 - 1840;
    long e = find_tag(&f, "elst", 0);
    CHECK(e > 0);
    CHECK(be32(f.buf + e + 8) == 1 && be32(f.buf + e + 12) == expect && be32(f.buf + e + 16) == 1040);
    faam_demuxer *d = open_mem(&f, 0);
    faam_gapless_info g; g.struct_size = sizeof(g);
    CHECK(faam_demuxer_get_track_gapless(d, 0, &g) == FAAM_OK);
    CHECK(g.encoder_delay == 1040 && g.end_padding == 1840 && g.total_samples == expect);
    faam_demuxer_close(&d);
    free(f.buf);

    /* The same through FAAM_UPDATE_GAPLESS. */
    memfile late = {0};
    mux_opts ol = o; ol.set_gapless_late = true;
    mux_file(&late, &ol);
    CHECK(late.len > 0 && find_tag(&late, "elst", 0) > 0);
    e = find_tag(&late, "elst", 0);
    CHECK(be32(late.buf + e + 12) == expect);
    free(late.buf);

    /* The edit list alone (iTunSMPB renamed away) still yields the total. */
    memfile r = {0};
    mux_file(&r, &o);
    long s = find_tag(&r, "iTunSMPB", 0);
    CHECK(s > 0);
    memcpy(r.buf + s, "iTunXXXX", 8);
    d = open_mem(&r, 0);
    CHECK(faam_demuxer_get_track_gapless(d, 0, &g) == FAAM_OK);
    CHECK(g.encoder_delay == 1040 && g.total_samples == expect && g.end_padding == 1840);
    faam_demuxer_close(&d);
    free(r.buf);

    /* An explicit total wins over the derived one. */
    memfile ex = {0};
    mux_opts oe = o; oe.gapless.total_samples = 100000;
    mux_file(&ex, &oe);
    e = find_tag(&ex, "elst", 0);
    CHECK(e > 0 && be32(ex.buf + e + 12) == 100000);
    free(ex.buf);

    /* Delay and padding bigger than the media: an empty programme, written as the empty
     * edit master's writer produces for it. */
    memfile tiny = {0};
    mux_opts ot = { .frames = 1, .ticks = 1024, .gapless = { sizeof(faam_gapless_info), 1000, 1000, 0, 0 } };
    mux_file(&tiny, &ot);
    e = find_tag(&tiny, "elst", 0);
    CHECK(e > 0 && be32(tiny.buf + e + 12) == 0);
    d = open_mem(&tiny, 0);
    CHECK(faam_demuxer_get_track_gapless(d, 0, &g) == FAAM_OK && g.encoder_delay == 1000 && g.total_samples == 0);
    faam_demuxer_close(&d);
    free(tiny.buf);

    /* Saturation must also hold beyond a single frame; a nonempty one-frame
     * programme must retain its exact sample count. */
    for (unsigned i = 0; i < 2; i++) {
        memfile short_file = {0};
        mux_opts os = { .frames = i ? 1 : 2, .ticks = 1024,
                        .gapless = { sizeof(faam_gapless_info), i ? 100 : 2000, i ? 200 : 1000, 0, 0 } };
        uint64_t total = i ? 724 : 0;
        mux_file(&short_file, &os);
        e = find_tag(&short_file, "elst", 0);
        CHECK(e > 0 && be32(short_file.buf + e + 12) == total);
        CHECK(be32(short_file.buf + e + 16) == os.gapless.encoder_delay);
        d = open_mem(&short_file, 0);
        CHECK(faam_demuxer_get_track_gapless(d, 0, &g) == FAAM_OK);
        CHECK(g.encoder_delay == os.gapless.encoder_delay &&
              g.end_padding == os.gapless.end_padding && g.total_samples == total);
        CHECK(faam_demuxer_get_track_gapless(d, 1, &g) == FAAM_OK);
        CHECK(g.encoder_delay == os.gapless.encoder_delay &&
              g.end_padding == os.gapless.end_padding && g.total_samples == total);
        faam_demuxer_close(&d);
        free(short_file.buf);
    }

    /* No frames at all behaves the same. */
    memfile none = {0};
    mux_opts on = { .frames = 0, .ticks = 1024, .gapless = { sizeof(faam_gapless_info), 1024, 0, 0, 0 } };
    mux_file(&none, &on);
    e = find_tag(&none, "elst", 0);
    CHECK(e > 0 && be32(none.buf + e + 12) == 0);
    free(none.buf);

    /* Padding only: no edit list (it needs a delay) but iTunSMPB carries the total. */
    memfile po = {0};
    mux_opts op = { .frames = 10, .ticks = 1024, .gapless = { sizeof(faam_gapless_info), 0, 500, 0, 0 } };
    mux_file(&po, &op);
    CHECK(!has_elst(&po));
    d = open_mem(&po, 0);
    CHECK(faam_demuxer_get_track_gapless(d, 0, &g) == FAAM_OK && g.end_padding == 500 && g.total_samples == 10 * 1024 - 500);
    faam_demuxer_close(&d);
    free(po.buf);
}

/* ---- 5. udta creation ---- */

/* Rebuild [ftyp][wide][mdat][moov] as [ftyp][wide][moov][mdat], shifting stco. */
static void make_faststart(memfile *f) {
    size_t pos = 0, moov_at = 0, moov_len = 0, mdat_at = 0, mdat_len = 0;
    while (pos + 8 <= f->len) {
        uint32_t sz = be32(f->buf + pos);
        if (!memcmp(f->buf + pos + 4, "moov", 4)) { moov_at = pos; moov_len = sz; }
        if (!memcmp(f->buf + pos + 4, "mdat", 4)) { mdat_at = pos; mdat_len = sz; }
        pos += sz;
    }
    CHECK(moov_at > mdat_at && moov_at + moov_len == f->len);
    uint8_t *out = (uint8_t *)malloc(f->len);
    memcpy(out, f->buf, mdat_at);
    memcpy(out + mdat_at, f->buf + moov_at, moov_len);
    memcpy(out + mdat_at + moov_len, f->buf + mdat_at, mdat_len);
    memfile tmp = { out, f->len, f->len, 0, 0, false, 0, 0, 0, 0 };
    for (long i = find_tag(&tmp, "stco", 0); i >= 0; i = find_tag(&tmp, "stco", i + 4)) {
        uint32_t n = be32(out + i + 8);
        for (uint32_t k = 0; k < n; k++) put32(out + i + 12 + 4 * k, be32(out + i + 12 + 4 * k) + (uint32_t)moov_len);
    }
    free(f->buf);
    f->buf = out;
    f->cap = f->len;
}

/* Inside moov the top-level atom `from` is renamed to `to`: same size, so the file
 * stays valid while the original content is hidden. */
static void rename_tag(memfile *f, const char *from, const char *to) {
    long i = find_tag(f, from, 0);
    CHECK(i > 0);
    memcpy(f->buf + i, to, 4);
}

typedef enum { ABSENT_UDTA, ABSENT_META, ABSENT_ILST, ABSENT_CHPL } absent_kind;

static void collect(memfile *f, uint32_t *n_chapters, char titles[4][16], faam_metadata *meta_out, char *title_copy) {
    /* meta_out->title/artist point into the demuxer: the copies below outlive it. */
    faam_demuxer *d = open_mem(f, 0);
    faam_chapter ch[4];
    for (uint32_t i = 0; i < 4; i++) ch[i].struct_size = sizeof(ch[i]);
    uint32_t n = 0;
    CHECK(test_read_chapters(d, ch, 4, &n) == FAAM_OK);
    *n_chapters = n;
    for (uint32_t i = 0; i < n && i < 4; i++) { strncpy(titles[i], ch[i].title, 15); titles[i][15] = 0; }
    meta_out->struct_size = sizeof(*meta_out);
    CHECK(faam_demuxer_get_metadata(d, meta_out) == FAAM_OK);
    title_copy[0] = 0;
    if (meta_out->title) strncpy(title_copy, meta_out->title, 63);
    static char artist_store[64];
    artist_store[0] = 0;
    if (meta_out->artist) strncpy(artist_store, meta_out->artist, 63);
    meta_out->artist = artist_store;
    faam_demuxer_close(&d);
}

static void run_udta_case(bool moov_first, bool tags_first, absent_kind kind) {
    memfile f = {0};
    faam_chapter seed[1] = { { sizeof(faam_chapter), 0, 0, "seed" } };
    faam_metadata seed_meta = { .struct_size = sizeof(seed_meta) }; seed_meta.title = "seed";
    (void)seed_meta;
    mux_opts o = { .frames = 30, .ticks = 1024, .gapless = { sizeof(faam_gapless_info), 1024, 100, 0, 0 }, .num_chapters = 1, .chapters = seed };
    mux_file(&f, &o);
    switch (kind) {
    case ABSENT_UDTA: rename_tag(&f, "udta", "free"); break;
    case ABSENT_META: rename_tag(&f, "meta", "free"); break;
    case ABSENT_ILST: rename_tag(&f, "ilst", "free"); break;
    case ABSENT_CHPL: rename_tag(&f, "chpl", "free"); break;
    }
    if (moov_first) make_faststart(&f);
    check_frames(&f, 30, 0);

    size_t mdat_len = 0, mdat_at = 0;
    for (size_t pos = 0; pos + 8 <= f.len; pos += be32(f.buf + pos))
        if (!memcmp(f.buf + pos + 4, "mdat", 4)) { mdat_at = pos; mdat_len = be32(f.buf + pos); }
    uint8_t *mdat_copy = (uint8_t *)malloc(mdat_len);
    memcpy(mdat_copy, f.buf + mdat_at, mdat_len);

    faam_io io = mem_io(&f);
    faam_metadata meta = { .struct_size = sizeof(meta) };
    meta.title = "Created title"; meta.artist = "Created artist"; meta.track_num = 3; meta.track_total = 9;
    faam_chapter chs[3] = { { sizeof(faam_chapter), 0, 0, "Intro" }, { sizeof(faam_chapter), 0, 61000, "Middle" }, { sizeof(faam_chapter), 0, 120500, "End" } };
    for (int step = 0; step < 2; step++) {
        bool tags = (step == 0) == tags_first;
        f.pos = 0;
        if (tags) CHECK(faam_update_tags_stream(&io, &meta, 0) == FAAM_OK);
        else CHECK(faam_update_chapters_stream(&io, chs, 3, 0) == FAAM_OK);
        check_frames(&f, 30, 0);
    }
    /* Whatever moved, the media bytes did not change. */
    size_t new_mdat_at = 0;
    for (size_t pos = 0; pos + 8 <= f.len; pos += be32(f.buf + pos))
        if (!memcmp(f.buf + pos + 4, "mdat", 4)) new_mdat_at = pos;
    CHECK(!memcmp(f.buf + new_mdat_at, mdat_copy, mdat_len));
    free(mdat_copy);

    uint32_t n; char titles[4][16]; faam_metadata got; got.struct_size = sizeof(got); char title[64];
    collect(&f, &n, titles, &got, title);
    CHECK(n == 3 && !strcmp(titles[0], "Intro") && !strcmp(titles[2], "End"));
    CHECK(!strcmp(title, "Created title") && !strcmp(got.artist, "Created artist"));
    CHECK(got.track_num == 3 && got.track_total == 9);
    /* A second pass on a now complete file replaces in place. */
    faam_chapter one[1] = { { sizeof(faam_chapter), 0, 7, "Only" } };
    f.pos = 0; CHECK(faam_update_chapters_stream(&io, one, 1, 0) == FAAM_OK);
    meta.title = "Second"; f.pos = 0; CHECK(faam_update_tags_stream(&io, &meta, 0) == FAAM_OK);
    collect(&f, &n, titles, &got, title);
    CHECK(n == 1 && !strcmp(titles[0], "Only") && !strcmp(title, "Second"));
    check_frames(&f, 30, 0);
    /* Gapless survives the rewrites. */
    faam_demuxer *d = open_mem(&f, 0);
    faam_gapless_info g; g.struct_size = sizeof(g);
    CHECK(faam_demuxer_get_track_gapless(d, 0, &g) == FAAM_OK && g.encoder_delay == 1024 && g.end_padding == 100);
    faam_demuxer_close(&d);
    free(f.buf);
}

static void test_udta_creation(void) {
    for (int moov_first = 0; moov_first < 2; moov_first++)
        for (int tags_first = 0; tags_first < 2; tags_first++)
            for (int kind = ABSENT_UDTA; kind <= ABSENT_CHPL; kind++)
                run_udta_case(moov_first, tags_first, (absent_kind)kind);
}

/* ---- 6. chapter limits ---- */

static void test_chapter_counts(void) {
    enum { N = 255 };
    static faam_chapter chs[N];
    for (uint32_t i = 0; i < N; i++) chs[i].struct_size = sizeof(chs[i]);
    static char names[N][12];
    for (int i = 0; i < N; i++) { snprintf(names[i], sizeof(names[i]), "Ch %d", i); chs[i].title = names[i]; chs[i].start_ms = (uint64_t)i * 1000; }
    memfile f = {0};
    mux_opts o = { .frames = 5, .ticks = 1024, .num_chapters = N, .chapters = chs };
    mux_file(&f, &o);
    faam_demuxer *d = open_mem(&f, 0);
    uint32_t n = 0;
    faam_chapter got[10];
    for (uint32_t i = 0; i < 10; i++) got[i].struct_size = sizeof(got[i]);
    CHECK(test_read_chapters(d, NULL, 0, &n) == FAAM_OK && n == N);
    n = 0;
    CHECK(test_read_chapters(d, got, 0, &n) == FAAM_OK && n == N);
    n = 0;
    CHECK(test_read_chapters(d, got, 10, &n) == FAAM_OK && n == N);
    CHECK(!strcmp(got[9].title, "Ch 9") && got[9].start_ms == 9000);
    static faam_chapter all[N];
    for (uint32_t i = 0; i < N; i++) all[i].struct_size = sizeof(all[i]);
    n = 0;
    CHECK(test_read_chapters(d, all, N, &n) == FAAM_OK && n == N);
    CHECK(!strcmp(all[N - 1].title, "Ch 254") && all[N - 1].start_ms == 254000);
    faam_demuxer_close(&d);

    /* Same through the in-place rewriter, shrinking and growing. */
    memfile u = {0};
    mux_opts o0 = { .frames = 5, .ticks = 1024 };
    mux_file(&u, &o0);
    faam_io io = mem_io(&u);
    u.pos = 0; CHECK(faam_update_chapters_stream(&io, chs, N, 0) == FAAM_OK);
    d = open_mem(&u, 0);
    CHECK(test_read_chapters(d, NULL, 0, &n) == FAAM_OK && n == N);
    faam_demuxer_close(&d);
    u.pos = 0; CHECK(faam_update_chapters_stream(&io, chs, 2, 0) == FAAM_OK);
    d = open_mem(&u, 0);
    CHECK(test_read_chapters(d, NULL, 0, &n) == FAAM_OK && n == 2);
    faam_demuxer_close(&d);
    u.pos = 0; CHECK(faam_update_chapters_stream(&io, chs, 0, 0) == FAAM_OK);
    d = open_mem(&u, 0);
    CHECK(test_read_chapters(d, NULL, 0, &n) == FAAM_OK && n == 0);
    faam_demuxer_close(&d);
    free(u.buf);
    free(f.buf);
}

/* ---- 6b. per-track gapless across different sample rates ---- */

/* Payload offset of the n-th occurrence of a box type, or -1. */
static long nth_tag(const memfile *f, const char *tag, unsigned n) {
    long pos = 0;
    for (unsigned i = 0; i <= n; i++) {
        pos = find_tag(f, tag, pos);
        if (pos < 0) return -1;
        if (i < n) pos += 4;
    }
    return pos;
}

static void mux_two_rates(memfile *f, bool low_first, const faam_gapless_info *g1, uint32_t n1,
                          const faam_gapless_info *g2, uint32_t n2) {
    static const uint8_t asc_lc_48k[ASC_LEN] = { 0x11, 0x90 };
    faam_muxer_config cfg;
    faam_track_config cfg_tracks[8];
    CHECK(faam_muxer_config_init(&cfg, sizeof(cfg)) == FAAM_OK);
    faam_track_config lo = { .struct_size = sizeof(lo) }, hi = lo;
    lo.track_type = hi.track_type = FAAM_TRACK_AUDIO;
    lo.codec_id = hi.codec_id = FAAM_CODEC_AAC;
    lo.channels = hi.channels = 2;
    lo.track_id = 1; lo.timescale = lo.sample_rate = 44100;
    lo.codec_data = asc_lc_44k; lo.codec_data_len = ASC_LEN; lo.gapless = g1;
    hi.track_id = 2; hi.timescale = hi.sample_rate = 48000;
    hi.codec_data = asc_lc_48k; hi.codec_data_len = ASC_LEN; hi.gapless = g2;
    CHECK(test_append_track(&cfg, cfg_tracks, low_first ? &lo : &hi, NULL) == FAAM_OK);
    CHECK(test_append_track(&cfg, cfg_tracks, low_first ? &hi : &lo, NULL) == FAAM_OK);
    faam_io io = mem_io(f);
    faam_muxer *m = NULL;
    CHECK(faam_muxer_open(&cfg, &io, &m) == FAAM_OK);
    uint8_t frame[16] = {0};
    for (uint32_t i = 0; i < n1 || i < n2; i++) {
        if (i < n1) CHECK(faam_muxer_write_frame(m, 1, frame, 10, 1024, 0, FAAM_FRAME_KEYFRAME) == FAAM_OK);
        if (i < n2) CHECK(faam_muxer_write_frame(m, 2, frame, 10, 1024, 0, FAAM_FRAME_KEYFRAME) == FAAM_OK);
    }
    CHECK(faam_muxer_finalize(m) == FAAM_OK);
    faam_muxer_close(&m);
}

static void test_multitrack_gapless(void) {
    /* The edit length is stored in movie ticks; both tracks' totals have to survive that
     * conversion for arbitrary programme lengths, whichever track is first. */
    for (uint32_t k = 0; k < 400; k++) {
        bool low_first = k < 200;
        uint32_t n1 = 110, n2 = 100 + k % 3;
        uint32_t total2 = 99000 + k % 200;
        faam_gapless_info g1 = { sizeof(faam_gapless_info), 1105, 700, 0, n1 * 1024 - 1105 - 700 };
        faam_gapless_info g2 = { sizeof(faam_gapless_info), 1067, n2 * 1024 - 1067 - total2, 0, total2 };
        memfile f = {0};
        mux_two_rates(&f, low_first, &g1, n1, &g2, n2);

        faam_demuxer *d = open_mem(&f, 0);
        faam_gapless_info r1 = { .struct_size = sizeof(r1) }, r2 = { .struct_size = sizeof(r2) };
        CHECK(faam_demuxer_get_track_gapless(d, 1, &r1) == FAAM_OK);
        CHECK(faam_demuxer_get_track_gapless(d, 2, &r2) == FAAM_OK);
        CHECK(r1.encoder_delay == g1.encoder_delay && r1.end_padding == g1.end_padding && r1.total_samples == g1.total_samples);
        CHECK(r2.encoder_delay == g2.encoder_delay && r2.end_padding == g2.end_padding && r2.total_samples == g2.total_samples);
        faam_demuxer_close(&d);

        /* Each track's own durations are in its own timescale; the movie ticks are the finest rate. */
        long mv = find_tag(&f, "mvhd", 0);
        uint32_t movie_ts = 48000;
        uint32_t d1 = (uint32_t)((uint64_t)n1 * 1024 * movie_ts / 44100), d2 = n2 * 1024;
        CHECK(be32(f.buf + mv + 16) == movie_ts && be32(f.buf + mv + 20) == (d1 > d2 ? d1 : d2));
        uint32_t media[2] = { n1 * 1024, n2 * 1024 }, rate[2] = { 44100, 48000 };
        for (unsigned box = 0; box < 2; box++) {
            unsigned t = low_first ? box : 1 - box;  /* index of the track stored at this position */
            long md = nth_tag(&f, "mdhd", box), tk = nth_tag(&f, "tkhd", box), el = nth_tag(&f, "elst", box);
            CHECK(md > 0 && tk > 0 && el > 0);
            CHECK(be32(f.buf + md + 16) == rate[t] && be32(f.buf + md + 20) == media[t]);
            CHECK(be32(f.buf + tk + 24) == (uint32_t)((uint64_t)media[t] * movie_ts / rate[t]));
            const faam_gapless_info *g = t ? &g2 : &g1;
            CHECK(be32(f.buf + el + 12) == (uint32_t)(((uint64_t)g->total_samples * movie_ts + rate[t] / 2) / rate[t]));
            CHECK(be32(f.buf + el + 16) == g->encoder_delay);
        }
        free(f.buf);
    }
}

/* ---- 7. creation time ---- */

static void test_creation_time(void) {
    /* Fits 32 bits after the epoch shift: version-0 boxes with the plain value. */
    memfile a = {0};
    mux_opts oa = { .frames = 3, .ticks = 1024, .creation_time = 1700000000u };
    mux_file(&a, &oa);
    long mv = find_tag(&a, "mvhd", 0);
    CHECK(mv > 0 && a.buf[mv + 4] == 0 && be32(a.buf + mv + 8) == 1700000000u + 2082844800u);
    free(a.buf);

    /* 2040 and later would wrap: version-1 boxes carry the full value. */
    static const uint32_t big[] = { 2212122496u, 4294967295u };
    for (unsigned i = 0; i < 2; i++) {
        memfile b = {0};
        mux_opts ob = { .frames = 3, .ticks = 1024, .creation_time = big[i] };
        mux_file(&b, &ob);
        uint64_t want = (uint64_t)big[i] + 2082844800u;
        CHECK(want > 0xFFFFFFFFULL);
        mv = find_tag(&b, "mvhd", 0);
        CHECK(mv > 0 && b.buf[mv + 4] == 1 && be64(b.buf + mv + 8) == want && be64(b.buf + mv + 16) == want);
        long tk = find_tag(&b, "tkhd", 0);
        CHECK(tk > 0 && b.buf[tk + 4] == 1 && be64(b.buf + tk + 8) == want);
        long md = find_tag(&b, "mdhd", 0);
        CHECK(md > 0 && b.buf[md + 4] == 1 && be64(b.buf + md + 8) == want);
        faam_demuxer *d = open_mem(&b, 0);
        faam_track_info ti = { .struct_size = sizeof(ti) };
        CHECK(faam_demuxer_get_track_info(d, 0, &ti) == FAAM_OK && ti.timescale == 44100 && ti.total_frames == 3);
        faam_demuxer_close(&d);
        check_frames(&b, 3, 0);
        free(b.buf);
    }

    /* Unset stays zero. */
    memfile z = {0};
    mux_opts oz = { .frames = 3, .ticks = 1024 };
    mux_file(&z, &oz);
    mv = find_tag(&z, "mvhd", 0);
    CHECK(z.buf[mv + 4] == 0 && be32(z.buf + mv + 8) == 0);
    free(z.buf);

    /* Updates are closed once the moov is written. */
    memfile f = {0};
    faam_io io = mem_io(&f);
    faam_muxer_config cfg;
    faam_track_config cfg_tracks[8];
    CHECK(faam_muxer_config_init(&cfg, sizeof(cfg)) == FAAM_OK);
    faam_track_config track = { .struct_size = sizeof(track), .track_type = FAAM_TRACK_AUDIO,
                                .codec_id = FAAM_CODEC_AAC, .timescale = 44100 };
    CHECK(test_append_track(&cfg, cfg_tracks, &track, NULL) == FAAM_OK);
    faam_muxer *m = NULL;
    CHECK(faam_muxer_open(&cfg, &io, &m) == FAAM_OK);
    CHECK(faam_muxer_update(m, &(faam_muxer_update_params){ .struct_size = sizeof(faam_muxer_update_params), .flags = FAAM_UPDATE_CREATION_TIME, .creation_time = 5 }) == FAAM_OK);
    faam_gapless_info g = { sizeof(faam_gapless_info), 1, 2, 0, 3 };
    CHECK(faam_muxer_update(m, &(faam_muxer_update_params){ .struct_size = sizeof(faam_muxer_update_params), .flags = FAAM_UPDATE_GAPLESS, .gapless = &g }) == FAAM_OK);
    CHECK(faam_muxer_update(m, &(faam_muxer_update_params){ .struct_size = sizeof(faam_muxer_update_params), .flags = FAAM_UPDATE_CREATION_TIME, .creation_time = 5 }) == FAAM_OK);
    uint8_t one = 1;
    CHECK(faam_muxer_write_frame(m, 1, &one, 1, 1024, 0, FAAM_FRAME_KEYFRAME) == FAAM_OK);
    CHECK(faam_muxer_finalize(m) == FAAM_OK);
    CHECK(faam_muxer_update(m, &(faam_muxer_update_params){ .struct_size = sizeof(faam_muxer_update_params), .flags = FAAM_UPDATE_CREATION_TIME, .creation_time = 7 }) == FAAM_ERR_INVALID_ARG);
    CHECK(faam_muxer_update(m, &(faam_muxer_update_params){ .struct_size = sizeof(faam_muxer_update_params), .flags = FAAM_UPDATE_GAPLESS, .gapless = &g }) == FAAM_ERR_INVALID_ARG);
    CHECK(faam_muxer_update(m, &(faam_muxer_update_params){ .struct_size = sizeof(faam_muxer_update_params), .flags = FAAM_UPDATE_CODEC_DATA, .track_id = 1, .codec_data = asc_lc_44k, .codec_data_len = ASC_LEN }) == FAAM_ERR_INVALID_ARG);
    CHECK(faam_muxer_update(m, &(faam_muxer_update_params){ .struct_size = sizeof(faam_muxer_update_params), .flags = FAAM_UPDATE_LANGUAGE, .track_id = 1, .language = { ("eng")[0], ("eng")[1], ("eng")[2], 0 } }) == FAAM_ERR_INVALID_ARG);
    faam_muxer_close(&m);
    free(f.buf);
}

/* ---- language rule ---- */

static void check_language(const char *in, const char *packed_as) {
    memfile f = {0};
    mux_opts o = { .frames = 2, .ticks = 1024, .language = in };
    mux_file(&f, &o);
    faam_demuxer *d = open_mem(&f, 0);
    faam_track_info ti = { .struct_size = sizeof(ti) };
    CHECK(faam_demuxer_get_track_info(d, 0, &ti) == FAAM_OK);
    CHECK(!strcmp(ti.language, packed_as));
    faam_demuxer_close(&d);
    free(f.buf);
}

static void test_language(void) {
    check_language("eng", "eng");
    check_language("ENG", "eng");
    check_language("DeU", "deu");
    check_language("engl", "eng");     /* only three characters count */
    check_language("zzz", "zzz");      /* any three letters, registered or not */
    check_language("en", "und");
    check_language("e1g", "und");
    check_language("e g", "und");
    check_language("", "und");
    check_language(NULL, "und");

    /* The late setter follows the same rule. */
    const char *late[][2] = { { "FRA", "fra" }, { "fr", "und" }, { "f4a", "und" }, { NULL, "und" }, { "spanish", "spa" } };
    for (unsigned i = 0; i < sizeof(late) / sizeof(late[0]); i++) {
        memfile f = {0};
        faam_io io = mem_io(&f);
        faam_muxer_config cfg;
        faam_track_config cfg_tracks[8];
        CHECK(faam_muxer_config_init(&cfg, sizeof(cfg)) == FAAM_OK);
        faam_track_config tc = { .struct_size = sizeof(tc) };
        tc.track_type = FAAM_TRACK_AUDIO; tc.codec_id = FAAM_CODEC_AAC; tc.track_id = 1;
        tc.timescale = tc.sample_rate = 44100; tc.channels = 2;
        CHECK(test_append_track(&cfg, cfg_tracks, &tc, NULL) == FAAM_OK);
        faam_muxer *m = NULL;
        CHECK(faam_muxer_open(&cfg, &io, &m) == FAAM_OK);
        faam_muxer_update_params language = { .struct_size = sizeof(language), .flags = FAAM_UPDATE_LANGUAGE, .track_id = 1 };
        if (late[i][0]) strncpy(language.language, late[i][0], sizeof(language.language));
        CHECK(faam_muxer_update(m, &language) == FAAM_OK);
        CHECK(faam_muxer_update(m, &(faam_muxer_update_params){ .struct_size = sizeof(faam_muxer_update_params), .flags = FAAM_UPDATE_CODEC_DATA, .track_id = 1, .codec_data = asc_lc_44k, .codec_data_len = ASC_LEN }) == FAAM_OK);
        uint8_t one = 1;
        CHECK(faam_muxer_write_frame(m, 1, &one, 1, 1024, 0, FAAM_FRAME_KEYFRAME) == FAAM_OK);
        CHECK(faam_muxer_finalize(m) == FAAM_OK);
        faam_muxer_close(&m);
        faam_demuxer *d = open_mem(&f, 0);
        faam_track_info ti = { .struct_size = sizeof(ti) };
        CHECK(faam_demuxer_get_track_info(d, 0, &ti) == FAAM_OK && !strcmp(ti.language, late[i][1]));
        faam_demuxer_close(&d);
        free(f.buf);
    }

    /* An unterminated four-character field in the config is read no further than three. */
    memfile f = {0};
    faam_muxer_config cfg;
    faam_track_config cfg_tracks[8];
    CHECK(faam_muxer_config_init(&cfg, sizeof(cfg)) == FAAM_OK);
    faam_track_config tc = { .struct_size = sizeof(tc) };
    tc.track_type = FAAM_TRACK_AUDIO; tc.codec_id = FAAM_CODEC_AAC; tc.track_id = 1;
    tc.timescale = tc.sample_rate = 44100; tc.channels = 2;
    memcpy(tc.language, "abcd", 4);
    CHECK(test_append_track(&cfg, cfg_tracks, &tc, NULL) == FAAM_OK);
    faam_io io = mem_io(&f);
    faam_muxer *m = NULL;
    CHECK(faam_muxer_open(&cfg, &io, &m) == FAAM_OK);
    faam_muxer_close(&m);
    free(f.buf);
}

/* ---- seek and moov-layout failures ---- */

/* A seek callback that exists but fails is a write failure whichever seek it is:
 * UNSUPPORTED is reserved for a missing callback. Every seek the muxer makes is
 * failed in turn, progressive and fragmented. */
static void test_seek_failures(void) {
    for (int frag = 0; frag < 2; frag++) {
#ifndef FAAM_MUXER_FRAGMENTED
        if (frag) continue;
#endif
        int failures = 0;
        bool completed = false;
        for (int n = 1; n < 200 && !completed; n++) {
            memfile f = {0};
            faam_muxer_config cfg;
            faam_track_config cfg_tracks[8];
            CHECK(faam_muxer_config_init(&cfg, sizeof(cfg)) == FAAM_OK);
            cfg.fragment_ms = frag ? 500 : 0;
            faam_track_config tc = { .struct_size = sizeof(tc) };
            tc.track_type = FAAM_TRACK_AUDIO; tc.codec_id = FAAM_CODEC_AAC; tc.track_id = 1;
            tc.timescale = tc.sample_rate = 44100; tc.channels = 2;
            tc.codec_data = asc_lc_44k; tc.codec_data_len = ASC_LEN;
            CHECK(test_append_track(&cfg, cfg_tracks, &tc, NULL) == FAAM_OK);
            faam_io io = mem_io(&f);
            faam_muxer *m = NULL;
            CHECK(faam_muxer_open(&cfg, &io, &m) == FAAM_OK);
            f.seeks = 0; f.fail_seek_at = n;
            faam_status st = FAAM_OK;
            for (uint32_t i = 0; i < 80 && st == FAAM_OK; i++) {
                uint8_t fb[16]; uint32_t len;
                frame_bytes(i, fb, &len);
                st = faam_muxer_write_frame(m, 1, fb, len, 1024, 0, FAAM_FRAME_KEYFRAME);
            }
            if (st == FAAM_OK) st = faam_muxer_finalize(m);
            if (st == FAAM_OK) completed = true;
            else { CHECK(st == FAAM_ERR_IO_WRITE); failures++; }
            if (st != FAAM_OK) CHECK(faam_muxer_finalize(m) == FAAM_ERR_IO_WRITE); /* sticky */
            faam_muxer_close(&m);
            free(f.buf);
        }
        CHECK(completed && failures > 0);
    }
}

/* A moov with a 64-bit size header cannot have its size field grown in place:
 * both rewriters must refuse and leave the file as it was. */
static void test_extended_moov(void) {
    memfile f = {0};
    mux_opts o = { .frames = 8, .ticks = 1024 };
    mux_file(&f, &o);
    long m = find_tag(&f, "moov", 0);
    CHECK(m >= 4);
    size_t at = (size_t)m - 4;
    uint32_t msize = be32(f.buf + at);
    CHECK(at + msize == f.len);

    memfile x = {0};
    mem_write(&x, f.buf, at);
    uint8_t hdr[16];
    put32(hdr, 1); memcpy(hdr + 4, "moov", 4); put32(hdr + 8, 0); put32(hdr + 12, msize + 8);
    mem_write(&x, hdr, 16);
    mem_write(&x, f.buf + at + 8, msize - 8);
    memfile ref = x;
    ref.buf = (uint8_t *)malloc(x.len);
    CHECK(ref.buf);
    memcpy(ref.buf, x.buf, x.len);

    faam_demuxer *d = open_mem(&x, 0);
    uint32_t nt = 0;
    CHECK(faam_demuxer_get_num_tracks(d, &nt, NULL) == FAAM_OK && nt == 1);
    faam_demuxer_close(&d);

    faam_io io = mem_io(&x);
    faam_metadata meta = { .struct_size = sizeof(meta) }; meta.title = "A title that makes the tag list grow";
    faam_chapter ch[1] = { { sizeof(faam_chapter), 0, 0, "Chapter" } };
    x.pos = 0;
    faam_status st = faam_update_tags_stream(&io, &meta, 0);
    CHECK(st == FAAM_ERR_UNSUPPORTED || st == FAAM_ERR_BAD_CONTAINER);
    CHECK(x.len == ref.len && !memcmp(x.buf, ref.buf, x.len));
    x.pos = 0;
    st = faam_update_chapters_stream(&io, ch, 1, 0);
    CHECK(st == FAAM_ERR_UNSUPPORTED || st == FAAM_ERR_BAD_CONTAINER);
    CHECK(x.len == ref.len && !memcmp(x.buf, ref.buf, x.len));
    free(ref.buf);
    free(x.buf);
    free(f.buf);
}

/* ---- 8. preservation of unmodeled atoms, custom tag namespace, and mdta ---- */

/* Chapter edits must preserve opaque metadata that the demuxer cannot model. */
static void test_chapters_preserve_ilst(memfile *f) {
    long pos = find_tag(f, "ilst", 0);
    CHECK(pos >= 4);
    uint32_t size = be32(f->buf + pos - 4);
    uint8_t *saved = (uint8_t *)malloc(size);
    CHECK(saved);
    memcpy(saved, f->buf + pos - 4, size);
    faam_io io = mem_io(f);
    faam_chapter chapters[2] = { { sizeof(faam_chapter), 0, 0, "Intro" }, { sizeof(faam_chapter), 0, 50, "End" } };
    f->pos = 0;
    CHECK(faam_update_chapters_stream(&io, chapters, 2, 0) == FAAM_OK);
    pos = find_tag(f, "ilst", 0);
    CHECK(pos >= 4);
    CHECK(be32(f->buf + pos - 4) == size);
    CHECK(!memcmp(saved, f->buf + pos - 4, size));
    free(saved);

    faam_metadata meta = { .struct_size = sizeof(meta) };
    meta.title = "After chapters";
    f->pos = 0;
    CHECK(faam_update_tags_stream(&io, &meta, 0) == FAAM_OK);
    faam_demuxer *d = open_mem(f, 0);
    faam_chapter got[2];
    for (uint32_t i = 0; i < 2; i++) got[i].struct_size = sizeof(got[i]);
    uint32_t count = 0;
    CHECK(test_read_chapters(d, got, 2, &count) == FAAM_OK && count == 2);
    for (uint32_t i = 0; i < count; i++) {
        CHECK(got[i].start_ms == chapters[i].start_ms);
        CHECK(!strcmp(got[i].title, chapters[i].title));
    }
    faam_demuxer_close(&d);
}

static void test_unmodeled_atoms_preservation(void) {
    memfile f = {0};
    mux_opts o = { .frames = 5, .ticks = 1024 };
    mux_file(&f, &o);

    /* First create udta/meta/ilst via tag update */
    faam_io io = mem_io(&f);
    faam_metadata init_meta = { .struct_size = sizeof(init_meta) }; init_meta.title = "Seed";
    CHECK(faam_update_tags_stream(&io, &init_meta, 0) == FAAM_OK);

    /* Construct custom ilst bytes containing modeled + unmodeled atoms */
    uint8_t custom_ilst[] = {
        /* ilst header placeholder */
        0, 0, 0, 0, 'i', 'l', 's', 't',
        /* 1. \251nam (Title: "Original Title") */
        0, 0, 0, 32, '\251', 'n', 'a', 'm',
          0, 0, 0, 24, 'd', 'a', 't', 'a',
          0, 0, 0, 1, 0, 0, 0, 0, 'O', 'r', 'i', 'g', 'i', 'n', 'a', 'l',
        /* 2. \251grp (Grouping: "Grp") -> unmodeled */
        0, 0, 0, 27, '\251', 'g', 'r', 'p',
          0, 0, 0, 19, 'd', 'a', 't', 'a',
          0, 0, 0, 1, 0, 0, 0, 0, 'G', 'r', 'p',
        /* 3. desc (Description: "Dsc") -> unmodeled */
        0, 0, 0, 27, 'd', 'e', 's', 'c',
          0, 0, 0, 19, 'd', 'a', 't', 'a',
          0, 0, 0, 1, 0, 0, 0, 0, 'D', 's', 'c',
        /* 4. ---- (mean: "com.custom.namespace", name: "MYKEY", data: "MYVAL") */
        0, 0, 0, 78, '-', '-', '-', '-',
          0, 0, 0, 32, 'm', 'e', 'a', 'n', 0, 0, 0, 0, 'c', 'o', 'm', '.', 'c', 'u', 's', 't', 'o', 'm', '.', 'n', 'a', 'm', 'e', 's', 'p', 'a', 'c', 'e',
          0, 0, 0, 17, 'n', 'a', 'm', 'e', 0, 0, 0, 0, 'M', 'Y', 'K', 'E', 'Y',
          0, 0, 0, 21, 'd', 'a', 't', 'a', 0, 0, 0, 1, 0, 0, 0, 0, 'M', 'Y', 'V', 'A', 'L',
        /* 5. covr (1st cover art: "IMG1") */
        0, 0, 0, 28, 'c', 'o', 'v', 'r',
          0, 0, 0, 20, 'd', 'a', 't', 'a', 0, 0, 0, 13, 0, 0, 0, 0, 'I', 'M', 'G', '1',
        /* 6. covr (2nd cover art: "IMG2") -> extra cover */
        0, 0, 0, 28, 'c', 'o', 'v', 'r',
          0, 0, 0, 20, 'd', 'a', 't', 'a', 0, 0, 0, 13, 0, 0, 0, 0, 'I', 'M', 'G', '2',
        /* 7. xyz1 (unknown 4cc: "RAW1234") -> unmodeled */
        0, 0, 0, 15, 'x', 'y', 'z', '1', 'R', 'A', 'W', '1', '2', '3', '4'
    };
    uint32_t ilst_len = (uint32_t)sizeof(custom_ilst);
    put32(custom_ilst, ilst_len);

    /* Inject custom ilst into file */
    long ilst_pos = find_tag(&f, "ilst", 0);
    CHECK(ilst_pos > 8);
    faam_atom_ref moov = {0}, mdat = {0};
    uint64_t fsz = f.len;
    CHECK(faam_atom_scan_top(&io, &moov, &mdat, &fsz) == FAAM_OK);
    long meta_pos = find_tag(&f, "meta", 0);
    long udta_pos = find_tag(&f, "udta", 0);
    uint64_t ancestors[3] = { (uint64_t)(meta_pos - 4), (uint64_t)(udta_pos - 4), moov.offset };
    uint32_t old_ilst_sz = be32(f.buf + ilst_pos - 4);
    CHECK(faam_atom_resize(&io, (uint64_t)(ilst_pos - 4), old_ilst_sz, custom_ilst, ilst_len, ancestors, 3, &moov, &mdat, fsz) == FAAM_OK);

    memfile chapter_file = {0};
    CHECK(mem_write(&chapter_file, f.buf, (uint32_t)f.len) == (int32_t)f.len);
    test_chapters_preserve_ilst(&chapter_file);
    free(chapter_file.buf);

    /* 1. Demuxer must report custom tag mean namespace */
    faam_demuxer *d = open_mem(&f, 0);
    faam_custom_tag tag = { .struct_size = sizeof(tag) };
    faam_metadata meta_got; meta_got.struct_size = sizeof(meta_got);
    CHECK(faam_demuxer_get_metadata(d, &meta_got) == FAAM_OK);
    CHECK(meta_got.num_custom_tags == 1 && meta_got.custom_tags == NULL);
    CHECK(faam_demuxer_get_custom_tag(d, 0, &tag) == FAAM_OK);
    CHECK(!strcmp(tag.name, "MYKEY"));
    CHECK(!strcmp(tag.value, "MYVAL"));
    CHECK(tag.mean && !strcmp(tag.mean, "com.custom.namespace"));

    /* 2. Update tags via faam_update_tags_stream (changing title) */
    faam_metadata update_meta = meta_got;
    update_meta.custom_tags = &tag;
    update_meta.title = "New Updated Title";
    f.pos = 0;
    CHECK(faam_update_tags_stream(&io, &update_meta, 0) == FAAM_OK);
    faam_demuxer_close(&d);

    /* Verify unmodeled atoms (\251grp, desc, xyz1, 2nd covr) and custom mean were preserved */
    CHECK(find_tag(&f, "\251grp", 0) > 0);
    CHECK(find_tag(&f, "desc", 0) > 0);
    CHECK(find_tag(&f, "xyz1", 0) > 0);
    long covr1 = find_tag(&f, "covr", 0);
    CHECK(covr1 > 0);
    long covr2 = find_tag(&f, "covr", covr1 + 4);
    CHECK(covr2 > 0); /* 2nd covr preserved! */
    CHECK(find_tag(&f, "com.custom.namespace", 0) > 0); /* custom mean preserved! */

    d = open_mem(&f, 0);
    CHECK(faam_demuxer_get_metadata(d, &meta_got) == FAAM_OK);
    CHECK(!strcmp(meta_got.title, "New Updated Title"));
    CHECK(meta_got.num_custom_tags == 1 && meta_got.custom_tags == NULL);
    CHECK(faam_demuxer_get_custom_tag(d, 0, &tag) == FAAM_OK);
    CHECK(tag.mean && !strcmp(tag.mean, "com.custom.namespace"));
    faam_demuxer_close(&d);

    /* 3. faam_update_tags_stream with FAAM_TAG_UPDATE_CLEAR must drop unmodeled atoms from ilst */
    faam_metadata clear_meta = { .struct_size = sizeof(clear_meta) };
    clear_meta.title = "Clear Title";
    f.pos = 0;
    CHECK(faam_update_tags_stream(&io, &clear_meta, FAAM_TAG_UPDATE_CLEAR) == FAAM_OK);
    long new_ilst = find_tag(&f, "ilst", 0);
    CHECK(new_ilst > 4);
    uint32_t new_ilst_sz = be32(f.buf + new_ilst - 4);
    long ilst_end = new_ilst - 4 + new_ilst_sz;
    CHECK(find_tag_in_range(&f, "\251grp", new_ilst - 4, ilst_end) < 0);
    CHECK(find_tag_in_range(&f, "desc", new_ilst - 4, ilst_end) < 0);
    CHECK(find_tag_in_range(&f, "xyz1", new_ilst - 4, ilst_end) < 0);
    CHECK(find_tag_in_range(&f, "covr", new_ilst - 4, ilst_end) < 0);
    CHECK(find_tag_in_range(&f, "com.custom.namespace", new_ilst - 4, ilst_end) < 0);

    free(f.buf);
}

static void test_mdta_unsupported(void) {
    memfile f = {0};
    mux_opts o = { .frames = 5, .ticks = 1024 };
    mux_file(&f, &o);

    /* First update tags to ensure udta/meta/hdlr exist */
    faam_io io_init = mem_io(&f);
    faam_metadata init_meta = { .struct_size = sizeof(init_meta) }; init_meta.title = "Seed";
    CHECK(faam_update_tags_stream(&io_init, &init_meta, 0) == FAAM_OK);

    /* Patch meta's hdlr from 'mdir' / 'mdirappl' to 'mdta' */
    long meta_pos = find_tag(&f, "meta", 0);
    CHECK(meta_pos > 0);
    long hdlr_pos = find_tag(&f, "hdlr", meta_pos);
    CHECK(hdlr_pos > 0);
    /* hdlr payload: 4-byte ver/flags, 4-byte pre_defined, then 4-byte handler_type */
    memcpy(f.buf + hdlr_pos + 12, "mdta", 4);

    memfile ref = f;
    ref.buf = (uint8_t *)malloc(f.len);
    CHECK(ref.buf);
    memcpy(ref.buf, f.buf, f.len);

    faam_io io = mem_io(&f);
    faam_metadata meta = { .struct_size = sizeof(meta) }; meta.title = "Attempted Edit";
    CHECK(faam_update_tags_stream(&io, &meta, 0) == FAAM_ERR_UNSUPPORTED);
    /* Buffer must be untouched! */
    CHECK(f.len == ref.len && !memcmp(f.buf, ref.buf, f.len));

    free(ref.buf);
    free(f.buf);
}

static void test_per_track_gapless(int mode) {
    memfile f = {0};
    faam_muxer_config cfg;
    faam_track_config cfg_tracks[8];
    CHECK(faam_muxer_config_init(&cfg, sizeof(cfg)) == FAAM_OK);

    faam_track_config tc1 = { .struct_size = sizeof(tc1) };
    tc1.track_type = FAAM_TRACK_AUDIO; tc1.codec_id = FAAM_CODEC_AAC; tc1.track_id = 1;
    tc1.timescale = tc1.sample_rate = 44100; tc1.channels = 2;
    tc1.codec_data = asc_lc_44k; tc1.codec_data_len = ASC_LEN;
    /* Function scope: the pointer is read when the muxer opens, long after an if body's block has ended. */
    faam_gapless_info tc1_gapless = { sizeof(faam_gapless_info), 1024, 500, 0, 0 };
    if (mode != 2) tc1.gapless = &tc1_gapless;
    CHECK(test_append_track(&cfg, cfg_tracks, &tc1, NULL) == FAAM_OK);

    faam_track_config tc2 = { .struct_size = sizeof(tc2) };
    tc2.track_type = FAAM_TRACK_AUDIO; tc2.codec_id = FAAM_CODEC_AAC; tc2.track_id = 2;
    tc2.timescale = tc2.sample_rate = 48000; tc2.channels = 2;
    static const uint8_t asc_lc_48k[ASC_LEN] = { 0x11, 0x90 };
    tc2.codec_data = asc_lc_48k; tc2.codec_data_len = ASC_LEN;
    tc2.gapless = &(faam_gapless_info){ sizeof(faam_gapless_info), 1040, 600, 0, 0 };
    CHECK(test_append_track(&cfg, cfg_tracks, &tc2, NULL) == FAAM_OK);

    faam_io io = mem_io(&f);
    faam_muxer *m = NULL;
    CHECK(faam_muxer_open(&cfg, &io, &m) == FAAM_OK);

    /* Distinct file-level values make accidental reuse on track 2 observable. */
    faam_gapless_info file_gapless = { sizeof(faam_gapless_info), 2112, 700, 0, 109828 };
    if (mode == 1) CHECK(faam_muxer_update(m, &(faam_muxer_update_params){ .struct_size = sizeof(faam_muxer_update_params), .flags = FAAM_UPDATE_GAPLESS, .gapless = &file_gapless }) == FAAM_OK);

    uint8_t frame[16] = {0};
    for (int i = 0; i < 110; i++) {
        CHECK(faam_muxer_write_frame(m, 1, frame, 10, 1024, 0, FAAM_FRAME_KEYFRAME) == FAAM_OK);
        CHECK(faam_muxer_write_frame(m, 2, frame, 10, 1024, 0, FAAM_FRAME_KEYFRAME) == FAAM_OK);
    }
    CHECK(faam_muxer_finalize(m) == FAAM_OK);
    faam_muxer_close(&m);

    faam_demuxer *d = open_mem(&f, 0);
    faam_gapless_info g1 = { .struct_size = sizeof(g1) }, g2 = { .struct_size = sizeof(g2) };
    CHECK(faam_demuxer_get_track_gapless(d, 1, &g1) == FAAM_OK);
    CHECK(faam_demuxer_get_track_gapless(d, 2, &g2) == FAAM_OK);

    if (mode == 1) {
        CHECK(find_tag(&f, "iTunSMPB", 0) > 0);
        CHECK(g1.encoder_delay == file_gapless.encoder_delay &&
              g1.end_padding == file_gapless.end_padding && g1.total_samples == file_gapless.total_samples);
    } else if (mode == 2) {
        CHECK(find_tag(&f, "iTunSMPB", 0) < 0);
        CHECK(g1.encoder_delay == 0 && g1.end_padding == 0 && g1.total_samples == 0);
        faam_gapless_info g = { sizeof(faam_gapless_info), 1, 2, 0, 3 };
        CHECK(faam_demuxer_get_track_gapless(d, 0, &g) == FAAM_OK);
        CHECK(g.encoder_delay == 0 && g.end_padding == 0 && g.total_samples == 0);
    } else {
        CHECK(g1.encoder_delay == 1024 && g1.end_padding == 500 && g1.total_samples == 111116);
    }
    CHECK(g2.encoder_delay == 1040 && g2.end_padding == 600 && g2.total_samples == 111000);

    long e1 = find_tag(&f, "elst", 0);
    CHECK(e1 > 0);
    long e2 = e1;
    if (mode != 2) {
        /* Movie ticks are the finest audio rate, 48000 Hz here. */
        CHECK(be32(f.buf + e1 + 12) == (uint32_t)((111116ULL * 48000 + 44100 / 2) / 44100) && be32(f.buf + e1 + 16) == 1024);
        e2 = find_tag(&f, "elst", e1 + 4);
        CHECK(e2 > 0);
    }
    CHECK(be32(f.buf + e2 + 12) == 111000 && be32(f.buf + e2 + 16) == 1040);
    if (mode == 2) CHECK(find_tag(&f, "elst", e2 + 4) < 0);

    faam_demuxer_close(&d);
    free(f.buf);
}

/* ---- 9. corrupt sizes must not wrap ---- */

/* A moov whose last child declares a size that is huge as an unsigned value: parsing must
 * reject it instead of stepping with a wrapped (negative) length. The 32-bit size matters
 * where long is 32 bits wide, the 64-bit one everywhere. */
static void open_with_bad_child(const uint8_t *hdr, size_t hdr_len) {
    memfile f = {0};
    uint8_t ftyp[16];
    put32(ftyp, 16); memcpy(ftyp + 4, "ftyp", 4); memcpy(ftyp + 8, "M4A ", 4); put32(ftyp + 12, 0);
    mem_write(&f, ftyp, sizeof(ftyp));
    uint8_t zeros[16] = {0};
    uint8_t moov[8];
    put32(moov, (uint32_t)(8 + 8 + hdr_len + sizeof(zeros))); memcpy(moov + 4, "moov", 4);
    mem_write(&f, moov, sizeof(moov));
    uint8_t filler[8];
    put32(filler, 8); memcpy(filler + 4, "free", 4);
    mem_write(&f, filler, sizeof(filler));
    mem_write(&f, hdr, hdr_len);
    mem_write(&f, zeros, sizeof(zeros));

    f.pos = 0;
    faam_io io = mem_io(&f);
    faam_demuxer *d = NULL;
    if (faam_demuxer_open(NULL, &io, &d) == FAAM_OK) {
        uint32_t nt = 1;
        CHECK(faam_demuxer_get_num_tracks(d, &nt, NULL) == FAAM_OK && nt == 0);
        faam_demuxer_close(&d);
    }
    free(f.buf);
}

static void test_oversized_child_box(void) {
    uint8_t big64[16] = { 0, 0, 0, 1, 't', 'r', 'a', 'k', 0x80, 0, 0, 0, 0, 0, 0, 0x10 };
    open_with_bad_child(big64, sizeof(big64));
    uint8_t max64[16] = { 0, 0, 0, 1, 't', 'r', 'a', 'k', 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF8 };
    open_with_bad_child(max64, sizeof(max64));
    uint8_t big32[8] = { 0x90, 0, 0, 0, 't', 'r', 'a', 'k' };
    open_with_bad_child(big32, sizeof(big32));
}

/* A co64 entry close to the signed limit plus a growing tag list moves it past INT64_MAX:
 * the shift is modular arithmetic on the unsigned offset, not signed overflow. */
static void test_chunk_offset_wrap(void) {
    memfile f = {0};
    mux_opts o = { .frames = 64, .ticks = 1024 };
    mux_file(&f, &o);
    make_faststart(&f);
    /* The muxer writes one chunk, so no stco box has room for a wide entry; a constant-rate
     * stts is exactly a one-entry co64 in size and is not read by a tag update. */
    long i = find_tag(&f, "stts", 0);
    CHECK(i > 0 && be32(f.buf + i - 4) == 24);
    memcpy(f.buf + i, "co64", 4);
    put32(f.buf + i + 8, 1);
    const uint64_t big = 0x7FFFFFFFFFFFFFF0ULL;
    put32(f.buf + i + 12, (uint32_t)(big >> 32));
    put32(f.buf + i + 16, (uint32_t)big);

    size_t before = f.len;
    faam_io io = mem_io(&f);
    faam_metadata meta = { .struct_size = sizeof(meta) };
    meta.title = "A title long enough that the tag list grows by more than sixteen bytes";
    f.pos = 0;
    CHECK(faam_update_tags_stream(&io, &meta, 0) == FAAM_OK);
    CHECK(f.len > before + 16);
    uint64_t delta = f.len - before;

    i = find_tag(&f, "co64", 0);
    CHECK(i > 0 && be32(f.buf + i + 8) == 1);
    CHECK(be64(f.buf + i + 12) == big + delta);
    free(f.buf);
}

/* A tag edit that has to append to a container whose last child overruns it would write into
 * that child; it must refuse and leave the file as it was. */
static void test_overrunning_child_refused(void) {
    memfile f = {0};
    mux_opts o = { .frames = 8, .ticks = 1024 };
    mux_file(&f, &o);
    faam_io io = mem_io(&f);
    faam_metadata meta = { .struct_size = sizeof(meta) }; meta.title = "Seed";
    f.pos = 0;
    CHECK(faam_update_tags_stream(&io, &meta, 0) == FAAM_OK);

    /* The ilst is no longer recognised, and its size now runs one byte past meta. */
    long i = find_tag(&f, "ilst", 0);
    CHECK(i >= 4);
    memcpy(f.buf + i, "free", 4);
    put32(f.buf + i - 4, be32(f.buf + i - 4) + 1);

    memfile ref = f;
    ref.buf = (uint8_t *)malloc(f.len);
    CHECK(ref.buf);
    memcpy(ref.buf, f.buf, f.len);

    meta.title = "A longer title for the second edit";
    f.pos = 0;
    CHECK(faam_update_tags_stream(&io, &meta, 0) == FAAM_ERR_BAD_CONTAINER);
    CHECK(f.len == ref.len && !memcmp(f.buf, ref.buf, f.len));
    free(ref.buf);
    free(f.buf);

    /* Both editors append to moov when it has no udta, and the last track overruns it here. */
    memfile g = {0};
    mux_file(&g, &o);
    long t = find_tag(&g, "trak", 0);
    CHECK(t >= 4);
    put32(g.buf + t - 4, be32(g.buf + t - 4) + 1);
    memfile gref = g;
    gref.buf = (uint8_t *)malloc(g.len);
    CHECK(gref.buf);
    memcpy(gref.buf, g.buf, g.len);
    faam_io gio = mem_io(&g);
    faam_chapter ch[1] = { { sizeof(faam_chapter), 0, 0, "Chapter" } };
    g.pos = 0;
    CHECK(faam_update_chapters_stream(&gio, ch, 1, 0) == FAAM_ERR_BAD_CONTAINER);
    CHECK(g.len == gref.len && !memcmp(g.buf, gref.buf, g.len));
    g.pos = 0;
    CHECK(faam_update_tags_stream(&gio, &meta, 0) == FAAM_ERR_BAD_CONTAINER);
    CHECK(g.len == gref.len && !memcmp(g.buf, gref.buf, g.len));
    free(gref.buf);
    free(g.buf);

    /* A udta whose size says "to the end" has no room after it for a sibling either. */
    memfile h = {0};
    mux_file(&h, &o);
    faam_io hio = mem_io(&h);
    h.pos = 0;
    CHECK(faam_update_tags_stream(&hio, &meta, 0) == FAAM_OK);
    long u = find_tag(&h, "udta", 0);
    CHECK(u >= 4);
    put32(h.buf + u - 4, 0);
    memfile href = h;
    href.buf = (uint8_t *)malloc(h.len);
    CHECK(href.buf);
    memcpy(href.buf, h.buf, h.len);
    h.pos = 0;
    CHECK(faam_update_chapters_stream(&hio, ch, 1, 0) == FAAM_ERR_BAD_CONTAINER);
    CHECK(h.len == href.len && !memcmp(h.buf, href.buf, h.len));
    free(href.buf);
    free(h.buf);
}

/* A real file of equal-sized samples has far more of them than its moov has bytes, so the
 * bound on a claimed count must come from the whole stream, not from the moov. */
static void test_fixed_size_samples_beyond_moov(void) {
    memfile f = {0};
    mux_opts o = { .frames = 8, .ticks = 1024 };
    mux_file(&f, &o);
    long m = find_tag(&f, "moov", 0);
    long z = find_tag(&f, "stsz", 0);
    long c = find_tag(&f, "stsc", 0);
    CHECK(m > 4 && z > 0 && c > 0);
    CHECK(be32(f.buf + m - 4) < 1500);

    /* The stream carries 4000 more bytes after moov, enough for 1500 one-byte samples. */
    size_t old_len = f.len;
    f.buf = (uint8_t *)realloc(f.buf, old_len + 4000);
    CHECK(f.buf);
    memset(f.buf + old_len, 0, 4000);
    put32(f.buf + old_len, 4000);
    memcpy(f.buf + old_len + 4, "free", 4);
    f.len = f.cap = old_len + 4000;
    put32(f.buf + z + 8, 1);
    put32(f.buf + z + 12, 1500);
    put32(f.buf + c + 4 + 4 + 4 + 4, 1500);

    faam_demuxer *d = open_mem(&f, 0);
    faam_track_info ti;
    ti.struct_size = sizeof(ti);
    CHECK(faam_demuxer_get_track_info(d, 0, &ti) == FAAM_OK);
    CHECK(ti.total_frames == 1500);
    faam_demuxer_close(&d);
    free(f.buf);
}

/* Equal-sized samples and a chunk table can together claim far more samples than the file
 * has bytes; the index must be sized by what could really be there. */
static void test_claimed_samples_bounded(void) {
    memfile f = {0};
    mux_opts o = { .frames = 8, .ticks = 1024 };
    mux_file(&f, &o);
    long z = find_tag(&f, "stsz", 0);
    long c = find_tag(&f, "stsc", 0);
    CHECK(z > 0 && c > 0);
    put32(f.buf + z + 8, 1);           /* every sample one byte */
    put32(f.buf + z + 12, 50000000);   /* claimed sample count */
    put32(f.buf + c + 4 + 4 + 4 + 4, 50000000); /* first entry: samples per chunk */

    faam_demuxer *d = open_mem(&f, 0);
    faam_track_info ti;
    ti.struct_size = sizeof(ti);
    CHECK(faam_demuxer_get_track_info(d, 0, &ti) == FAAM_OK);
    CHECK(ti.total_frames <= f.len);
    faam_demuxer_close(&d);
    free(f.buf);
}

int main(void) {
    test_status_contract();
    test_short_reads();
    test_gapless();
    test_per_track_gapless(0);
    test_per_track_gapless(1);
    test_per_track_gapless(2);
    test_udta_creation();
    test_chapter_counts();
    test_multitrack_gapless();
    test_creation_time();
    test_language();
    test_seek_failures();
    test_extended_moov();
    test_unmodeled_atoms_preservation();
    test_mdta_unsupported();
    test_oversized_child_box();
    test_chunk_offset_wrap();
    test_overrunning_child_refused();
    test_fixed_size_samples_beyond_moov();
    test_claimed_samples_bounded();
    printf("libfaam contract tests passed.\n");
    return 0;
}
