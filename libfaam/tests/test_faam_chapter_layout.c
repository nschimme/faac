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
 * Chapters are written as a Nero chpl atom and as a QuickTime text track laid out the way ffmpeg
 * writes it. The two lists are parsed here from the raw atoms, without the demuxer, and must equal
 * each other and the demuxer's list after every operation, in each file layout. The layout checks
 * pin the details players depend on (hidden-but-in-movie track, text handler, gmhd, UTF-8 marker,
 * chapter trak ahead of mvex), and the fallback cases show when the text track is not trusted.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "faam.h"
#include "mem_io.h"
#include "../../tests/faam_test_helpers.h"

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

#include "ffmpeg_fragmented.h"

enum { FRAMES = 40, MAX_LIST = 8 };
static const uint8_t asc_lc_44k[2] = { 0x12, 0x10 };
static const uint8_t k_encd[12] = { 0, 0, 0, 12, 'e', 'n', 'c', 'd', 0, 0, 1, 0 };

typedef struct { uint64_t ms; char title[64]; } entry;
typedef struct { entry e[MAX_LIST]; uint32_t n; } list;

typedef enum { LAYOUT_MOOV_LAST, LAYOUT_FASTSTART, LAYOUT_FRAGMENTED } layout;

/* ---- generated audio ---- */

static uint32_t frame_len(uint32_t i) { return 10 + i % 5; }
static uint8_t frame_byte(uint32_t i, uint32_t k) { return (uint8_t)(i * 7 + k); }

static void mux_ex(mem_file *out, const list *l, layout lay, bool finalize)
{
    faam_chapter ch[MAX_LIST];
    for (uint32_t i = 0; i < l->n; i++)
        ch[i] = (faam_chapter){ .struct_size = sizeof(faam_chapter), .start_ms = l->e[i].ms, .title = l->e[i].title };
    faam_muxer_config cfg;
    CHECK(faam_muxer_config_init(&cfg, sizeof(cfg)) == FAAM_OK);
    cfg.flags = FAAM_MUXER_M4B | (lay == LAYOUT_FASTSTART ? FAAM_MUXER_FASTSTART : 0);
    cfg.fragment_ms = lay == LAYOUT_FRAGMENTED ? 200 : 0;
    cfg.chapters = ch;
    cfg.num_chapters = l->n;
    faam_track_config tc;
    CHECK(faam_track_config_init(&tc, sizeof(tc)) == FAAM_OK);
    tc.track_type = FAAM_TRACK_AUDIO; tc.codec_id = FAAM_CODEC_AAC; tc.track_id = 1;
    tc.timescale = tc.sample_rate = 44100; tc.channels = 2;
    tc.codec_data = asc_lc_44k; tc.codec_data_len = sizeof(asc_lc_44k);
    cfg.num_tracks = 1;
    cfg.tracks = &tc;
    faam_io io = mem_file_io(out);
    faam_muxer *m = NULL;
    CHECK(faam_muxer_open(&cfg, &io, &m) == FAAM_OK);
    for (uint32_t i = 0; i < FRAMES; i++) {
        uint8_t fr[16];
        for (uint32_t k = 0; k < frame_len(i); k++) fr[k] = frame_byte(i, k);
        CHECK(faam_muxer_write_frame(m, 1, fr, frame_len(i), 1024, 0, FAAM_FRAME_KEYFRAME) == FAAM_OK);
    }
    if (finalize) CHECK(faam_muxer_finalize(m) == FAAM_OK);
    faam_muxer_close(&m);
}

static void mux(mem_file *out, const list *l, layout lay) { mux_ex(out, l, lay, true); }

static void check_audio(mem_file *f)
{
    f->pos = 0;
    faam_io io = mem_file_io(f);
    faam_demuxer *d = NULL;
    CHECK(faam_demuxer_open(NULL, &io, &d) == FAAM_OK);
    uint32_t held = 0, i = 0;
    CHECK(faam_demuxer_get_num_tracks(d, &held, NULL) == FAAM_OK && held == 1);
    for (;; i++) {
        uint8_t fr[64];
        uint32_t got = 0;
        if (faam_demuxer_read_frame(d, fr, sizeof(fr), &got) != FAAM_OK) break;
        CHECK(i < FRAMES && got == frame_len(i));
        for (uint32_t k = 0; k < got; k++) CHECK(fr[k] == frame_byte(i, k));
    }
    CHECK(i == FRAMES);
    faam_demuxer_close(&d);
}

