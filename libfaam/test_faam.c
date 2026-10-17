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
 * Unit and Integration Tests for libfaam over Stream I/O
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "faam.h"
#include "../tests/faam_test_helpers.h"

static int32_t file_read_cb(void *user_data, void *buf, uint32_t bytes) {
    return (int32_t)fread(buf, 1, bytes, (FILE *)user_data);
}

static int32_t file_write_cb(void *user_data, const void *buf, uint32_t bytes) {
    return (int32_t)fwrite(buf, 1, bytes, (FILE *)user_data);
}

static bool file_seek_cb(void *user_data, uint64_t offset) {
    return fseek((FILE *)user_data, (long)offset, SEEK_SET) == 0;
}

static uint64_t file_tell_cb(void *user_data) {
    return (uint64_t)ftell((FILE *)user_data);
}

#ifdef FAAM_HAVE_TAG_CHAPTER
static inline uint32_t t_read_u32(const uint8_t *b) {
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}
static inline void t_write_u32(uint8_t *b, uint32_t v) {
    b[0] = (uint8_t)(v >> 24); b[1] = (uint8_t)(v >> 16); b[2] = (uint8_t)(v >> 8); b[3] = (uint8_t)v;
}
static inline uint64_t t_read_u64(const uint8_t *b) {
    return ((uint64_t)t_read_u32(b) << 32) | (uint64_t)t_read_u32(b + 4);
}
static inline void t_write_u64(uint8_t *b, uint64_t v) {
    t_write_u32(b, (uint32_t)(v >> 32)); t_write_u32(b + 4, (uint32_t)v);
}

static bool t_is_offset_table_container(const char type[4]) {
    static const char *containers[] = { "trak", "mdia", "minf", "stbl" };
    for (size_t i = 0; i < sizeof(containers) / sizeof(containers[0]); i++) {
        if (memcmp(type, containers[i], 4) == 0) return true;
    }
    return false;
}

static void t_shift_chunk_offsets(uint8_t *moov_buf, long box_off, bool is64, int64_t delta) {
    uint32_t count = t_read_u32(moov_buf + box_off + 12);
    long p = box_off + 16;
    for (uint32_t i = 0; i < count; i++) {
        if (is64) {
            uint64_t v = t_read_u64(moov_buf + p);
            t_write_u64(moov_buf + p, (uint64_t)((int64_t)v + delta));
            p += 8;
        } else {
            uint32_t v = t_read_u32(moov_buf + p);
            t_write_u32(moov_buf + p, (uint32_t)((int64_t)v + delta));
            p += 4;
        }
    }
}

static void t_fixup_moov_offsets(uint8_t *moov_buf, long start, long end, int64_t delta) {
    long pos = start;
    while (pos + 8 <= end) {
        uint32_t size = t_read_u32(moov_buf + pos);
        char type[4];
        memcpy(type, moov_buf + pos + 4, 4);
        if (size < 8 || pos + (long)size > end) break;

        if (memcmp(type, "stco", 4) == 0) t_shift_chunk_offsets(moov_buf, pos, false, delta);
        else if (memcmp(type, "co64", 4) == 0) t_shift_chunk_offsets(moov_buf, pos, true, delta);
        else if (t_is_offset_table_container(type)) t_fixup_moov_offsets(moov_buf, pos + 8, pos + size, delta);

        pos += size;
    }
}

/* Rewrites a just-muxed ftyp+mdat+moov file in place into a faststart-style
 * ftyp+moov+mdat layout, correcting every stco/co64 chunk offset for the
 * move (mdat's new start is ftyp_end+moov_size instead of ftyp_end, so every
 * stored offset needs += moov_size). This is test-only scaffolding to get a
 * moov-before-mdat file to patch against, since mux.c's cfg.faststart field
 * is currently unimplemented (see libfaam_internal.h / mux.c: nothing reads
 * cfg.faststart) -- it does not touch atom_patch.c or mux.c itself.
 */
