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

/* FAAM_MUXER_FASTSTART: the same frames muxed with and without the flag must demux to the
 * same tracks, payloads, tags and chapters; only the chunk offsets and the box order differ.
 * Also covers in-place edits of a moov-first file, the failure paths of the copy, and the
 * mp4v (MPEG-4 Part 2) demux details that live next to the other visual sample entries. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "faam.h"

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

typedef struct {
    uint8_t *buf;
    size_t len, cap, pos;
    uint64_t written;
    uint64_t fail_write_at; /* 0: never; else a write that would pass this many total bytes fails */
    bool fail_read;
} memfile;

static int32_t mem_read(void *user, void *out, uint32_t n) {
    memfile *f = (memfile *)user;
    if (f->fail_read) return -1;
    size_t avail = f->pos < f->len ? f->len - f->pos : 0;
    if (n > avail) n = (uint32_t)avail;
    memcpy(out, f->buf + f->pos, n);
    f->pos += n;
    return (int32_t)n;
}

static int32_t mem_write(void *user, const void *data, uint32_t n) {
    memfile *f = (memfile *)user;
    if (f->fail_write_at && f->written + n > f->fail_write_at) return -1;
    f->written += n;
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

static bool mem_seek(void *user, uint64_t pos) { ((memfile *)user)->pos = (size_t)pos; return true; }
static uint64_t mem_tell(void *user) { return ((memfile *)user)->pos; }

static faam_io mem_io(memfile *f, bool with_read) {
    faam_io io = { .struct_size = sizeof(faam_io), .user_data = f, .read = with_read ? mem_read : NULL, .write = mem_write, .seek = mem_seek, .tell = mem_tell, .flush = NULL };
    return io;
}

static uint32_t be32(const uint8_t *b) {
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
}

static bool have_feature(uint32_t bit) {
    faam_library_info info = { .struct_size = sizeof(info) };
    CHECK(faam_get_library_info(&info) == FAAM_OK);
    return (info.flags & bit) != 0;
}

/* Top-level box order: where moov and mdat start, and moov's size. */
static void top_level(const memfile *f, size_t *moov, size_t *mdat, size_t *moov_size) {
    size_t o = 0;
    *moov = *mdat = *moov_size = 0;
    while (o + 8 <= f->len) {
        uint64_t n = be32(f->buf + o);
        CHECK(n >= 8 && o + n <= f->len);
        if (!memcmp(f->buf + o + 4, "moov", 4)) { *moov = o; *moov_size = (size_t)n; }
        if (!memcmp(f->buf + o + 4, "mdat", 4)) *mdat = o;
        o += (size_t)n;
    }
    CHECK(o == f->len && *moov && *mdat);
}

#define MAX_FRAMES 700
typedef struct {
    uint32_t track, size, duration, hash;
    int32_t cts;
    bool key;
    uint64_t offset;
} frame_rec;

typedef struct {
    uint32_t num_tracks;
    faam_track_info info[2];
    uint8_t codec[2][64];
    uint32_t codec_len[2];
    frame_rec frames[MAX_FRAMES];
    unsigned num_frames;
    char title[128];
    char artist[32];
    uint32_t num_chapters;
    uint64_t chapter_ms[4];
    char chapter_title[4][32];
    faam_gapless_info gapless;
} snapshot;

static void snap(memfile *f, snapshot *s) {
    memset(s, 0, sizeof(*s));
    f->pos = 0;
    faam_io io = mem_io(f, true);
    faam_demuxer *d;
    CHECK(faam_demuxer_open(NULL, &io, &d) == FAAM_OK);
    uint32_t held;
    CHECK(faam_demuxer_get_num_tracks(d, &held, NULL) == FAAM_OK && held <= 2);
    s->num_tracks = held;
    for (uint32_t i = 0; i < held; i++) {
        s->info[i].struct_size = sizeof(s->info[i]);
        CHECK(faam_demuxer_get_track_info(d, i, &s->info[i]) == FAAM_OK);
        CHECK(faam_demuxer_get_codec_data(d, s->info[i].track_id, s->codec[i], sizeof(s->codec[i]),
                                          &s->codec_len[i]) == FAAM_OK);
    }
    uint32_t meta_count = 0;
    if (faam_demuxer_get_num_metadata(d, &meta_count) == FAAM_OK) {
        for (uint32_t i = 0; i < meta_count; i++) {
            faam_metadata_entry entry = { .struct_size = sizeof(entry) };
            if (faam_demuxer_get_metadata_entry(d, i, &entry) != FAAM_OK ||
                entry.data_type != FAAM_METADATA_UTF8 || memchr(entry.data, 0, entry.data_bytes)) continue;
            if (entry.key == FAAM_METADATA_KEY_TITLE) snprintf(s->title, sizeof(s->title), "%.*s", (int)entry.data_bytes, (const char *)entry.data);
            if (entry.key == FAAM_METADATA_KEY_ARTIST) snprintf(s->artist, sizeof(s->artist), "%.*s", (int)entry.data_bytes, (const char *)entry.data);
        }
    }
    CHECK(faam_demuxer_get_num_chapters(d, &s->num_chapters) == FAAM_OK && s->num_chapters <= 4);
    for (uint32_t i = 0; i < s->num_chapters; i++) {
        faam_chapter c = { .struct_size = sizeof(c) };
        CHECK(faam_demuxer_get_chapter(d, i, &c) == FAAM_OK);
        s->chapter_ms[i] = c.start_ms;
        snprintf(s->chapter_title[i], sizeof(s->chapter_title[i]), "%s", c.title);
    }
    s->gapless.struct_size = sizeof(s->gapless);
    CHECK(faam_demuxer_get_track_gapless(d, 0, &s->gapless) == FAAM_OK);

    faam_frame_loc loc = { .struct_size = sizeof(loc) };
    uint8_t buf[512];
    while (faam_demuxer_next_frame_loc(d, &loc) == FAAM_OK) {
        CHECK(s->num_frames < MAX_FRAMES);
        frame_rec *r = &s->frames[s->num_frames++];
        uint32_t got;
        CHECK(loc.frame_bytes <= sizeof(buf));
        CHECK(faam_demuxer_read_frame(d, buf, sizeof(buf), &got) == FAAM_OK && got == loc.frame_bytes);
        r->track = loc.track_id; r->size = loc.frame_bytes; r->duration = loc.duration_ticks;
        r->cts = loc.cts_offset; r->key = !!(loc.flags & FAAM_FRAME_KEYFRAME); r->offset = loc.file_offset;
        r->hash = 2166136261u;
        for (uint32_t i = 0; i < got; i++) r->hash = (r->hash ^ buf[i]) * 16777619u;
    }
    faam_demuxer_close(&d);
}

/* Everything but the chunk offsets must match; returns the common offset shift. */
static uint64_t same_content(const snapshot *a, const snapshot *b) {
    CHECK(a->num_tracks == b->num_tracks);
    for (uint32_t i = 0; i < a->num_tracks; i++) {
        const faam_track_info *x = &a->info[i], *y = &b->info[i];
        CHECK(x->track_id == y->track_id && x->track_type == y->track_type && x->codec_id == y->codec_id);
        CHECK(x->timescale == y->timescale && x->sample_rate == y->sample_rate && x->channels == y->channels);
        CHECK(x->total_frames == y->total_frames && x->total_duration == y->total_duration);
        CHECK(x->max_frame_bytes == y->max_frame_bytes && x->fourcc == y->fourcc);
        CHECK(x->width == y->width && x->height == y->height);
        CHECK(a->codec_len[i] == b->codec_len[i] && !memcmp(a->codec[i], b->codec[i], a->codec_len[i]));
    }
    CHECK(!strcmp(a->title, b->title) && !strcmp(a->artist, b->artist));
    CHECK(a->num_chapters == b->num_chapters);
    for (uint32_t i = 0; i < a->num_chapters; i++)
        CHECK(a->chapter_ms[i] == b->chapter_ms[i] && !strcmp(a->chapter_title[i], b->chapter_title[i]));
    CHECK(a->gapless.encoder_delay == b->gapless.encoder_delay && a->gapless.end_padding == b->gapless.end_padding &&
          a->gapless.total_samples == b->gapless.total_samples);
    CHECK(a->num_frames == b->num_frames && a->num_frames > 0);
    uint64_t shift = b->frames[0].offset - a->frames[0].offset;
    for (unsigned i = 0; i < a->num_frames; i++) {
        const frame_rec *x = &a->frames[i], *y = &b->frames[i];
        CHECK(x->track == y->track && x->size == y->size && x->duration == y->duration);
        CHECK(x->cts == y->cts && x->key == y->key && x->hash == y->hash);
        CHECK(y->offset - x->offset == shift);
    }
    return shift;
}

static const uint8_t k_asc[] = { 0x12, 0x10 };
static const uint8_t k_avcc[] = { 0x01, 0x64, 0x00, 0x1F, 0xFF, 0xE1, 0x00, 0x02, 0x67, 0x64, 0x01, 0x00, 0x01, 0x68 };

/* Audio, optionally with an interleaved B-frame video track, plus tags, chapters and gapless. */
static faam_status mux(memfile *out, bool faststart, bool video, bool finalize_only_status) {
    faam_muxer_config cfg;
    faam_track_config tracks[2];
    CHECK(faam_muxer_config_init(&cfg, sizeof(cfg)) == FAAM_OK);
    memset(tracks, 0, sizeof(tracks));
    tracks[0].struct_size = sizeof(tracks[0]);
    tracks[0].track_type = FAAM_TRACK_AUDIO;
    tracks[0].codec_id = FAAM_CODEC_AAC;
    tracks[0].timescale = 44100;
    tracks[0].sample_rate = 44100;
    tracks[0].channels = 2;
    tracks[0].codec_data = k_asc;
    tracks[0].codec_data_len = sizeof(k_asc);
    cfg.num_tracks = 1;
    if (video) {
        tracks[1].struct_size = sizeof(tracks[1]);
        tracks[1].track_type = FAAM_TRACK_VIDEO;
        tracks[1].codec_id = FAAM_CODEC_H264;
        tracks[1].timescale = 90000;
        tracks[1].width = 64;
        tracks[1].height = 48;
        tracks[1].codec_data = k_avcc;
        tracks[1].codec_data_len = sizeof(k_avcc);
        cfg.num_tracks = 2;
    }
    cfg.tracks = tracks;
    cfg.flags = faststart ? FAAM_MUXER_FASTSTART : 0;
    cfg.creation_time = 3000000000u;
    faam_metadata_entry entries[] = {
        { .struct_size = sizeof(faam_metadata_entry), .key = FAAM_METADATA_KEY_TITLE,
          .data_type = FAAM_METADATA_UTF8, .data = "Faststart", .data_bytes = sizeof("Faststart") - 1 },
        { .struct_size = sizeof(faam_metadata_entry), .key = FAAM_METADATA_KEY_ARTIST,
          .data_type = FAAM_METADATA_UTF8, .data = "Test", .data_bytes = sizeof("Test") - 1 },
    };

    cfg.metadata = entries; cfg.num_metadata = 2;
    faam_chapter chapters[2] = {
        { .struct_size = sizeof(faam_chapter), .start_ms = 0, .title = "One" },
        { .struct_size = sizeof(faam_chapter), .start_ms = 1500, .title = "Two" },
    };
    cfg.chapters = chapters;
    cfg.num_chapters = 2;
    faam_gapless_info gapless = { sizeof(faam_gapless_info), 1024, 512, 0 };
    cfg.gapless = &gapless;

    faam_io io = mem_io(out, true);
    faam_muxer *m;
    faam_status st = faam_muxer_open(&cfg, &io, &m);
    if (st != FAAM_OK) return st;
    uint32_t atrack, vtrack = 0;
    CHECK(faam_muxer_get_track_id(m, 0, &atrack) == FAAM_OK);
    if (video) CHECK(faam_muxer_get_track_id(m, 1, &vtrack) == FAAM_OK);

    uint8_t frame[256];
    unsigned v = 0;
    for (unsigned i = 0; i < 200; i++) {
        uint32_t size = 100 + (i * 7) % 90;
        memset(frame, (int)(i * 3 + 1), sizeof(frame));
        CHECK(faam_muxer_write_frame(m, atrack, frame, size, 1024, 0, FAAM_FRAME_KEYFRAME) == FAAM_OK);
        if (video && i % 2 == 0) {
            memset(frame, (int)(0x80 + v), sizeof(frame));
            CHECK(faam_muxer_write_frame(m, vtrack, frame, 40 + v % 13, 3000, v % 3 ? 3000 : 0,
                                         v % 12 == 0 ? FAAM_FRAME_KEYFRAME : 0) == FAAM_OK);
            v++;
        }
    }
    st = faam_muxer_finalize(m);
    faam_status closed = faam_muxer_close(&m);
    return finalize_only_status ? st : (st != FAAM_OK ? st : closed);
}

static void check_pair(bool video) {
    memfile plain = {0}, fast = {0};
    CHECK(mux(&plain, false, video, false) == FAAM_OK);
    CHECK(mux(&fast, true, video, false) == FAAM_OK);

    size_t moov, mdat, moov_size, moov_p, mdat_p, size_p;
    top_level(&plain, &moov_p, &mdat_p, &size_p);
    top_level(&fast, &moov, &mdat, &moov_size);
    CHECK(mdat_p < moov_p);   /* the default layout is unchanged: moov last */
    CHECK(moov < mdat);       /* faststart: moov first, directly followed by mdat */
    CHECK(moov + moov_size == mdat);
    CHECK(fast.len == plain.len);

    snapshot a, b;
    snap(&plain, &a);
    snap(&fast, &b);
    CHECK(same_content(&a, &b) == moov_size);

    free(plain.buf);
    free(fast.buf);
}

/* Tag and chapter edits must work on a moov-first file and leave the media bytes alone. */
static void check_update(void) {
    memfile fast = {0};
    CHECK(mux(&fast, true, false, false) == FAAM_OK);
    snapshot before, after;
    snap(&fast, &before);

    static const char replacement[] = "A considerably longer replacement title than the original one, so moov must grow";
    static const char other[] = "Other";
    faam_metadata_entry entries[] = {
        { .struct_size = sizeof(faam_metadata_entry), .key = FAAM_METADATA_KEY_TITLE,
          .data_type = FAAM_METADATA_UTF8, .data = replacement, .data_bytes = sizeof(replacement) - 1 },
        { .struct_size = sizeof(faam_metadata_entry), .key = FAAM_METADATA_KEY_ARTIST,
          .data_type = FAAM_METADATA_UTF8, .data = other, .data_bytes = sizeof(other) - 1 },
    };

    fast.pos = 0;
    faam_io io = mem_io(&fast, true);
    CHECK(faam_update_metadata_stream(&io, entries, 2, 0) == FAAM_OK);
    faam_chapter ch = { .struct_size = sizeof(ch), .start_ms = 0, .title = "Only" };
    fast.pos = 0;
    CHECK(faam_update_chapters_stream(&io, &ch, 1, 0) == FAAM_OK);

    snap(&fast, &after);
    CHECK(!strcmp(after.title, replacement));
    CHECK(!strcmp(after.artist, "Other"));
    CHECK(after.num_chapters == 1 && !strcmp(after.chapter_title[0], "Only"));
    CHECK(after.num_frames == before.num_frames);
    for (unsigned i = 0; i < before.num_frames; i++)
        CHECK(after.frames[i].size == before.frames[i].size && after.frames[i].hash == before.frames[i].hash);
    free(fast.buf);
}

static void check_errors(void) {
    memfile out = {0};

    /* The flag needs read; it is refused before anything is written. */
    faam_muxer_config cfg;
    faam_track_config t = { .struct_size = sizeof(t), .track_type = FAAM_TRACK_AUDIO, .codec_id = FAAM_CODEC_AAC,
        .timescale = 44100, .sample_rate = 44100, .channels = 2, .codec_data = k_asc, .codec_data_len = sizeof(k_asc) };
    CHECK(faam_muxer_config_init(&cfg, sizeof(cfg)) == FAAM_OK);
    cfg.tracks = &t;
    cfg.num_tracks = 1;
    cfg.flags = FAAM_MUXER_FASTSTART;
    faam_io write_only = mem_io(&out, false);
    faam_muxer *m = (faam_muxer *)&out;
    CHECK(faam_muxer_open(&cfg, &write_only, &m) == FAAM_ERR_UNSUPPORTED && m == NULL);
    CHECK(out.written == 0);

    /* Unknown flag bits are refused; so is faststart on a fragmented file. */
    cfg.flags = 0x8;
    faam_io io = mem_io(&out, true);
    CHECK(faam_muxer_open(&cfg, &io, &m) == FAAM_ERR_INVALID_ARGUMENT && m == NULL);
    cfg.flags = FAAM_MUXER_FASTSTART;
    cfg.fragment_ms = 1000;
    CHECK(faam_muxer_open(&cfg, &io, &m) ==
          (have_feature(FAAM_LIB_FRAGMENTED) ? FAAM_ERR_INVALID_ARGUMENT : FAAM_ERR_NOT_BUILT) && m == NULL);
    free(out.buf);

    /* A write that fails inside the copy and inside the final moov is sticky. */
    memfile clean = {0};
    CHECK(mux(&clean, true, false, false) == FAAM_OK);
    uint64_t total = clean.written;
    free(clean.buf);
    uint64_t points[2] = { total * 3 / 4, total - 10 };
    for (unsigned i = 0; i < 2; i++) {
        memfile f = {0};
        f.fail_write_at = points[i];
        faam_status st = mux(&f, true, false, true);
        CHECK(st == FAAM_ERR_IO_WRITE);
        free(f.buf);
        f = (memfile){0};
        f.fail_write_at = points[i];
        CHECK(mux(&f, true, false, false) == FAAM_ERR_IO_WRITE);
        free(f.buf);
    }

    /* A read that fails during the copy is a read error, not a write error. */
    memfile r = {0};
    r.fail_read = true;
    CHECK(mux(&r, true, false, true) == FAAM_ERR_IO_READ);
    free(r.buf);
    r = (memfile){0};
    r.fail_read = true;
    CHECK(mux(&r, false, false, true) == FAAM_OK); /* the default path never reads */
    free(r.buf);
}

/* mp4v is a generic visual entry: it reports its size and its esds VOL header, nothing more. */
static void check_mp4v(void) {
    static const uint8_t vol[6] = { 0x00, 0x00, 0x01, 0xB0, 0x01, 0x00 };
    /* An avcC box of this payload size has the same length as the esds below (40 bytes). */
    uint8_t avcc[32] = { 0x01, 0x64, 0x00, 0x1F, 0xFF, 0xE1, 0x00, 0x02, 0x67, 0x64, 0x01, 0x00, 0x01, 0x68 };

    memfile f = {0};
    faam_muxer_config cfg;
    faam_track_config t = { .struct_size = sizeof(t), .track_type = FAAM_TRACK_VIDEO, .codec_id = FAAM_CODEC_H264,
        .timescale = 90000, .width = 64, .height = 48, .codec_data = avcc, .codec_data_len = sizeof(avcc) };
    CHECK(faam_muxer_config_init(&cfg, sizeof(cfg)) == FAAM_OK);
    cfg.tracks = &t;
    cfg.num_tracks = 1;
    faam_io io = mem_io(&f, true);
    faam_muxer *m;
    CHECK(faam_muxer_open(&cfg, &io, &m) == FAAM_OK);
    uint8_t frame[24] = {0};
    for (unsigned i = 0; i < 4; i++)
        CHECK(faam_muxer_write_frame(m, 1, frame, sizeof(frame), 3000, 0, i == 0 ? FAAM_FRAME_KEYFRAME : 0) == FAAM_OK);
    CHECK(faam_muxer_finalize(m) == FAAM_OK);
    faam_muxer_close(&m);

    /* Rewrite the avc1/avcC sample entry as mp4v/esds in place. */
    uint8_t *entry = NULL, *box = NULL;
    for (size_t i = 4; i + 4 <= f.len; i++) {
        if (!entry && !memcmp(f.buf + i, "avc1", 4)) entry = f.buf + i;
        if (!box && !memcmp(f.buf + i, "avcC", 4)) box = f.buf + i - 4;
    }
    CHECK(entry && box && be32(box) == 40);
    memcpy(entry, "mp4v", 4);
    memcpy(box + 4, "esds", 4);
    uint8_t *p = box + 8;
    memset(p, 0, 32);
    *p++ = 0; *p++ = 0; *p++ = 0; *p++ = 0;                 /* version, flags */
    *p++ = 0x03; *p++ = 26;                                  /* ES_Descriptor */
    *p++ = 0; *p++ = 1; *p++ = 0;                            /* ES_ID, flags */
    *p++ = 0x04; *p++ = 21;                                 /* DecoderConfigDescriptor */
    *p++ = 0x20; *p++ = 0x11; p += 3; p += 8;                /* MPEG-4 Visual, video stream, rates */
    *p++ = 0x05; *p++ = sizeof(vol);                         /* DecoderSpecificInfo */
    memcpy(p, vol, sizeof(vol));
    f.pos = 0;
    faam_io rio = mem_io(&f, true);
    faam_demuxer *d;
    CHECK(faam_demuxer_open(NULL, &rio, &d) == FAAM_OK);
    faam_track_info info = { .struct_size = sizeof(info) };
    CHECK(faam_demuxer_get_track_info(d, 0, &info) == FAAM_OK);
    CHECK(info.codec_id == FAAM_CODEC_GENERIC && info.fourcc == be32((const uint8_t *)"mp4v"));
    CHECK(info.width == 64 && info.height == 48 && info.total_frames == 4);
    uint8_t got[32];
    uint32_t len = 0;
    CHECK(faam_demuxer_get_codec_data(d, info.track_id, got, sizeof(got), &len) == FAAM_OK);
    CHECK(len == sizeof(vol) && !memcmp(got, vol, sizeof(vol)));
    faam_demuxer_close(&d);
    free(f.buf);
}

int main(void) {
    check_pair(false);
    check_update();
    check_errors();
    if (have_feature(FAAM_LIB_VIDEO)) {
        check_pair(true);
        check_mp4v();
    }
    puts("faststart: layout, content equality, edits, failures and mp4v passed");
    return 0;
}