/* ---- raw atom parsing, independent of the demuxer ---- */

static bool child(const mem_file *f, size_t *s, size_t *e, const char *name)
{
    for (size_t p = *s; p + 8 <= *e;) {
        uint32_t sz = mem_be32(f->buf + p);
        if (sz < 8 || sz > *e - p) return false;
        if (!memcmp(f->buf + p + 4, name, 4)) { *s = p + 8; *e = p + sz; return true; }
        p += sz;
    }
    return false;
}

static void set_title(entry *e, const uint8_t *t, uint32_t n)
{
    CHECK(n < sizeof(e->title));
    memcpy(e->title, t, n);
    e->title[n] = 0;
}

static void raw_chpl(const mem_file *f, list *out)
{
    memset(out, 0, sizeof(*out));
    size_t s = 0, e = f->len;
    if (!child(f, &s, &e, "moov") || !child(f, &s, &e, "udta") || !child(f, &s, &e, "chpl")) return;
    CHECK(e - s >= 9);
    uint32_t count = f->buf[s + 8];
    size_t p = s + 9;
    for (uint32_t i = 0; i < count; i++) {
        CHECK(p + 9 <= e && out->n < MAX_LIST);
        uint32_t tl = f->buf[p + 8];
        CHECK(p + 9 + tl <= e);
        out->e[out->n].ms = mem_be64(f->buf + p) / 10000;
        set_title(&out->e[out->n++], f->buf + p + 9, tl);
        p += 9 + tl;
    }
}

/* The trak that the first trak's tref/chap names; false when there is none. */
static bool chapter_trak(const mem_file *f, size_t *ts, size_t *te, uint32_t *id)
{
    size_t ms = 0, me = f->len;
    CHECK(child(f, &ms, &me, "moov"));
    *id = 0;
    for (size_t p = ms; p + 8 <= me && !*id; p += mem_be32(f->buf + p)) {
        size_t s = p + 8, e = p + mem_be32(f->buf + p);
        if (!memcmp(f->buf + p + 4, "trak", 4) && child(f, &s, &e, "tref") && child(f, &s, &e, "chap")) *id = mem_be32(f->buf + s);
    }
    if (!*id) return false;
    for (size_t p = ms; p + 8 <= me; p += mem_be32(f->buf + p)) {
        size_t s = p + 8, e = p + mem_be32(f->buf + p);
        if (memcmp(f->buf + p + 4, "trak", 4) || !child(f, &s, &e, "tkhd")) continue;
        if (mem_be32(f->buf + s + (f->buf[s] == 1 ? 20 : 12)) == *id) { *ts = p + 8; *te = p + mem_be32(f->buf + p); return true; }
    }
    return false;
}

/* One text sample is length + UTF-8 + the encd marker. */
static void take_sample(const mem_file *f, size_t at, uint32_t size, entry *out)
{
    CHECK(size >= 2 + sizeof(k_encd) && at + size <= f->len);
    uint32_t tl = (uint32_t)f->buf[at] << 8 | f->buf[at + 1];
    CHECK(size == 2 + tl + sizeof(k_encd));
    CHECK(!memcmp(f->buf + at + 2 + tl, k_encd, sizeof(k_encd)));
    set_title(out, f->buf + at + 2, tl);
}