static void make_faststart_layout(const char *path) {
    FILE *f = fopen(path, "rb");
    assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long total = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc((size_t)total);
    assert(buf != NULL);
    size_t bytes_read = fread(buf, 1, (size_t)total, f);
    assert(bytes_read == (size_t)total);
    fclose(f);

    long pos = 0, ftyp_end = 0;
    long mdat_off = -1, moov_off = -1, moov_size = 0;
    while (pos + 8 <= total) {
        uint32_t size = t_read_u32(buf + pos);
        char type[4];
        memcpy(type, buf + pos + 4, 4);
        if (size < 8 || pos + (long)size > total) break;
        if (memcmp(type, "ftyp", 4) == 0 || memcmp(type, "wide", 4) == 0) ftyp_end = pos + size;
        else if (memcmp(type, "mdat", 4) == 0) mdat_off = pos;
        else if (memcmp(type, "moov", 4) == 0) { moov_off = pos; moov_size = size; }
        pos += size;
    }
    assert(ftyp_end > 0 && mdat_off == ftyp_end && moov_off > mdat_off);

    uint8_t *moov_buf = (uint8_t *)malloc((size_t)moov_size);
    assert(moov_buf != NULL);
    memcpy(moov_buf, buf + moov_off, (size_t)moov_size);
    t_fixup_moov_offsets(moov_buf, 8, moov_size, (int64_t)moov_size);

    FILE *out = fopen(path, "wb");
    assert(out != NULL);
    fwrite(buf, 1, (size_t)ftyp_end, out);
    fwrite(moov_buf, 1, (size_t)moov_size, out);
    fwrite(buf + mdat_off, 1, (size_t)(moov_off - mdat_off), out);
    fclose(out);

    free(moov_buf);
    free(buf);
}
#endif /* FAAM_HAVE_TAG_CHAPTER */

/* Payload writes advance a virtual file; only headers and moov occupy memory. */
typedef struct {
    uint64_t pos;
    uint8_t header[44];
    uint8_t moov[16384];
    uint32_t moov_size;
    uint64_t moov_start;
    bool fail_write;
    bool fail_seek;
    bool fail_moov;
} virtual_file;

static int32_t virtual_write(void *user, const void *data, uint32_t size) {
    virtual_file *v = (virtual_file *)user;
    if (v->fail_write || (v->fail_moov && v->moov_start && v->pos >= v->moov_start)) return -1;
    if (v->pos < 44 && size <= 44 - v->pos) memcpy(v->header + v->pos, data, size);
    else if (v->moov_start && v->pos >= v->moov_start && v->pos - v->moov_start + size <= sizeof(v->moov)) {
        uint32_t offset = (uint32_t)(v->pos - v->moov_start);
        memcpy(v->moov + offset, data, size);
        if (v->moov_size < offset + size) v->moov_size = offset + size;
    }
    v->pos += size;
    return (int32_t)size;
}
static bool virtual_seek(void *user, uint64_t pos) {
    virtual_file *v = (virtual_file *)user;
    if (v->fail_seek) return false;
    v->pos = pos; return true;
}
static uint64_t virtual_tell(void *user) { return ((virtual_file *)user)->pos; }
static uint32_t test_u32(const uint8_t *b) {
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
}
static uint64_t test_u64(const uint8_t *b) { return ((uint64_t)test_u32(b) << 32) | test_u32(b + 4); }

