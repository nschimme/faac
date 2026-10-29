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
 * Video (ctts, Annex-B) and fragmented-MP4 tests over a memory-backed faam_io.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "faam.h"
#include "../../tests/faam_test_helpers.h"

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

#if defined(FAAM_MUXER_VIDEO) || defined(FAAM_MUXER_FRAGMENTED)
typedef struct {
    uint8_t *buf;
    size_t len, cap, pos;
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

static faam_io mem_io(memfile *f) {
    faam_io io = { .struct_size = sizeof(faam_io), .user_data = f, .read = mem_read, .write = mem_write, .seek = mem_seek, .tell = mem_tell, .flush = NULL };
    return io;
}

static uint32_t be32(const uint8_t *b) {
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
}

#ifdef FAAM_MUXER_VIDEO
static void test_write_be32(uint8_t *b, uint32_t value) {
    b[0] = (uint8_t)(value >> 24);
    b[1] = (uint8_t)(value >> 16);
    b[2] = (uint8_t)(value >> 8);
    b[3] = (uint8_t)value;
}

/* First occurrence of a four-character code at or after `from`; the tests
 * only look for box names that cannot occur in the synthetic payloads. */
static const uint8_t *find_box(const memfile *f, const char *name, size_t from) {
    for (size_t i = from; i + 4 <= f->len; i++)
        if (!memcmp(f->buf + i, name, 4)) return f->buf + i;
    return NULL;
}
#endif

static faam_muxer *open_muxer(const faam_muxer_config *cfg, const faam_io *io) {
    faam_muxer *m;
    CHECK(faam_muxer_open(cfg, io, &m) == FAAM_OK);
    return m;
}
#endif

#if defined(FAAM_MUXER_VIDEO) || defined(FAAM_MUXER_FRAGMENTED)
/* Decode order I P B B P B B at 3000 ticks per frame, display delayed by two frames. */
static const int32_t k_cts[7] = { 6000, 12000, 3000, 3000, 12000, 3000, 3000 };

#endif

#ifdef FAAM_MUXER_VIDEO
static const uint8_t k_avcc[] = { 0x01, 0x64, 0x00, 0x1F, 0xFF, 0xE1, 0x00, 0x02, 0x67, 0x64, 0x01, 0x00, 0x01, 0x68 };

static faam_track_config video_track(void) {
    faam_track_config t;
    memset(&t, 0, sizeof(t));
    t.struct_size = sizeof(t);
    t.track_type = FAAM_TRACK_VIDEO;
    t.codec_id = FAAM_CODEC_H264;
    t.timescale = 90000;
    t.width = 640;
    t.height = 360;
    t.codec_data = k_avcc;
    t.codec_data_len = sizeof(k_avcc);
    return t;
}

static void mux_video(memfile *out, const int32_t *cts, unsigned n) {
    faam_muxer_config cfg;
    faam_track_config cfg_tracks[8];
    CHECK(faam_muxer_config_init(&cfg, sizeof(cfg)) == FAAM_OK);
    faam_track_config t = video_track();
    CHECK(test_append_track(&cfg, cfg_tracks, &t, NULL) == FAAM_OK);
    faam_io io = mem_io(out);
    faam_muxer *m = open_muxer(&cfg, &io);
    for (unsigned i = 0; i < n; i++) {
        uint8_t frame[40];
        memset(frame, (int)(0x30 + i), sizeof(frame));
        CHECK(faam_muxer_write_frame(m, 1, frame, sizeof(frame), 3000, cts ? cts[i] : 0, i == 0 ? FAAM_FRAME_KEYFRAME : 0) == FAAM_OK);
    }
    CHECK(faam_muxer_finalize(m) == FAAM_OK);
    faam_muxer_close(&m);
}

static void check_video_roundtrip(memfile *f, const int32_t *cts, unsigned n) {
    faam_io io = mem_io(f);
    faam_demuxer *d;
    CHECK(faam_demuxer_open(NULL, &io, &d) == FAAM_OK);
    faam_frame_loc loc; loc.struct_size = sizeof(loc);
    unsigned i = 0;
    uint8_t frame[64];
    uint32_t bytes;
    while (faam_demuxer_next_frame_loc(d, &loc) == FAAM_OK) {
        CHECK(i < n);
        CHECK(loc.cts_offset == (cts ? cts[i] : 0));
        CHECK(loc.duration_ticks == 3000 && !!(loc.flags & FAAM_FRAME_KEYFRAME) == (i == 0));
        CHECK(faam_demuxer_read_frame(d, frame, sizeof(frame), &bytes) == FAAM_OK && bytes == 40);
        CHECK(frame[0] == 0x30 + i && frame[39] == 0x30 + i);
        i++;
    }
    CHECK(i == n);
    faam_demuxer_close(&d);
}

/* An absent stss declares every video sample to be a sync sample. */
static void test_absent_stss(void) {
    memfile f = {0};
    mux_video(&f, NULL, 1);
    const uint8_t *stss = find_box(&f, "stss", 0);
    CHECK(stss);
    memcpy(f.buf + (stss - f.buf), "free", 4);
    check_video_roundtrip(&f, NULL, 1);
    free(f.buf);
}

/* An explicitly empty stss means that none of the video samples are sync samples. */
static void test_empty_stss(void) {
    memfile f = {0};
    mux_video(&f, NULL, 1);
    const uint8_t *stss = find_box(&f, "stss", 0);
    CHECK(stss);
    uint8_t *mutable_stss = f.buf + (stss - f.buf) - 4;
    uint32_t size = be32(mutable_stss);
    CHECK(size == 20);
    /* Remove the sole sync sample, keeping the containing MP4 well-formed. */
    const char *parents[] = { "stbl", "minf", "mdia", "trak", "moov" };
    for (size_t i = 0; i < sizeof(parents) / sizeof(parents[0]); i++) {
        const uint8_t *parent = find_box(&f, parents[i], 0);
        CHECK(parent);
        uint8_t *mutable_parent = f.buf + (parent - f.buf) - 4;
        test_write_be32(mutable_parent, be32(mutable_parent) - 4);
    }
    test_write_be32(mutable_stss, 16);
    test_write_be32(mutable_stss + 12, 0);
    size_t tail = (size_t)(mutable_stss - f.buf) + size;
    size_t remove = size - 16;
    memmove(mutable_stss + 16, f.buf + tail, f.len - tail);
    f.len -= remove;
    faam_io io = mem_io(&f);
    faam_demuxer *d;
    CHECK(faam_demuxer_open(NULL, &io, &d) == FAAM_OK);
    faam_frame_loc loc = { .struct_size = sizeof(loc) };
    CHECK(faam_demuxer_next_frame_loc(d, &loc) == FAAM_OK);
    CHECK(!(loc.flags & FAAM_FRAME_KEYFRAME));
    faam_demuxer_close(&d);
    free(f.buf);
}

static void test_ctts(void) {
    memfile f = {0};

    /* IP-only: no ctts and no edit list at all. */
    mux_video(&f, NULL, 7);
    CHECK(!find_box(&f, "ctts", 0) && !find_box(&f, "edts", 0));
    check_video_roundtrip(&f, NULL, 7);

    /* B-frames: ctts v0 with run-length entries, edit list starts at the first displayed frame. */
    free(f.buf);
    memset(&f, 0, sizeof(f));
    mux_video(&f, k_cts, 7);
    const uint8_t *ctts = find_box(&f, "ctts", 0);
    CHECK(ctts && ctts[4] == 0);
    CHECK(be32(ctts + 8) == 5);                       /* 6000, 12000, 3000x2, 12000, 3000x2 */
    CHECK(be32(ctts + 12) == 1 && be32(ctts + 16) == 6000);
    CHECK(be32(ctts + 28) == 2 && be32(ctts + 32) == 3000);
    const uint8_t *elst = find_box(&f, "elst", 0);
    CHECK(elst && be32(elst + 8) == 1);
    CHECK(be32(elst + 12) == (7 * 3000 - 6000) / 90); /* movie timescale is 1000 for video-only */
    CHECK(be32(elst + 16) == 6000);
    check_video_roundtrip(&f, k_cts, 7);

    /* Negative offsets switch to ctts version 1 and need no edit (earliest PTS is 0). */
    int32_t neg[7];
    for (unsigned i = 0; i < 7; i++) neg[i] = k_cts[i] - 6000;
    free(f.buf);
    memset(&f, 0, sizeof(f));
    mux_video(&f, neg, 7);
    ctts = find_box(&f, "ctts", 0);
    CHECK(ctts && ctts[4] == 1 && !find_box(&f, "edts", 0));
    check_video_roundtrip(&f, neg, 7);
    free(f.buf);

    /* Offsets are a video concept. */
    faam_muxer_config cfg;
    faam_track_config cfg_tracks[8];
    CHECK(faam_muxer_config_init(&cfg, sizeof(cfg)) == FAAM_OK);
    faam_track_config a;
    memset(&a, 0, sizeof(a)); a.struct_size = sizeof(a);
    uint8_t asc[2] = { 0x12, 0x10 };
    a.track_type = FAAM_TRACK_AUDIO; a.codec_id = FAAM_CODEC_AAC; a.timescale = a.sample_rate = 44100;
    a.channels = 2; a.codec_data = asc; a.codec_data_len = 2;
    CHECK(test_append_track(&cfg, cfg_tracks, &a, NULL) == FAAM_OK);
    memfile sink = {0};
    faam_io io = mem_io(&sink);
    faam_muxer *m = open_muxer(&cfg, &io);
    uint8_t byte = 1;
    CHECK(faam_muxer_write_frame(m, 1, &byte, 1, 1024, 5, FAAM_FRAME_KEYFRAME) == FAAM_ERR_INVALID_ARGUMENT);
    faam_muxer_close(&m);
    free(sink.buf);
}

static const uint8_t k_sps[] = { 0x67, 0x64, 0x00, 0x1F, 0xAC, 0xD9, 0x40, 0x50, 0x05, 0xBB, 0x01, 0x10,
                                 0x00, 0x00, 0x03, 0x00, 0x10, 0x00, 0x00, 0x03, 0x03, 0xC0, 0xF1, 0x83 };
static const uint8_t k_pps[] = { 0x68, 0xEE, 0x3C, 0x80 };
static const uint8_t k_idr[] = { 0x65, 0x88, 0x84, 0x00, 0x33, 0xFF, 0x00, 0x00, 0x03, 0x01, 0x55 };
static const uint8_t k_slice[] = { 0x41, 0x9A, 0x02, 0x11, 0x22, 0x33, 0x44 };

/* Appends a start code and a NAL unit; `three` selects 00 00 01 over 00 00 00 01. */
static uint32_t annexb_add(uint8_t *out, uint32_t pos, bool three, const uint8_t *nal, uint32_t n) {
    if (!three) out[pos++] = 0;
    out[pos++] = 0; out[pos++] = 0; out[pos++] = 1;
    memcpy(out + pos, nal, n);
    return pos + n;
}

static uint32_t avcc_add(uint8_t *out, uint32_t pos, const uint8_t *nal, uint32_t n) {
    out[pos] = 0; out[pos + 1] = 0; out[pos + 2] = (uint8_t)(n >> 8); out[pos + 3] = (uint8_t)n;
    memcpy(out + pos + 4, nal, n);
    return pos + 4 + n;
}

static void test_annexb(void) {
    faam_track_config t = video_track();
    t.codec_data = NULL;
    t.codec_data_len = 0;
    faam_muxer_config cfg;
    faam_track_config cfg_tracks[8];
    CHECK(faam_muxer_config_init(&cfg, sizeof(cfg)) == FAAM_OK);
    CHECK(test_append_track(&cfg, cfg_tracks, &t, NULL) == FAAM_ERR_INVALID_ARGUMENT);   /* avcC needed without Annex-B */
    t.flags = FAAM_TRACK_ANNEXB;
    t.codec_id = FAAM_CODEC_H265;
    CHECK(test_append_track(&cfg, cfg_tracks, &t, NULL) == FAAM_ERR_INVALID_ARGUMENT);   /* hvcC cannot be derived */
    t.codec_id = FAAM_CODEC_H264;
    CHECK(test_append_track(&cfg, cfg_tracks, &t, NULL) == FAAM_OK);

    /* AU 1: SPS PPS IDR with mixed 4/3-byte start codes; AU 2: slice with trailing zero bytes. */
    uint8_t au1[128], au2[64], want1[128], want2[64];
    uint32_t n1 = 0, n2 = 0, w1 = 0, w2 = 0;
    n1 = annexb_add(au1, n1, false, k_sps, sizeof(k_sps));
    n1 = annexb_add(au1, n1, false, k_pps, sizeof(k_pps));
    n1 = annexb_add(au1, n1, true, k_idr, sizeof(k_idr));
    w1 = avcc_add(want1, w1, k_sps, sizeof(k_sps));
    w1 = avcc_add(want1, w1, k_pps, sizeof(k_pps));
    w1 = avcc_add(want1, w1, k_idr, sizeof(k_idr));
    n2 = annexb_add(au2, n2, true, k_slice, sizeof(k_slice));
    au2[n2++] = 0; au2[n2++] = 0;
    w2 = avcc_add(want2, w2, k_slice, sizeof(k_slice));

    memfile f = {0};
    faam_io io = mem_io(&f);
    faam_muxer *m = open_muxer(&cfg, &io);
    uint8_t not_annexb[8] = { 0, 0, 0, 5, 0x65, 1, 2, 3 };
    CHECK(faam_muxer_write_frame(m, 1, not_annexb, sizeof(not_annexb), 3000, 0, FAAM_FRAME_KEYFRAME) == FAAM_ERR_INVALID_ARGUMENT);
    uint8_t junk_first[6] = { 9, 0, 0, 1, 0x65, 1 };
    CHECK(faam_muxer_write_frame(m, 1, junk_first, sizeof(junk_first), 3000, 0, FAAM_FRAME_KEYFRAME) == FAAM_ERR_INVALID_ARGUMENT);
    CHECK(faam_muxer_write_frame(m, 1, au1, n1, 3000, 0, FAAM_FRAME_KEYFRAME) == FAAM_OK);
    CHECK(faam_muxer_write_frame(m, 1, au2, n2, 3000, 0, 0) == FAAM_OK);
    CHECK(faam_muxer_finalize(m) == FAAM_OK);
    faam_muxer_close(&m);

    uint8_t avcc[64];
    uint32_t a = 0;
    avcc[a++] = 1; avcc[a++] = k_sps[1]; avcc[a++] = k_sps[2]; avcc[a++] = k_sps[3];
    avcc[a++] = 0xFF; avcc[a++] = 0xE1; avcc[a++] = 0; avcc[a++] = sizeof(k_sps);
    memcpy(avcc + a, k_sps, sizeof(k_sps)); a += sizeof(k_sps);
    avcc[a++] = 1; avcc[a++] = 0; avcc[a++] = sizeof(k_pps);
    memcpy(avcc + a, k_pps, sizeof(k_pps)); a += sizeof(k_pps);
    const uint8_t *box = find_box(&f, "avcC", 0);
    CHECK(box && be32(box - 4) == 8 + a && !memcmp(box + 4, avcc, a));

    io = mem_io(&f);
    faam_demuxer *d;
    CHECK(faam_demuxer_open(NULL, &io, &d) == FAAM_OK);
    uint8_t got[64];
    uint32_t len;
    CHECK(faam_demuxer_get_codec_data(d, 1, got, sizeof(got), &len) == FAAM_OK && len == a && !memcmp(got, avcc, a));
    CHECK(faam_demuxer_read_frame(d, got, sizeof(got), &len) == FAAM_OK && len == w1 && !memcmp(got, want1, w1));
    CHECK(faam_demuxer_read_frame(d, got, sizeof(got), &len) == FAAM_OK && len == w2 && !memcmp(got, want2, w2));
    CHECK(faam_demuxer_read_frame(d, got, sizeof(got), &len) == FAAM_END_OF_STREAM);
    faam_demuxer_close(&d);

    /* A stream that never carried parameter sets cannot be finalized. */
    free(f.buf);
    memset(&f, 0, sizeof(f));
    io = mem_io(&f);
    m = open_muxer(&cfg, &io);
    CHECK(faam_muxer_write_frame(m, 1, au2, n2, 3000, 0, FAAM_FRAME_KEYFRAME) == FAAM_OK);
    CHECK(faam_muxer_finalize(m) == FAAM_ERR_INVALID_ARGUMENT);
    faam_muxer_close(&m);
    free(f.buf);
}

/* Exercise packet buffer growth and Annex-B-to-length-prefix conversion with
 * enough tiny NALs to exceed ordinary frame-size assumptions. */
static void test_annexb_many_nals(void) {
    enum { NAL_COUNT = 300000 };
    faam_track_config t = video_track();
    t.codec_data = NULL;
    t.codec_data_len = 0;
    t.flags = FAAM_TRACK_ANNEXB;
    faam_muxer_config cfg;
    faam_track_config cfg_tracks[8];
    CHECK(faam_muxer_config_init(&cfg, sizeof(cfg)) == FAAM_OK);
    CHECK(test_append_track(&cfg, cfg_tracks, &t, NULL) == FAAM_OK);

    size_t cap = 4 + sizeof(k_sps) + 4 + sizeof(k_pps) + (size_t)NAL_COUNT * 5 + 4 + sizeof(k_idr);
    uint8_t *input = (uint8_t *)malloc(cap);
    CHECK(input);
    uint32_t len = 0;
    len = annexb_add(input, len, false, k_sps, sizeof(k_sps));
    len = annexb_add(input, len, false, k_pps, sizeof(k_pps));
    for (unsigned i = 0; i < NAL_COUNT; i++) {
        input[len++] = 0; input[len++] = 0; input[len++] = 1;
        input[len++] = 0x06; input[len++] = 0x80;
    }
    len = annexb_add(input, len, false, k_idr, sizeof(k_idr));
    CHECK(len == cap);

    memfile f = {0};
    faam_io io = mem_io(&f);
    faam_muxer *m = open_muxer(&cfg, &io);
    CHECK(faam_muxer_write_frame(m, 1, input, len, 3000, 0, FAAM_FRAME_KEYFRAME) == FAAM_OK);
    CHECK(faam_muxer_finalize(m) == FAAM_OK);
    faam_muxer_close(&m);

    io = mem_io(&f);
    faam_demuxer *d = NULL;
    CHECK(faam_demuxer_open(NULL, &io, &d) == FAAM_OK);
    size_t sample_cap = (size_t)NAL_COUNT * 9 + 4096;
    uint8_t *sample = (uint8_t *)malloc(sample_cap);
    CHECK(sample);
    uint32_t sample_len = 0;
    CHECK(faam_demuxer_read_frame(d, sample, (uint32_t)sample_cap, &sample_len) == FAAM_OK);
    size_t pos = 0;
    unsigned count = 0;
    while (pos < sample_len) {
        CHECK(pos + 4 <= sample_len);
        uint32_t n = be32(sample + pos);
        pos += 4;
        CHECK(n && pos + n <= sample_len);
        uint8_t type = sample[pos] & 31;
        uint8_t expected = count == 0 ? 7 : count == 1 ? 8 : count <= NAL_COUNT + 1 ? 6 : 5;
        CHECK(type == expected);
        count++;
        pos += n;
    }
    CHECK(count == NAL_COUNT + 3);
    CHECK(faam_demuxer_read_frame(d, sample, (uint32_t)sample_cap, &sample_len) == FAAM_END_OF_STREAM);
    faam_demuxer_close(&d);
    free(sample);
    free(input);
    free(f.buf);
}
#endif /* FAAM_MUXER_VIDEO */


#ifdef FAAM_MUXER_FRAGMENTED
enum { MAX_FRAMES = 1024, A_TRACK = 1, V_TRACK = 2, A_TS = 48000, V_TS = 90000 };

typedef struct {
    uint32_t track, size, duration;
    int32_t cts;
    bool key;
    uint64_t offset;
    uint8_t fill;
} frame_rec;

typedef struct {
    frame_rec f[MAX_FRAMES];
    unsigned n;
} frame_list;

static const uint8_t k_asc[2] = { 0x12, 0x10 };

/* Writes `video_frames` video frames at 30 fps (keyframe every 30) and the audio that
 * fits their span; frames of both tracks interleave in presentation order. */
static faam_muxer *start_frag(memfile *out, uint32_t fragment_ms, bool with_video)
{
    faam_muxer_config cfg;
    faam_track_config cfg_tracks[8];
    CHECK(faam_muxer_config_init(&cfg, sizeof(cfg)) == FAAM_OK);
    cfg.fragment_ms = fragment_ms;
    faam_track_config a;
    memset(&a, 0, sizeof(a));
    a.struct_size = sizeof(a);
    a.track_type = FAAM_TRACK_AUDIO; a.codec_id = FAAM_CODEC_AAC;
    a.timescale = a.sample_rate = A_TS; a.channels = 2;
    a.codec_data = k_asc; a.codec_data_len = sizeof(k_asc);
    CHECK(test_append_track(&cfg, cfg_tracks, &a, NULL) == FAAM_OK);
#ifdef FAAM_MUXER_VIDEO
    if (with_video) {
        faam_track_config v = video_track();
        CHECK(test_append_track(&cfg, cfg_tracks, &v, NULL) == FAAM_OK);
    }
#else
    (void)with_video;
#endif
    faam_io io = mem_io(out);
    return open_muxer(&cfg, &io);
}

static void write_one(faam_muxer *m, frame_list *l, uint32_t track, unsigned idx)
{
    frame_rec *r = &l->f[l->n++];
    CHECK(l->n <= MAX_FRAMES);
    r->track = track;
    r->fill = (uint8_t)(track * 61 + idx * 7);
    if (track == A_TRACK) {
        r->size = 20 + (idx * 7) % 50; r->duration = 1024; r->cts = 0; r->key = true;
    } else {
        r->key = idx % 30 == 0;
        r->size = 100 + (idx * 13) % 200 + (r->key ? 300 : 0);
        r->duration = 3000;
        r->cts = k_cts[idx % 7];
    }
    uint8_t buf[700];
    memset(buf, r->fill, r->size);
    CHECK(faam_muxer_write_frame(m, track, buf, r->size, r->duration, r->cts, r->key ? FAAM_FRAME_KEYFRAME : 0) == FAAM_OK);
}

static void feed(faam_muxer *m, frame_list *l, unsigned video_frames, bool with_video, unsigned *a_idx, unsigned *v_idx, unsigned stop)
{
    while (l->n < stop) {
        bool more_video = with_video && *v_idx < video_frames;
        unsigned a_total = with_video ? video_frames * 3000 / V_TS * A_TS / 1024 + 1 : video_frames;
        bool more_audio = *a_idx < a_total;
        if (!more_video && !more_audio) break;
        /* Compare a*1024/48000 with v*3000/90000 without division. */
        bool audio_first = more_audio && (!more_video || (uint64_t)*a_idx * 1024 * V_TS <= (uint64_t)*v_idx * 3000 * A_TS);
        if (audio_first) write_one(m, l, A_TRACK, (*a_idx)++);
        else write_one(m, l, V_TRACK, (*v_idx)++);
    }
}

typedef struct { uint64_t pos, size; char type[5]; } top_box;

static unsigned scan_top(const memfile *f, top_box *out, unsigned cap)
{
    unsigned n = 0;
    for (uint64_t pos = 0; pos + 8 <= f->len && n < cap;) {
        uint64_t size = be32(f->buf + pos);
        if (size == 0) size = f->len - pos;
        CHECK(size >= 8);
        out[n].pos = pos; out[n].size = size;
        memcpy(out[n].type, f->buf + pos + 4, 4); out[n].type[4] = 0;
        n++;
        pos += size;
    }
    return n;
}

/* Demuxes every frame, verifying payload bytes against the fill pattern. */
static void demux_all(const memfile *f, frame_list *got)
{
    memfile copy = *f;
    faam_io io = mem_io(&copy);
    faam_demuxer *d;
    CHECK(faam_demuxer_open(NULL, &io, &d) == FAAM_OK);
    got->n = 0;
    faam_frame_loc loc; loc.struct_size = sizeof(loc);
    uint8_t buf[700];
    while (faam_demuxer_next_frame_loc(d, &loc) == FAAM_OK) {
        CHECK(got->n < MAX_FRAMES);
        frame_rec *r = &got->f[got->n++];
        r->track = loc.track_id; r->size = loc.frame_bytes; r->duration = loc.duration_ticks;
        r->cts = loc.cts_offset; r->key = !!(loc.flags & FAAM_FRAME_KEYFRAME); r->offset = loc.file_offset;
        uint32_t bytes;
        CHECK(faam_demuxer_read_frame(d, buf, sizeof(buf), &bytes) == FAAM_OK && bytes == r->size);
        r->fill = buf[0];
        for (uint32_t i = 0; i < bytes; i++) CHECK(buf[i] == buf[0]);
    }
    faam_demuxer_close(&d);
}

/* Per track, the demuxed frames must be the written ones in order. */
static void check_prefix_of(const frame_list *want, const frame_list *got)
{
    unsigned next[3] = { 0, 0, 0 };
    for (unsigned i = 0; i < got->n; i++) {
        const frame_rec *g = &got->f[i];
        CHECK(g->track == A_TRACK || g->track == V_TRACK);
        unsigned k = next[g->track]++, seen = 0, j;
        for (j = 0; j < want->n; j++)
            if (want->f[j].track == g->track && seen++ == k) break;
        CHECK(j < want->n);
        const frame_rec *w = &want->f[j];
        CHECK(w->size == g->size && w->duration == g->duration && w->key == g->key && w->fill == g->fill);
        CHECK(g->cts == w->cts);
    }
}

#ifdef FAAM_MUXER_VIDEO
static unsigned frames_in(const frame_list *got, uint64_t lo, uint64_t hi)
{
    unsigned c = 0;
    for (unsigned i = 0; i < got->n; i++) c += got->f[i].offset >= lo && got->f[i].offset < hi;
    return c;
}

/* Frames of the fragments that end at or before `limit` (strictly before when exclusive). */
static unsigned frames_complete(const memfile *full, const frame_list *all, uint64_t limit, bool exclusive)
{
    top_box box[512];
    unsigned n = scan_top(full, box, 512), c = 0;
    for (unsigned i = 0; i < n; i++) {
        if (strcmp(box[i].type, "moof")) continue;
        unsigned j = i + 1;
        while (j < n && strcmp(box[j].type, "mdat")) j++;
        CHECK(j < n);
        uint64_t end = box[j].pos + box[j].size;
        if (exclusive ? end < limit : end <= limit) c += frames_in(all, box[i].pos, end);
    }
    return c;
}

static void test_fragmented_av(void)
{
    memfile f = {0};
    frame_list want = {0}, got = {0};
    unsigned a = 0, v = 0;
    faam_muxer *m = start_frag(&f, 1000, true);
    feed(m, &want, 150, true, &a, &v, MAX_FRAMES);
    CHECK(want.n > 300);
    CHECK(faam_muxer_finalize(m) == FAAM_OK);
    faam_muxer_info info = {0};
    info.struct_size = sizeof(info);
    CHECK(faam_muxer_get_info(m, V_TRACK, &info) == FAAM_OK && info.frame_count == 150);
    faam_muxer_close(&m);

    /* Layout: ftyp, moov, then moof+mdat pairs with the filler between them. */
    top_box box[512];
    unsigned n = scan_top(&f, box, 512), fragments = 0;
    CHECK(n > 8 && !strcmp(box[0].type, "ftyp") && !strcmp(box[1].type, "moov"));
    for (unsigned i = 2; i < n; i++) {
        if (strcmp(box[i].type, "moof")) continue;
        CHECK(!strcmp(box[i + 1].type, "free") && !strcmp(box[i + 2].type, "mdat"));
        CHECK(box[i].pos + box[i].size + box[i + 1].size == box[i + 2].pos);
        fragments++;
    }
    CHECK(fragments == 5);   /* cut at the keyframes of frames 30, 60, 90 and 120, then finalize */
    const uint8_t *mehd = find_box(&f, "mehd", 0);
    CHECK(mehd && mehd[4] == 1);
    uint64_t total = ((uint64_t)be32(mehd + 8) << 32) | be32(mehd + 12);
    CHECK(total == 235ULL * 1024);                 /* audio (235 frames) outlasts the 5 s of video */
    CHECK(!find_box(&f, "stss", 0));

    demux_all(&f, &got);
    CHECK(got.n == want.n);
    check_prefix_of(&want, &got);
    for (unsigned i = 1; i < got.n; i++) CHECK(got.f[i].offset > got.f[i - 1].offset);

    /* Cut the finished file at many points: every fragment that is whole still demuxes. */
    uint64_t cuts[16384];
    unsigned nc = 0;
    cuts[nc++] = box[1].pos + box[1].size;
    cuts[nc++] = box[1].pos + box[1].size + 20;
    for (unsigned i = 2; i < n; i++) {
        uint64_t end = box[i].pos + box[i].size;
        if (strcmp(box[i].type, "mdat")) { cuts[nc++] = box[i].pos + 5; cuts[nc++] = end - 1; continue; }
        cuts[nc++] = box[i].pos + 5;
        cuts[nc++] = box[i].pos + box[i].size / 2;
        cuts[nc++] = end - 1;
        cuts[nc++] = end;
        cuts[nc++] = end + 3;
    }
    cuts[nc++] = f.len;
    for (unsigned i = 0; i < nc; i++) {
        if (cuts[i] > f.len) continue;
        memfile cut = f;
        cut.len = (size_t)cuts[i];
        frame_list part;
        demux_all(&cut, &part);
        CHECK(part.n == frames_complete(&f, &got, cuts[i], 0));
        check_prefix_of(&want, &part);
    }

    /* A crash right after the moof landed leaves that fragment's mdat size at 0. */
    for (unsigned i = 2; i < n; i++) {
        if (strcmp(box[i].type, "mdat") || i == n - 1) continue;
        memfile cut = f;
        cut.len = (size_t)(box[i].pos + box[i].size);
        unsigned expect = frames_complete(&f, &got, cut.len, 0);
        uint8_t saved[4];
        memcpy(saved, cut.buf + box[i].pos, 4);
        memset(cut.buf + box[i].pos, 0, 4);
        frame_list part;
        demux_all(&cut, &part);
        memcpy(cut.buf + box[i].pos, saved, 4);
        CHECK(part.n == expect);
        break;
    }

    free(f.buf);

    /* Crash without finalize: snapshots mid-recording see only closed fragments. */
    memset(&f, 0, sizeof(f));
    memset(&want, 0, sizeof(want));
    a = v = 0;
    m = start_frag(&f, 1000, true);
    memfile full = {0};
    {
        /* Reference image from a finished identical run. */
        frame_list ref = {0};
        unsigned ra = 0, rv = 0;
        faam_muxer *rm = start_frag(&full, 1000, true);
        feed(rm, &ref, 150, true, &ra, &rv, MAX_FRAMES);
        CHECK(faam_muxer_finalize(rm) == FAAM_OK);
        faam_muxer_close(&rm);
        demux_all(&full, &got);
    }
    for (unsigned stop = 20; stop < 480; stop += 37) {
        feed(m, &want, 150, true, &a, &v, stop);
        memfile snap = f;
        frame_list part;
        demux_all(&snap, &part);
        check_prefix_of(&want, &part);
        unsigned lo = frames_complete(&full, &got, snap.len, true), hi = frames_complete(&full, &got, snap.len, 0);
        CHECK(part.n == lo || part.n == hi);

        /* The crashed recording can still be tagged in place: growing the moov must leave every
         * completed fragment playable, since each locates its data relative to its own moof. */
        memfile tagged = {0};
        tagged.cap = snap.len + 8192;
        tagged.buf = (uint8_t *)malloc(tagged.cap);
        CHECK(tagged.buf);
        memcpy(tagged.buf, snap.buf, snap.len);
        tagged.len = snap.len;
        uint8_t title[3000];
        memset(title, 'T', sizeof(title));
        faam_metadata_entry e = { .struct_size = sizeof(e), .key = FAAM_METADATA_KEY_TITLE,
                                  .data_type = FAAM_METADATA_UTF8, .data = title, .data_bytes = sizeof(title) };
        faam_io tio = mem_io(&tagged);
        CHECK(faam_update_metadata_stream(&tio, &e, 1, 0) == FAAM_OK);
        frame_list after;
        demux_all(&tagged, &after);
        CHECK(after.n == part.n);
        check_prefix_of(&want, &after);
        free(tagged.buf);
    }
    faam_muxer_close(&m);
    free(f.buf);
    free(full.buf);
}
#endif

static void test_fragmented(void)
{
    memfile f = {0};
    frame_list want = {0}, got = {0};
    unsigned a = 0, v = 0;
    top_box box[512];
    unsigned n, fragments;
    faam_muxer *m;
#ifdef FAAM_MUXER_VIDEO
    test_fragmented_av();
#endif

    /* Audio only: fragments cut on time. */
    m = start_frag(&f, 500, 0);
    feed(m, &want, 200, false, &a, &v, MAX_FRAMES);
    CHECK(faam_muxer_finalize(m) == FAAM_OK);
    faam_muxer_close(&m);
    n = scan_top(&f, box, 512);
    fragments = 0;
    for (unsigned i = 0; i < n; i++) fragments += !strcmp(box[i].type, "moof");
    CHECK(fragments == 9);   /* 24 frames (512 ms) each: the first to reach 500 ms closes it */
    demux_all(&f, &got);
    CHECK(got.n == want.n);
    check_prefix_of(&want, &got);
    free(f.buf);
}
#endif /* FAAM_MUXER_FRAGMENTED */

int main(void) {
#ifdef FAAM_MUXER_VIDEO
    test_absent_stss();
    test_empty_stss();
    test_ctts();
    test_annexb();
    test_annexb_many_nals();
#endif
#ifdef FAAM_MUXER_FRAGMENTED
    test_fragmented();
#else
    {
        faam_muxer_config cfg;
        faam_track_config cfg_tracks[8];
        CHECK(faam_muxer_config_init(&cfg, sizeof(cfg)) == FAAM_OK);
        faam_track_config track = { .struct_size = sizeof(track), .track_type = FAAM_TRACK_AUDIO,
                                    .codec_id = FAAM_CODEC_AAC, .timescale = 44100 };
        cfg_tracks[0] = track; cfg.tracks = cfg_tracks; cfg.num_tracks = 1;
        cfg.fragment_ms = 1000;
        faam_io io = { .struct_size = sizeof(io) };
        faam_muxer *m = (faam_muxer *)0x1;
        CHECK(faam_muxer_open(&cfg, &io, &m) == FAAM_ERR_NOT_BUILT && !m);
    }
#endif
    puts("libfaam media tests passed.");
    return 0;
}