/* Fragmented: the samples are in the trun of whichever fragments carry the chapter track. */
static void raw_text_fragments(const mem_file *f, uint32_t chap_id, uint32_t scale, list *out)
{
    for (size_t p = 0; p + 8 <= f->len && mem_be32(f->buf + p) >= 8; p += mem_be32(f->buf + p)) {
        if (memcmp(f->buf + p + 4, "moof", 4)) continue;
        size_t mend = p + mem_be32(f->buf + p);
        for (size_t t = p + 8; t + 8 <= mend; t += mem_be32(f->buf + t)) {
            if (memcmp(f->buf + t + 4, "traf", 4)) continue;
            size_t tend = t + mem_be32(f->buf + t);
            uint64_t base = p, dts = 0;
            uint32_t def_dur = 0, def_size = 0;
            for (size_t c = t + 8; c + 8 <= tend; c += mem_be32(f->buf + c)) {
                const uint8_t *q = f->buf + c + 8;
                if (!memcmp(f->buf + c + 4, "tfhd", 4)) {
                    uint32_t fl = mem_be32(q) & 0xffffff;
                    if (mem_be32(q + 4) != chap_id) break;
                    q += 8;
                    if (fl & 1) { base = mem_be64(q); q += 8; }
                    if (fl & 2) q += 4;
                    if (fl & 8) { def_dur = mem_be32(q); q += 4; }
                    if (fl & 0x10) def_size = mem_be32(q);
                } else if (!memcmp(f->buf + c + 4, "tfdt", 4)) {
                    dts = q[0] == 1 ? mem_be64(q + 4) : mem_be32(q + 4);
                } else if (!memcmp(f->buf + c + 4, "trun", 4)) {
                    uint32_t fl = mem_be32(q) & 0xffffff, count = mem_be32(q + 4);
                    int64_t doff = 0;
                    q += 8;
                    if (fl & 1) { doff = (int32_t)mem_be32(q); q += 4; }
                    if (fl & 4) q += 4;
                    size_t at = (size_t)(base + doff);
                    for (uint32_t i = 0; i < count; i++) {
                        uint32_t d = def_dur, z = def_size;
                        if (fl & 0x100) { d = mem_be32(q); q += 4; }
                        if (fl & 0x200) { z = mem_be32(q); q += 4; }
                        if (fl & 0x400) q += 4;
                        if (fl & 0x800) q += 4;
                        CHECK(out->n < MAX_LIST);
                        take_sample(f, at, z, &out->e[out->n]);
                        out->e[out->n++].ms = (dts * 1000 + scale / 2) / scale;
                        dts += d;
                        at += z;
                    }
                }
            }
        }
    }
}

static void raw_text(const mem_file *f, list *out)
{
    memset(out, 0, sizeof(*out));
    size_t ts, te;
    uint32_t id;
    if (!chapter_trak(f, &ts, &te, &id)) return;
    size_t ds = ts, de = te;
    CHECK(child(f, &ds, &de, "mdia"));
    size_t hs = ds, he = de;
    CHECK(child(f, &hs, &he, "mdhd"));
    uint32_t scale = mem_be32(f->buf + hs + (f->buf[hs] == 1 ? 20 : 12));
    CHECK(scale == 1000);
    size_t ss = ds, se = de;
    CHECK(child(f, &ss, &se, "minf") && child(f, &ss, &se, "stbl"));
    size_t t0 = ss, t1 = se, z0 = ss, z1 = se, o0 = ss, o1 = se;
    CHECK(child(f, &t0, &t1, "stts") && child(f, &z0, &z1, "stsz"));
    bool wide = false;
    if (!child(f, &o0, &o1, "stco")) { o0 = ss; o1 = se; CHECK(child(f, &o0, &o1, "co64")); wide = true; }
    uint32_t n = mem_be32(f->buf + z0 + 8), nstts = mem_be32(f->buf + t0 + 4);
    if (!n) { raw_text_fragments(f, id, scale, out); return; }
    CHECK(mem_be32(f->buf + z0 + 4) == 0 && nstts == n);
    size_t off = (size_t)(wide ? mem_be64(f->buf + o0 + 8) : mem_be32(f->buf + o0 + 8));
    uint64_t ticks = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t sz = mem_be32(f->buf + z0 + 12 + 4 * i);
        CHECK(out->n < MAX_LIST && mem_be32(f->buf + t0 + 8 + 8 * i) == 1);
        take_sample(f, off, sz, &out->e[out->n]);
        out->e[out->n++].ms = ticks;
        ticks += mem_be32(f->buf + t0 + 8 + 8 * i + 4);
        off += sz;
    }
}

static void read_list(mem_file *f, list *out)
{
    memset(out, 0, sizeof(*out));
    f->pos = 0;
    faam_io io = mem_file_io(f);
    faam_demuxer *d = NULL;
    CHECK(faam_demuxer_open(NULL, &io, &d) == FAAM_OK);
    uint32_t n = 0;
    CHECK(faam_demuxer_get_num_chapters(d, &n) == FAAM_OK && n <= MAX_LIST);
    for (; out->n < n; out->n++) {
        faam_chapter c = { .struct_size = sizeof(c) };
        CHECK(faam_demuxer_get_chapter(d, out->n, &c) == FAAM_OK);
        out->e[out->n].ms = c.start_ms;
        set_title(&out->e[out->n], (const uint8_t *)c.title, (uint32_t)strlen(c.title));
    }
    faam_demuxer_close(&d);
}

