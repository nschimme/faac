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

/* Single-track AAC layout: every mvhd/tkhd/mdhd/elst/stsz/stco/esds field is derived
 * from the written frames, for ABR, constant-rate, faststart and 64-bit durations. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "faam.h"
#include "../../tests/faam_test_helpers.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static int32_t wr(void *u, const void *p, uint32_t n) { return (int32_t)fwrite(p, 1, n, (FILE *)u); }
static int32_t rd(void *u, void *p, uint32_t n) { return (int32_t)fread(p, 1, n, (FILE *)u); }
static bool seek_to(void *u, uint64_t p) { return fseek((FILE *)u, (long)p, SEEK_SET) == 0; }
static uint64_t tell_at(void *u) { return (uint64_t)ftell((FILE *)u); }
static uint32_t u32(const unsigned char *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static const unsigned char *child(const unsigned char *box, unsigned skip, const char *name) {
    uint32_t end = u32(box);
    for (uint32_t p = 8 + skip; p + 8 <= end;) {
        uint32_t size = u32(box + p);
        CHECK(size >= 8 && size <= end - p);
        if (!memcmp(box + p + 4, name, 4)) return box + p;
        p += size;
    }
    CHECK(0);
    return NULL;
}

static void run_case(unsigned count, bool constant, bool wide_time, bool faststart) {
    FILE *f = tmpfile(); CHECK(f);
    faam_io io = { .struct_size = sizeof(faam_io) }; io.user_data = f; io.read = rd; io.write = wr; io.seek = seek_to; io.tell = tell_at;
    faam_muxer_config cfg;
    faam_track_config cfg_tracks[8];
    CHECK(faam_muxer_config_init(&cfg, sizeof(cfg)) == FAAM_OK);
    cfg.flags = constant ? FAAM_MUXER_CONSTANT_RATE : 0;
    cfg.creation_time = 0;
    const char encoder[] = "FAAC test";
    faam_metadata_entry metadata_entry = { .struct_size = sizeof(metadata_entry),
        .key = FAAM_METADATA_KEY_ENCODER, .data_type = FAAM_METADATA_UTF8,
        .data = encoder, .data_bytes = sizeof(encoder) - 1 };

    cfg.metadata = &metadata_entry; cfg.num_metadata = 1;
    faam_track_config track = { .struct_size = sizeof(track) };
    track.struct_size = sizeof(track); track.track_type = FAAM_TRACK_AUDIO;
    track.codec_id = FAAM_CODEC_AAC; track.timescale = track.sample_rate = 44100;
    track.channels = 2;
    memcpy(track.language, "ENG", 4);
    unsigned char asc[] = {0x12, 0x10}; track.codec_data = asc; track.codec_data_len = sizeof(asc);
    CHECK(test_append_track(&cfg, cfg_tracks, &track, NULL) == FAAM_OK);
    if (faststart) cfg.flags |= FAAM_MUXER_FASTSTART;
    faam_muxer *m;
    CHECK(faam_muxer_open(&cfg, &io, &m) == FAAM_OK);
    unsigned char frame[300] = {0};
    uint64_t bytes = 0, ticks = 0, window_bytes = 0, window_ticks = 0;
    uint32_t peak = 0;
    for (unsigned i = 0; i < count; i++) {
        uint32_t size = i < 44 ? 100 : 200;
        uint32_t duration = wide_time ? UINT32_MAX : (i + 1 == count ? 512 : 1024);
        CHECK(faam_muxer_write_frame(m, 1, frame, size, duration, 0, FAAM_FRAME_KEYFRAME) == FAAM_OK);
        bytes += size; ticks += duration;
        if (duration >= 1024) {
            window_bytes += size; window_ticks += duration;
            if (window_ticks >= 44100) {
                uint32_t rate = (uint32_t)(8 * window_bytes * 44100 / window_ticks);
                if (peak < rate) peak = rate;
                window_bytes = window_ticks = 0;
            }
        }
    }
    faam_gapless_info gapless = { sizeof(faam_gapless_info), 1024, 512, ticks - 1536 };
    CHECK(faam_muxer_update(m, &(faam_muxer_update_params){ .struct_size = sizeof(faam_muxer_update_params), .flags = FAAM_UPDATE_GAPLESS, .gapless = &gapless }) == FAAM_OK);
    CHECK(faam_muxer_update(m, &(faam_muxer_update_params){ .struct_size = sizeof(faam_muxer_update_params), .flags = FAAM_UPDATE_CREATION_TIME, .creation_time = 123 }) == FAAM_OK);
    CHECK(faam_muxer_finalize(m) == FAAM_OK);
    faam_muxer_close(&m);
    CHECK(fseek(f, 0, SEEK_END) == 0);
    long length = ftell(f); CHECK(length > 0);
    unsigned char *data = malloc((size_t)length); CHECK(data);
    rewind(f); CHECK(fread(data, 1, (size_t)length, f) == (size_t)length); fclose(f);
    CHECK(u32(data) == 28 && !memcmp(data+8, "M4A \0\0\0\0M4A mp42isom", 20));
    size_t mdat_pos;
    if (faststart) {
        CHECK(u32(data + 28) == 8 && !memcmp(data + 32, "wide", 4));
        CHECK(!memcmp(data + 40, "moov", 4));
        mdat_pos = 36 + u32(data + 36);
        CHECK(!memcmp(data + mdat_pos + 4, "mdat", 4));
    } else {
        CHECK(u32(data+28) == 8 && !memcmp(data+32,"wide",4));
        CHECK(u32(data+36) == bytes+8 && !memcmp(data+40,"mdat",4));
        mdat_pos = 36;
    }
    const unsigned char *moov = faststart ? data + 36 : data + 44 + bytes;
    CHECK(!memcmp(moov+4,"moov",4));
    const unsigned char *mvhd = child(moov,0,"mvhd");
    const unsigned char *trak = child(moov,0,"trak");
    const unsigned char *tkhd = child(trak,0,"tkhd");
    const unsigned char *elst = child(child(trak,0,"edts"),0,"elst");
    const unsigned char *mdia = child(trak,0,"mdia");
    const unsigned char *mdhd = child(mdia,0,"mdhd");
    uint32_t version = wide_time ? 0x1000000 : 0;
    CHECK(u32(mvhd+8) == version && u32(tkhd+8) == (version|1) && u32(mdhd+8) == version);
    CHECK(u32(mvhd + (wide_time ? 16 : 12)) == (uint32_t)(2082844800U + 123));
    unsigned time_extra = wide_time ? 8 : 0;
    CHECK(u32(mvhd+20+time_extra) == 44100 && u32(mdhd+20+time_extra) == 44100);
    if (wide_time) {
        CHECK(u32(mvhd+32) == (ticks >> 32) && u32(mvhd+36) == (uint32_t)ticks);
        CHECK(u32(tkhd+36) == (ticks >> 32) && u32(tkhd+40) == (uint32_t)ticks);
        CHECK(u32(mdhd+32) == (ticks >> 32) && u32(mdhd+36) == (uint32_t)ticks);
        CHECK(u32(elst+16) == (gapless.total_samples >> 32) && u32(elst+20) == (uint32_t)gapless.total_samples);
        CHECK(u32(elst+24) == 0 && u32(elst+28) == 1024 && u32(elst+32) == 0x10000);
    } else {
        CHECK(u32(mvhd+24) == ticks && u32(tkhd+28) == ticks && u32(mdhd+24) == ticks);
        CHECK(u32(elst+16) == gapless.total_samples && u32(elst+20) == 1024 && u32(elst+24) == 0x10000);
    }
    CHECK(u32(elst+8) == version && u32(elst+12) == 1);
    CHECK(mdhd[28+(wide_time?12:0)] == 0x15 && mdhd[29+(wide_time?12:0)] == 0xc7);
    const unsigned char *stbl = child(child(mdia,0,"minf"),0,"stbl");
    const unsigned char *stsc = child(stbl,0,"stsc");
    CHECK(u32(stsc) == 28 && u32(stsc+12) == 1 && u32(stsc+16) == 1 && u32(stsc+20) == count && u32(stsc+24) == 1);
    const unsigned char *stco = child(stbl,0,"stco");
    CHECK(u32(stco) == 20 && u32(stco+12) == 1 && u32(stco+16) == mdat_pos + 8);
    const unsigned char *stsz = child(stbl,0,"stsz");
    CHECK(u32(stsz+12) == 0 && u32(stsz+16) == count && u32(stsz) == 20 + 4*count);
    for (unsigned i = 0; i < count; i++) CHECK(u32(stsz+20+4*i) == (i < 44 ? 100 : 200));
    const unsigned char *esds = child(child(child(stbl,0,"stsd"),8,"mp4a"),28,"esds");
    uint32_t average = (uint32_t)(8*bytes*44100/ticks);
    CHECK(u32(esds+30) == (constant || !peak ? average : peak));
    CHECK(u32(esds+34) == average);
    free(data);
}

int main(void) {
    run_case(10, false, 0, false);
    run_case(10, false, 0, true);
    run_case(100, false, 0, false);
    run_case(100, true, 0, false);
    run_case(10, true, 0, false);
    run_case(2, false, true, false);
    puts("audio layout: short, ABR, CBR, faststart and 64-bit duration passed");
    return 0;
}