static void test_virtual_mux(void) {
    faam_muxer_config cfg;
    faam_track_config cfg_tracks[8];
    faam_status st = faam_muxer_config_init(&cfg, sizeof(cfg));
    assert(st == FAAM_OK && cfg.gapless == NULL);
    faam_track_config invalid = { .struct_size = sizeof(invalid) };
    invalid.track_type = FAAM_TRACK_VIDEO; invalid.codec_id = FAAM_CODEC_H264;
#ifdef FAAM_MUXER_VIDEO
    st = test_append_track(&cfg, cfg_tracks, &invalid, NULL); assert(st == FAAM_ERR_INVALID_ARG);
#else
    st = test_append_track(&cfg, cfg_tracks, &invalid, NULL); assert(st == FAAM_ERR_NOT_BUILT);
#endif
    invalid.track_type = FAAM_TRACK_AUDIO; invalid.codec_id = FAAM_CODEC_GENERIC;
    st = test_append_track(&cfg, cfg_tracks, &invalid, NULL); assert(st == FAAM_ERR_INVALID_ARG);
    faam_muxer_config validation = cfg;
    faam_track_config validation_tracks[8];
    invalid.codec_id = FAAM_CODEC_AAC; invalid.track_id = 77;
    st = test_append_track(&validation, validation_tracks, &invalid, NULL); assert(st == FAAM_OK);
    st = test_append_track(&validation, validation_tracks, &invalid, NULL); assert(st == FAAM_ERR_INVALID_ARG);
    for (unsigned i = 0; i < 7; i++) {
        invalid.track_id = i + 100;
        st = test_append_track(&validation, validation_tracks, &invalid, NULL); assert(st == FAAM_OK);
    }
    st = test_append_track(&validation, validation_tracks, &invalid, NULL); assert(st == FAAM_ERR_INVALID_ARG);
    faam_custom_tag custom_tags[16] = {0};
    for (uint32_t i = 0; i < 16; i++) custom_tags[i].struct_size = sizeof(custom_tags[i]);
    for (unsigned i = 0; i < 16; i++) {
        custom_tags[i].name = "test";
        custom_tags[i].value = "value";
    }
    faam_metadata metadata = { .struct_size = sizeof(metadata) }; metadata.num_custom_tags = 16; metadata.custom_tags = custom_tags;
    cfg.metadata = &metadata;
    invalid.track_id = 77; invalid.timescale = 44100;
    st = test_append_track(&cfg, cfg_tracks, &invalid, NULL); assert(st == FAAM_OK);
    faam_gapless_info gap = { .struct_size = sizeof(gap), .encoder_delay = 10 };
    cfg.gapless = &gap;

    uint8_t payload = 0;
    for (unsigned mode = 0; mode < 7; mode++) {
        virtual_file v = {0};
        faam_io io = { sizeof(faam_io), &v, NULL, virtual_write, virtual_seek, virtual_tell, NULL };
        if (mode == 6) io.seek = NULL;
        faam_muxer *m;
        if (mode == 6) {
            /* A progressive muxer needs seek up front, not at finalize. */
            st = faam_muxer_open(&cfg, &io, &m); assert(st == FAAM_ERR_UNSUPPORTED && !m);
            continue;
        }
        st = faam_muxer_open(&cfg, &io, &m); assert(st == FAAM_OK);
        st = faam_muxer_write_frame(m, 999, &payload, 1, 1, 0, FAAM_FRAME_KEYFRAME); assert(st == FAAM_ERR_NO_TRACK);
        if (mode == 0) {
            for (unsigned i = 0; i < 5; i++) {
                st = faam_muxer_write_frame(m, 77, &payload, 0x40000000, 44100, 0, FAAM_FRAME_KEYFRAME); assert(st == FAAM_OK);
            }
        } else {
            st = faam_muxer_write_frame(m, 77, &payload, 100, 44100, 0, FAAM_FRAME_KEYFRAME); assert(st == FAAM_OK);
        }
        if (mode == 2) v.fail_write = true;
        if (mode == 5) v.fail_moov = true;
        if (mode == 3) v.fail_seek = true;
        if (mode == 4) {
            v.fail_write = true;
            st = faam_muxer_write_frame(m, 77, &payload, 1, 1, 0, FAAM_FRAME_KEYFRAME); assert(st == FAAM_ERR_IO_WRITE);
            v.fail_write = false;
        }
        v.moov_start = v.pos;
        st = faam_muxer_finalize(m);
        if (mode >= 2) {
            faam_status expected = FAAM_ERR_IO_WRITE; /* a seek that exists but fails is a write failure */
            assert(st == expected);
            v.fail_write = v.fail_seek = v.fail_moov = false;
            st = faam_muxer_finalize(m); assert(st == expected);
            st = faam_muxer_write_frame(m, 77, &payload, 1, 1, 0, FAAM_FRAME_KEYFRAME); assert(st == expected);
        } else if (mode == 0) {
            assert(st == FAAM_OK);
            assert(test_u32(v.header + 28) == 1 && !memcmp(v.header + 32, "mdat", 4));
            assert(test_u64(v.header + 36) == 5ULL * 0x40000000 + 16);
            bool co64 = false;
            for (uint32_t i = 4; i + 56 <= v.moov_size; i++) if (!memcmp(v.moov + i, "co64", 4)) {
                assert(test_u32(v.moov + i + 8) == 1);
                assert(test_u64(v.moov + i + 12) == 44);
                co64 = true;
            }
            assert(co64);
            faam_muxer_info info = {0}; info.struct_size = sizeof(info);
            st = faam_muxer_get_info(m, 77, &info); assert(st == FAAM_OK);
            assert(info.max_frame_size == 0x40000000);
        } else {
            assert(st == FAAM_OK);
            bool edit = false, movie = false, track = false;
            for (uint32_t i = 4; i + 100 <= v.moov_size; i++) {
                if (!memcmp(v.moov + i, "elst", 4)) {
                    assert(test_u32(v.moov + i + 12) == 44100 - 10); /* derived: ticks minus delay */
                    assert(test_u32(v.moov + i + 16) == 10); edit = true;
                }
                if (!memcmp(v.moov + i, "mvhd", 4)) {
                    assert(test_u32(v.moov + i + 100) == 78); movie = true;
                }
                if (!memcmp(v.moov + i, "tkhd", 4)) {
                    assert(test_u32(v.moov + i + 4) == 1); track = true;
                }
            }
            assert(edit && movie && track);
            assert(test_u32(v.header + 36) == 108 && !memcmp(v.header + 40, "mdat", 4));
            faam_muxer_info info = {0}; info.struct_size = sizeof(info);
            st = faam_muxer_get_info(m, 77, &info); assert(st == FAAM_OK);
            assert(info.avg_bitrate == 800 && info.max_bitrate == 800 && info.max_frame_size == 100);
        }
        faam_muxer_close(&m);
    }
}