static bool same(const list *a, const list *b)
{
    if (a->n != b->n) return false;
    for (uint32_t i = 0; i < a->n; i++)
        if (a->e[i].ms != b->e[i].ms || strcmp(a->e[i].title, b->e[i].title)) return false;
    return true;
}

/* ---- layout facts ---- */

static void check_text_layout(const mem_file *f, layout lay)
{
    size_t ts, te;
    uint32_t id;
    CHECK(chapter_trak(f, &ts, &te, &id));

    size_t s = ts, e = te;
    CHECK(child(f, &s, &e, "tkhd"));
    CHECK(mem_be32(f->buf + s) == 2); /* in the movie, not enabled */
    size_t ds = ts, de = te;
    CHECK(child(f, &ds, &de, "mdia"));
    s = ds; e = de;
    CHECK(child(f, &s, &e, "hdlr") && !memcmp(f->buf + s + 8, "text", 4));
    CHECK(!memcmp(f->buf + s + 24, "SubtitleHandler", 15));
    size_t ms = ds, me = de;
    CHECK(child(f, &ms, &me, "minf"));
    s = ms; e = me;
    CHECK(child(f, &s, &e, "gmhd"));
    s = ms; e = me;
    CHECK(child(f, &s, &e, "stbl") && child(f, &s, &e, "stsd"));
    CHECK(mem_be32(f->buf + s + 4) == 1 && !memcmp(f->buf + s + 12, "text", 4));

    /* One chapter trak, found by its handler: removed ones are free boxes, not traks. */
    size_t mvs = 0, mve = f->len;
    CHECK(child(f, &mvs, &mve, "moov"));
    uint32_t text_traks = 0;
    size_t first_trak = 0, mvex_at = 0, chap_at = 0;
    for (size_t p = mvs; p + 8 <= mve; p += mem_be32(f->buf + p)) {
        if (!memcmp(f->buf + p + 4, "mvex", 4)) mvex_at = p;
        if (memcmp(f->buf + p + 4, "trak", 4)) continue;
        if (!first_trak) first_trak = p;
        size_t a = p + 8, b = p + mem_be32(f->buf + p);
        if (child(f, &a, &b, "mdia") && child(f, &a, &b, "hdlr") && !memcmp(f->buf + a + 8, "text", 4)) { text_traks++; chap_at = p; }
    }
    CHECK(text_traks == 1 && chap_at == te - mem_be32(f->buf + chap_at));
    size_t a = first_trak + 8, b = first_trak + mem_be32(f->buf + first_trak);
    CHECK(child(f, &a, &b, "tref") && child(f, &a, &b, "chap") && mem_be32(f->buf + a) == id);

    if (lay != LAYOUT_FRAGMENTED) return;
    /* AVFoundation ignores a chapter trak that follows mvex; a trex is what lets its fragment parse. */
    CHECK(mvex_at && chap_at < mvex_at);
    a = mvex_at + 8; b = mvex_at + mem_be32(f->buf + mvex_at);
    bool trex = false;
    for (size_t p = a; p + 8 <= b; p += mem_be32(f->buf + p))
        if (!memcmp(f->buf + p + 4, "trex", 4) && mem_be32(f->buf + p + 12) == id) trex = true;
    CHECK(trex);
}

static int by_value(const void *x, const void *y)
{
    uint32_t a = *(const uint32_t *)x, b = *(const uint32_t *)y;
    return (a > b) - (a < b);
}

/* A gap in the fragment numbering makes AVFoundation drop the chapters; an update frees the first
 * fragment, so the run may start above 1 but must stay unbroken. */
static void check_fragment_numbers(const mem_file *f)
{
    uint32_t seq[64], n = 0;
    for (size_t p = 0; p + 8 <= f->len && mem_be32(f->buf + p) >= 8; p += mem_be32(f->buf + p)) {
        if (memcmp(f->buf + p + 4, "moof", 4)) continue;
        size_t mf = p + 8;
        CHECK(!memcmp(f->buf + mf + 4, "mfhd", 4) && n < 64);
        seq[n++] = mem_be32(f->buf + mf + 12);
    }
    CHECK(n > 1);
    qsort(seq, n, sizeof(seq[0]), by_value);
    for (uint32_t i = 1; i < n; i++) CHECK(seq[i] == seq[i - 1] + 1);
}

/* Both raw lists, the demuxer's list and the expected one all agree, and the audio is untouched. */
static void verify(mem_file *f, const list *want, layout lay)
{
    list a, b, c;
    raw_chpl(f, &a);
    raw_text(f, &b);
    read_list(f, &c);
    CHECK(same(&a, want) && same(&b, want) && same(&c, want));
    if (want->n) {
        check_text_layout(f, lay);
        if (lay == LAYOUT_FRAGMENTED) check_fragment_numbers(f);
    } else {
        size_t ts, te;
        uint32_t id;
        CHECK(!chapter_trak(f, &ts, &te, &id));
    }
    check_audio(f);
}

static void update(mem_file *f, const list *l)
{
    faam_chapter ch[MAX_LIST];
    for (uint32_t i = 0; i < l->n; i++)
        ch[i] = (faam_chapter){ .struct_size = sizeof(faam_chapter), .start_ms = l->e[i].ms, .title = l->e[i].title };
    f->pos = 0;
    faam_io io = mem_file_io(f);
    CHECK(faam_update_chapters_stream(&io, ch, l->n, 0) == FAAM_OK);
}

static list make_list(const char *t0, const char *t1, const char *t2, uint64_t s1, uint64_t s2)
{
    list l = { .n = 0 };
    const char *t[3] = { t0, t1, t2 };
    uint64_t s[3] = { 0, s1, s2 };
    for (int i = 0; i < 3; i++) {
        if (!t[i]) continue;
        l.e[l.n].ms = s[i];
        snprintf(l.e[l.n++].title, sizeof(l.e[0].title), "%s", t[i]);
    }
    return l;
}

static void run_layout(layout lay)
{
    const list l1 = make_list("One", "Zwei \xc3\xa4\xe2\x82\xac", "Three", 300, 600);
    const list l2 = make_list("Intro", "Rest", NULL, 400, 0);
    const list l3 = make_list("A", "B", "C", 100, 700);
    const list none = { .n = 0 };
    mem_file f = { 0 };

    mux(&f, &l1, lay);
    verify(&f, &l1, lay);

    update(&f, &l2);
    verify(&f, &l2, lay);

    /* A tag edit grows moov; moov-last files must keep their audio offsets, moov-first ones shift them. */
    f.pos = 0;
    faam_io io = mem_file_io(&f);
    CHECK(test_set_tags(&io, "A rather long title to grow the moov atom a bit more", "Artist", 0) == FAAM_OK);
    verify(&f, &l2, lay);
    update(&f, &l3);
    verify(&f, &l3, lay);

    update(&f, &none);
    verify(&f, &none, lay);
    update(&f, &l1);
    verify(&f, &l1, lay);
    free(f.buf);
}

/* ---- inputs written by other muxers, made by taking one form away ---- */

static void rename_box(mem_file *f, size_t at, const char *type) { memcpy(f->buf + at + 4, type, 4); }

static void drop_text(mem_file *f)
{
    size_t ts, te;
    uint32_t id;
    CHECK(chapter_trak(f, &ts, &te, &id));
    rename_box(f, ts - 8, "free");
    size_t ms = 0, me = f->len;
    CHECK(child(f, &ms, &me, "moov"));
    size_t s = ms, e = me;
    CHECK(child(f, &s, &e, "trak"));
    size_t r = s, q = e;
    CHECK(child(f, &r, &q, "tref"));
    rename_box(f, r - 8, "free");
}

static void drop_chpl(mem_file *f)
{
    size_t s = 0, e = f->len;
    CHECK(child(f, &s, &e, "moov") && child(f, &s, &e, "udta") && child(f, &s, &e, "chpl"));
    rename_box(f, s - 8, "free");
}

static void run_foreign(layout lay)
{
    const list l1 = make_list("One", "Two", "Three", 300, 600);
    const list l2 = make_list("Intro", "Rest", NULL, 400, 0);
    for (int shape = 0; shape < 2; shape++) {
        mem_file f = { 0 };
        mux(&f, &l1, lay);
        if (shape == 0) drop_text(&f); else drop_chpl(&f);
        list got;
        read_list(&f, &got);
        /* Either form alone is a complete list, except that a fragmented file is read through chpl. */
        if (lay == LAYOUT_FRAGMENTED && shape == 1) CHECK(got.n == 0);
        else CHECK(same(&got, &l1));
        check_audio(&f);
        update(&f, &l2);        /* the missing form is added back and no second chapter trak appears */
        verify(&f, &l2, lay);
        free(f.buf);
    }
}