int main(void)
{
    test_virtual_mux();
    /* Test 1: Stream Muxer file creation (Audio & Video tracks, chapters) */
    FILE *fout = fopen("test_output.mp4", "wb");
    assert(fout != NULL);

    faam_io io_out = { sizeof(faam_io), fout, file_read_cb, file_write_cb, file_seek_cb, file_tell_cb, NULL };

    faam_muxer_config cfg;
    faam_track_config cfg_tracks[8];
    faam_status st = faam_muxer_config_init(&cfg, sizeof(cfg));
    assert(st == FAAM_OK);

    faam_chapter chaps[2];
    for (uint32_t i = 0; i < 2; i++) chaps[i].struct_size = sizeof(chaps[i]);
    chaps[0].start_ms = 0;
    chaps[0].title = "Intro";
    chaps[1].start_ms = 5000;
    chaps[1].title = "Event";
    cfg.chapters = chaps;
    cfg.num_chapters = 2;

    /* Track 1: Audio */
    uint8_t dummy_asc[2] = { 0x12, 0x10 };
    faam_track_config a_tr;
    memset(&a_tr, 0, sizeof(a_tr));
    a_tr.struct_size = sizeof(a_tr);
    a_tr.track_type = FAAM_TRACK_AUDIO;
    a_tr.codec_id = FAAM_CODEC_AAC;
    a_tr.timescale = 44100;
    a_tr.sample_rate = 44100;
    a_tr.channels = 2;
    a_tr.codec_data = dummy_asc;
    a_tr.codec_data_len = sizeof(dummy_asc);

    uint32_t a_track_id = 0;
    st = test_append_track(&cfg, cfg_tracks, &a_tr, &a_track_id);
    assert(st == FAAM_OK);
    assert(a_track_id == 1);

    /* Track 2: Video (H.264 Security Camera) */
#ifdef FAAM_MUXER_VIDEO
    uint8_t dummy_avcc[10] = { 0x01, 0x64, 0x00, 0x1F, 0xFF, 0xE1, 0x00, 0x02, 0x67, 0x64 };
    faam_track_config v_tr;
    memset(&v_tr, 0, sizeof(v_tr));
    v_tr.struct_size = sizeof(v_tr);
    v_tr.track_type = FAAM_TRACK_VIDEO;
    v_tr.codec_id = FAAM_CODEC_H264;
    v_tr.timescale = 90000;
    v_tr.width = 1920;
    v_tr.height = 1080;
    v_tr.codec_data = dummy_avcc;
    v_tr.codec_data_len = sizeof(dummy_avcc);

    uint32_t v_track_id = 0;
    st = test_append_track(&cfg, cfg_tracks, &v_tr, &v_track_id);
    assert(st == FAAM_OK);
    assert(v_track_id == 2);
#else
    uint32_t v_track_id = 0;
#endif

    faam_muxer *m = NULL;
    st = faam_muxer_open(&cfg, &io_out, &m);
    assert(st == FAAM_OK);
    assert(m != NULL);
    st = faam_muxer_get_track_id(m, 0, &a_track_id);
    assert(st == FAAM_OK && a_track_id == 1);
    if (v_track_id) {
        st = faam_muxer_get_track_id(m, 1, &v_track_id);
        assert(st == FAAM_OK && v_track_id == 2);
    }

    uint8_t dummy_frame[512];
    memset(dummy_frame, 0xAB, sizeof(dummy_frame));
    for (int i = 0; i < 10; i++) {
        st = faam_muxer_write_frame(m, a_track_id, dummy_frame, sizeof(dummy_frame), 1024, 0, FAAM_FRAME_KEYFRAME);
        assert(st == FAAM_OK);
        if (v_track_id) {
            st = faam_muxer_write_frame(m, v_track_id, dummy_frame, sizeof(dummy_frame), 3000, 0, (i % 5 == 0) ? FAAM_FRAME_KEYFRAME : 0);
            assert(st == FAAM_OK);
        }
    }

    st = faam_muxer_finalize(m);
    assert(st == FAAM_OK);
    faam_muxer_close(&m);

    fclose(fout);

    /* Test 2: Stream Demuxer file reading */
    FILE *fin = fopen("test_output.mp4", "rb");
    assert(fin != NULL);

    faam_io io_in = { sizeof(faam_io), fin, file_read_cb, file_write_cb, file_seek_cb, file_tell_cb, NULL };

    faam_demuxer *d = NULL;
    st = faam_demuxer_open(NULL, &io_in, &d);
    assert(st == FAAM_OK);
    assert(d != NULL);

    uint32_t num_tracks = 0;
    st = faam_demuxer_get_num_tracks(d, &num_tracks, NULL);
    assert(st == FAAM_OK);
    assert(num_tracks == (v_track_id ? 2u : 1u));

    faam_track_info t1 = {0};
    t1.struct_size = sizeof(t1);
    st = faam_demuxer_get_track_info(d, 0, &t1);
    assert(st == FAAM_OK);
    assert(t1.track_type == FAAM_TRACK_AUDIO);

    if (v_track_id) {
        faam_track_info t2 = {0};
        t2.struct_size = sizeof(t2);
        st = faam_demuxer_get_track_info(d, 1, &t2);
        assert(st == FAAM_OK);
        assert(t2.track_type == FAAM_TRACK_VIDEO);
        assert(t2.width == 1920 && t2.height == 1080);
    }

    uint8_t read_codec_data[64];
    uint32_t read_cdata_len = 0;
    st = faam_demuxer_get_codec_data(d, t1.track_id, read_codec_data, sizeof(read_codec_data), &read_cdata_len);
    assert(st == FAAM_OK);
    assert(read_cdata_len == sizeof(dummy_asc));

    faam_chapter read_chaps[10];
    for (uint32_t i = 0; i < 10; i++) read_chaps[i].struct_size = sizeof(read_chaps[i]);
    uint32_t read_ch_count = 0;
    st = test_read_chapters(d, read_chaps, 10, &read_ch_count);
    assert(st == FAAM_OK);
    assert(read_ch_count == 2);
    assert(strcmp(read_chaps[0].title, "Intro") == 0);
    assert(strcmp(read_chaps[1].title, "Event") == 0);

    faam_demuxer_close(&d);

    fclose(fin);

    remove("test_output.mp4");

#ifdef FAAM_HAVE_TAG_CHAPTER
    /* --- Tests 3/4: in-place tag growth must not corrupt sample data ---
     *
     * faam_update_tags_stream()/faam_update_chapters_stream() rewrite the
     * ilst/chpl atom in place. When the new payload is bigger than what was
     * there, everything physically after it in the file has to shift, and if
     * mdat sits after the patched atom (moov-before-mdat / faststart-style
     * layout), every stco/co64 chunk offset -- an absolute file position --
     * has to move with it. Getting either of those wrong either overwrites
     * real file content or leaves stale offsets that point at the wrong
     * bytes. These tests build known frame content, force ilst growth well
     * past the atom's original size, and confirm every frame still reads
     * back byte-for-byte correct.
     */
#define TEST_FRAME_COUNT 40
#define TEST_FRAME_SIZE  96

    for (int faststart_case = 0; faststart_case < 2; faststart_case++) {
        const char *path = "test_grow.mp4";

        FILE *gf = fopen(path, "wb");
        assert(gf != NULL);
        faam_io gio = { sizeof(faam_io), gf, file_read_cb, file_write_cb, file_seek_cb, file_tell_cb, NULL };

        faam_muxer_config gcfg;
        faam_track_config gcfg_tracks[8];
        st = faam_muxer_config_init(&gcfg, sizeof(gcfg));
        assert(st == FAAM_OK);

        faam_gapless_info ggap = { .struct_size = sizeof(ggap) };
        gcfg.gapless = &ggap;
        ggap.encoder_delay = 123;
        ggap.end_padding = 45;
        ggap.total_samples = 40000;

        uint8_t g_asc[2] = { 0x12, 0x10 };
        faam_track_config g_tr;
        memset(&g_tr, 0, sizeof(g_tr));
        g_tr.struct_size = sizeof(g_tr);
        g_tr.track_type = FAAM_TRACK_AUDIO;
        g_tr.codec_id = FAAM_CODEC_AAC;
        g_tr.timescale = 44100;
        g_tr.sample_rate = 44100;
        g_tr.channels = 2;
        g_tr.codec_data = g_asc;
        g_tr.codec_data_len = sizeof(g_asc);

        uint32_t g_track_id = 0;
        st = test_append_track(&gcfg, gcfg_tracks, &g_tr, &g_track_id);
        assert(st == FAAM_OK);

        faam_muxer *gm = NULL;
        st = faam_muxer_open(&gcfg, &gio, &gm);
        assert(st == FAAM_OK);

        uint8_t frame_buf[TEST_FRAME_SIZE];
        for (int i = 0; i < TEST_FRAME_COUNT; i++) {
            memset(frame_buf, (uint8_t)(i * 7 + 3), sizeof(frame_buf));
            st = faam_muxer_write_frame(gm, g_track_id, frame_buf, sizeof(frame_buf), 1024, 0, FAAM_FRAME_KEYFRAME);
            assert(st == FAAM_OK);
        }

        st = faam_muxer_finalize(gm);
        assert(st == FAAM_OK);
        faam_muxer_close(&gm);

        fclose(gf);

        /* faam's own muxer always writes mdat before moov -- cfg.faststart
         * is declared in faam.h but mux.c never reads it, so it can't
         * actually produce a moov-first file yet. Synthesize that layout by
         * hand (ftyp+moov+mdat, with stco/co64 corrected for the move) so
         * the faststart_case=1 pass exercises atom_patch's chunk-offset
         * correction path the way a real faststart writer eventually would.
         */
        if (faststart_case == 1) {
            make_faststart_layout(path);
        }

        long size_before_tag = 0;
        {
            FILE *sf = fopen(path, "rb");
            assert(sf != NULL);
            fseek(sf, 0, SEEK_END);
            size_before_tag = ftell(sf);
            fclose(sf);
        }

        /* Force real growth: this dwarfs the tiny default ilst (just an
         * encoder tag) the muxer writes. */
        faam_metadata big_meta;
        memset(&big_meta, 0, sizeof(big_meta)); big_meta.struct_size = sizeof(big_meta);
        big_meta.title = "A Reasonably Long Test Title For Growth";
        big_meta.artist = "Test Artist Name";
        big_meta.album = "Test Album Name";
        big_meta.track_num = 3;
        big_meta.track_total = 12;
        big_meta.compilation = true;
        faam_custom_tag big_tags[16] = {0};
        for (uint32_t i = 0; i < 16; i++) big_tags[i].struct_size = sizeof(big_tags[i]);
        char big_names[16][32];
        char big_value[256];
        memset(big_value, 'x', sizeof(big_value) - 1);
        big_value[sizeof(big_value) - 1] = '\0';
        big_meta.num_custom_tags = 16;
        big_meta.custom_tags = big_tags;
        for (uint32_t i = 0; i < big_meta.num_custom_tags; i++) {
            snprintf(big_names[i], sizeof(big_names[i]), "custom_tag_%u", i);
            big_tags[i].name = big_names[i];
            big_tags[i].value = big_value;
        }

        FILE *tf = fopen(path, "r+b");
        assert(tf != NULL);
        faam_io tio = { sizeof(faam_io), tf, file_read_cb, file_write_cb, file_seek_cb, file_tell_cb, NULL };
        st = faam_update_tags_stream(&tio, &big_meta, 0);
        assert(st == FAAM_OK);
        faam_chapter updated[2] = {0};
        for (uint32_t i = 0; i < 2; i++) updated[i].struct_size = sizeof(updated[i]);
        updated[0].start_ms = 123; updated[0].title = "First";
        updated[1].start_ms = 4567; updated[1].title = "Second";
        st = faam_update_chapters_stream(&tio, updated, 2, 0); assert(st == FAAM_OK);
        fclose(tf);

        long size_after_grow = 0;
        {
            FILE *sf = fopen(path, "rb");
            assert(sf != NULL);
            fseek(sf, 0, SEEK_END);
            size_after_grow = ftell(sf);
            fclose(sf);
        }
        assert(size_after_grow > size_before_tag);

        if (faststart_case == 0) {
            /* mdat precedes the patched atom here, so growth only ever has
             * to shift the (empty, in this layout) tail after moov -- no
             * unrelated content should have moved or been touched. */
            long delta = size_after_grow - size_before_tag;
            assert(delta > 0);

            /* Shrinking back onto the now-larger slot should pad in place
             * (free atom), not shrink the file -- confirms the cheap path
             * still runs instead of an unnecessary tail shift. */
            faam_metadata small_meta;
            memset(&small_meta, 0, sizeof(small_meta)); small_meta.struct_size = sizeof(small_meta);
            small_meta.title = "T";

            FILE *tf2 = fopen(path, "r+b");
            assert(tf2 != NULL);
            faam_io tio2 = { sizeof(faam_io), tf2, file_read_cb, file_write_cb, file_seek_cb, file_tell_cb, NULL };
            st = faam_update_tags_stream(&tio2, &small_meta, 0);
            assert(st == FAAM_OK);
            fclose(tf2);

            long size_after_shrink = 0;
            FILE *sf2 = fopen(path, "rb");
            assert(sf2 != NULL);
            fseek(sf2, 0, SEEK_END);
            size_after_shrink = ftell(sf2);
            fclose(sf2);
            assert(size_after_shrink == size_after_grow);
        }

        /* The real assertion: every sample frame must still demux to
         * exactly what was muxed, regardless of layout. Under the old
         * 64KB-capped, non-shifting tag.c this would either fail to locate
         * ilst (moov past 64KB was never the trigger here, file is small)
         * or -- in the faststart_case=1 pass -- read back corrupted/wrong
         * frame bytes because stco still pointed at pre-shift offsets while
         * mdat physically moved by the ilst growth delta.
         */
        FILE *df = fopen(path, "rb");
        assert(df != NULL);
        faam_io dio = { sizeof(faam_io), df, file_read_cb, file_write_cb, file_seek_cb, file_tell_cb, NULL };

        faam_demuxer *dd = NULL;
        st = faam_demuxer_open(NULL, &dio, &dd);
        assert(st == FAAM_OK);

        faam_chapter updated_read[2];
        for (uint32_t i = 0; i < 2; i++) updated_read[i].struct_size = sizeof(updated_read[i]); uint32_t updated_count;
        st = test_read_chapters(dd, updated_read, 2, &updated_count); assert(st == FAAM_OK);
        assert(updated_count == 2 && updated_read[0].start_ms == 123 && updated_read[1].start_ms == 4567);
        assert(!strcmp(updated_read[1].title, "Second"));
        faam_gapless_info gapless; gapless.struct_size = sizeof(gapless);
        st = faam_demuxer_get_track_gapless(dd, 0, &gapless); assert(st == FAAM_OK);
        assert(gapless.encoder_delay == 123 && gapless.end_padding == 45 && gapless.total_samples == 40000);

        int frames_seen = 0;
        uint8_t read_buf[TEST_FRAME_SIZE];
        faam_frame_loc loc; loc.struct_size = sizeof(loc);
        while (faam_demuxer_next_frame_loc(dd, &loc) == FAAM_OK) {
            uint32_t got_bytes = 0;
            st = faam_demuxer_read_frame(dd, read_buf, sizeof(read_buf), &got_bytes);
            assert(st == FAAM_OK);
            assert(got_bytes == TEST_FRAME_SIZE);

            uint8_t expected = (uint8_t)(frames_seen * 7 + 3);
            for (int b = 0; b < TEST_FRAME_SIZE; b++) {
                assert(read_buf[b] == expected);
            }
            frames_seen++;
        }
        assert(frames_seen == TEST_FRAME_COUNT);

        faam_demuxer_close(&dd);

        fclose(df);

        remove(path);
    }
#endif /* FAAM_HAVE_TAG_CHAPTER */

    printf("libfaam stream unit tests passed successfully.\n");
    return 0;
}