/* ---- fragmented files that other muxers write, and ones that never finished ---- */

/* FFmpeg names fragment data by absolute position and ends the file with an mfra index. A chapter
 * update must leave every sample reachable, add both forms, and not leave an index that no longer
 * ends the file. A build without fragmented support refuses the file and leaves it as it was. */
static void run_ffmpeg_style(void)
{
    const list l2 = make_list("Intro", "Rest", NULL, 400, 0);
    mem_file f;
    build(&f, 1);
    check_layout(&f);
#if FAAM_MUXER_FRAGMENTED
    update(&f, &l2);
    list a, b;
    raw_chpl(&f, &a);
    raw_text(&f, &b);
    CHECK(same(&a, &l2) && same(&b, &l2));
    check_samples(&f);
    CHECK(find_box(&f, "mfra", 0, f.len) == (size_t)-1);
#else
    uint8_t *before = (uint8_t *)malloc(f.len);
    size_t len = f.len;
    memcpy(before, f.buf, len);
    faam_chapter ch[2] = { { sizeof(faam_chapter), 0, "Intro" }, { sizeof(faam_chapter), 400, "Rest" } };
    f.pos = 0;
    faam_io io = mem_file_io(&f);
    CHECK(faam_update_chapters_stream(&io, ch, 2, 0) == FAAM_ERR_UNSUPPORTED);
    CHECK(f.len == len && !memcmp(f.buf, before, len));
    free(before);
    (void)l2;
#endif
    free(f.buf);
}

#if FAAM_MUXER_FRAGMENTED
/* A recording that never finalized ends in an open fragment; appending there would overwrite it. */
static void run_unfinished(void)
{
    const list l1 = make_list("One", "Two", "Three", 300, 600);
    mem_file f = { 0 };
    mux_ex(&f, &l1, LAYOUT_FRAGMENTED, false);
    uint8_t *before = (uint8_t *)malloc(f.len);
    size_t len = f.len;
    memcpy(before, f.buf, len);
    f.pos = 0;
    faam_io io = mem_file_io(&f);
    faam_chapter ch = { .struct_size = sizeof(ch), .start_ms = 0, .title = "x" };
    CHECK(faam_update_chapters_stream(&io, &ch, 1, 0) == FAAM_ERR_UNSUPPORTED);
    CHECK(f.len == len && !memcmp(f.buf, before, len));
    free(before);
    free(f.buf);
}
#endif

/* ---- when the text track is not trusted, chpl serves ---- */

static void chapter_stbl(const mem_file *f, const char *name, size_t *s, size_t *e)
{
    size_t ts, te;
    uint32_t id;
    CHECK(chapter_trak(f, &ts, &te, &id));
    *s = ts; *e = te;
    CHECK(child(f, s, e, "mdia") && child(f, s, e, "minf") && child(f, s, e, "stbl") && child(f, s, e, name));
}

static void put32_at(mem_file *f, size_t at, uint32_t v)
{
    f->buf[at] = (uint8_t)(v >> 24); f->buf[at + 1] = (uint8_t)(v >> 16); f->buf[at + 2] = (uint8_t)(v >> 8); f->buf[at + 3] = (uint8_t)v;
}

static void run_fallback(void)
{
    const list l1 = make_list("One", "Two", "Three", 300, 600);
    list changed = l1;
    changed.e[0].title[0] = 'X'; /* chpl says "Xne": it is chpl that answered */
    for (int mode = 0; mode < 7; mode++) {
        mem_file f = { 0 };
        mux(&f, &l1, LAYOUT_MOOV_LAST);
        size_t s = 0, e = f.len;
        CHECK(child(&f, &s, &e, "moov") && child(&f, &s, &e, "udta") && child(&f, &s, &e, "chpl"));
        f.buf[s + 9 + 9] = 'X';
        size_t z, y;
        uint64_t lead = 0;
        switch (mode) {
        case 0: break;                                                 /* intact: the text track answers */
        case 1: chapter_stbl(&f, "stsz", &z, &y); put32_at(&f, z + 8, 0); break;          /* no samples */
        case 2: chapter_stbl(&f, "stco", &z, &y); put32_at(&f, z + 4, 0); break;          /* chunks missing: fewer samples than declared */
        case 3: {                                                      /* a sample longer than its bytes */
            size_t o;
            chapter_stbl(&f, "stco", &z, &y);
            o = mem_be32(f.buf + z + 8);
            f.buf[o] = 0x7f;
            break;
        }
        case 4: {                                                      /* a trimmed text track */
            size_t ts, te;
            uint32_t id;
            CHECK(chapter_trak(&f, &ts, &te, &id));
            size_t a = ts, b = te;
            CHECK(child(&f, &a, &b, "edts") && child(&f, &a, &b, "elst"));
            put32_at(&f, a + 8 + 4, 1000); /* media_time of the only entry */
            break;
        }
        case 5: chapter_stbl(&f, "stsd", &z, &y); memcpy(f.buf + z + 12, "tx3g", 4); break; /* a text entry it accepts */
        default: {                                                     /* a leading empty edit is a lead-in */
            size_t ts, te;
            uint32_t id;
            CHECK(chapter_trak(&f, &ts, &te, &id));
            size_t a = ts, b = te;
            CHECK(child(&f, &a, &b, "edts") && child(&f, &a, &b, "elst"));
            lead = mem_be32(f.buf + a + 8) * 1000ull;
            put32_at(&f, a + 8 + 4, 0xFFFFFFFFu);
            size_t m = 0, me = f.len;
            CHECK(child(&f, &m, &me, "moov") && child(&f, &m, &me, "mvhd"));
            lead /= mem_be32(f.buf + m + 12);
            break;
        }
        }
        list got;
        read_list(&f, &got);
        bool text_answered = mode == 0 || mode == 5 || mode == 6;
        if (mode == 6) {
            CHECK(lead > 0);
            list shifted = l1;
            for (uint32_t i = 0; i < shifted.n; i++) shifted.e[i].ms += lead;
            CHECK(same(&got, &shifted));
        } else {
            CHECK(same(&got, text_answered ? &l1 : &changed));
        }
        check_audio(&f);
        free(f.buf);
    }
}

#if FAAM_MUXER_CHAPTERS
int main(void)
{
    run_layout(LAYOUT_MOOV_LAST);
    run_layout(LAYOUT_FASTSTART);
    run_foreign(LAYOUT_MOOV_LAST);
    run_foreign(LAYOUT_FASTSTART);
#if FAAM_MUXER_FRAGMENTED
    run_layout(LAYOUT_FRAGMENTED);
    run_foreign(LAYOUT_FRAGMENTED);
    run_unfinished();
#endif
    run_ffmpeg_style();
    run_fallback();
    return 0;
}
#else
/* Without muxer-chapters nothing is written or read; both entry points say so. */
int main(void)
{
    (void)run_layout; (void)run_foreign; (void)run_fallback; (void)run_ffmpeg_style;
#if FAAM_MUXER_FRAGMENTED
    (void)run_unfinished;
#endif
    faam_chapter ch = { .struct_size = sizeof(ch), .start_ms = 0, .title = "One" };
    faam_muxer_config cfg;
    CHECK(faam_muxer_config_init(&cfg, sizeof(cfg)) == FAAM_OK);
    faam_track_config tc;
    CHECK(faam_track_config_init(&tc, sizeof(tc)) == FAAM_OK);
    tc.track_type = FAAM_TRACK_AUDIO; tc.codec_id = FAAM_CODEC_AAC; tc.track_id = 1;
    tc.timescale = tc.sample_rate = 44100; tc.channels = 2;
    tc.codec_data = asc_lc_44k; tc.codec_data_len = sizeof(asc_lc_44k);
    cfg.num_tracks = 1;
    cfg.tracks = &tc;
    cfg.chapters = &ch;
    cfg.num_chapters = 1;
    mem_file f = { 0 };
    faam_io io = mem_file_io(&f);
    faam_muxer *m = NULL;
    CHECK(faam_muxer_open(&cfg, &io, &m) == FAAM_ERR_NOT_BUILT);
    CHECK(faam_update_chapters_stream(&io, &ch, 1, 0) == FAAM_ERR_NOT_BUILT);
    free(f.buf);
    return 0;
}
#endif
