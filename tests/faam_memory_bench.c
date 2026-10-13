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

/* FAAM allocation/refactor benchmark. Compile against each release archive;
 * see faam_memory_benchmark.py. This is deliberately outside meson test. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "faam.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
typedef struct { uint8_t *data; size_t pos, len, cap; bool discard; } file;
static int32_t wr(void *user, const void *data, uint32_t n)
{
    file *f = user;
    if (!f->discard) {
        if (f->pos + n > f->cap) {
            size_t cap = (f->pos + n) * 2;
            void *p = realloc(f->data, cap); CHECK(p);
            f->data = p; f->cap = cap;
        }
        memcpy(f->data + f->pos, data, n);
    }
    f->pos += n;
    if (f->pos > f->len) f->len = f->pos;
    return (int32_t)n;
}
static int32_t rd(void *user, void *data, uint32_t n)
{
    file *f = user;
    size_t left = f->pos < f->len ? f->len - f->pos : 0;
    if (n > left) n = (uint32_t)left;
    if (n) memcpy(data, f->data + f->pos, n);
    f->pos += n; return (int32_t)n;
}
static bool seek(void *user, uint64_t pos) { ((file *)user)->pos = (size_t)pos; return true; }
static uint64_t tell(void *user) { return ((file *)user)->pos; }
static double now(void)
{
    struct timespec ts; CHECK(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static void mux(faam_muxer_config *cfg, file *f, unsigned frames, bool finish)
{
    f->pos = f->len = 0;
    faam_io io = { sizeof(io), f, rd, wr, seek, tell, NULL };
    faam_muxer *m = NULL;
    CHECK(faam_muxer_open(cfg, &io, &m) == FAAM_OK);
    uint8_t payload[128] = {0};
    for (unsigned i = 0; i < frames; i++)
        for (unsigned t = 0; t < cfg->num_tracks; t++)
            CHECK(faam_muxer_write_frame(m, t + 1, payload, sizeof(payload), 1024, 0, FAAM_FRAME_KEYFRAME) == FAAM_OK);
    if (finish) CHECK(faam_muxer_finalize(m) == FAAM_OK);
    CHECK(faam_muxer_close(&m) == FAAM_OK);
}
static void demux(file *f, bool frames)
{
    f->pos = 0;
    faam_io io = { sizeof(io), f, rd, wr, seek, tell, NULL };
    faam_demuxer *d = NULL; CHECK(faam_demuxer_open(NULL, &io, &d) == FAAM_OK);
    if (frames) {
        faam_frame_loc loc = { .struct_size = sizeof(loc) };
        faam_status st;
        while ((st = faam_demuxer_next_frame_loc(d, &loc)) == FAAM_OK) {
            uint32_t n; CHECK(faam_demuxer_read_frame(d, NULL, 0, &n) == FAAM_OK);
        }
        CHECK(st == FAAM_END_OF_STREAM);
    }
    CHECK(faam_demuxer_close(&d) == FAAM_OK);
}
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    const char *mode = argv[1];
    faam_track_config tracks[2] = {0};
    const uint8_t asc[2] = {0x12, 0x10};
    for (unsigned i = 0; i < 2; i++) {
        tracks[i].struct_size = sizeof(tracks[i]);
        tracks[i].track_type = FAAM_TRACK_AUDIO; tracks[i].codec_id = FAAM_CODEC_AAC;
        tracks[i].timescale = tracks[i].sample_rate = 44100; tracks[i].channels = 2;
        tracks[i].codec_data = asc; tracks[i].codec_data_len = sizeof(asc);
    }
    faam_muxer_config cfg; CHECK(faam_muxer_config_init(&cfg, sizeof(cfg)) == FAAM_OK);
    cfg.tracks = tracks; cfg.num_tracks = strstr(mode, "multi") ? 2 : 1;
    if (strstr(mode, "fragment")) cfg.fragment_ms = 2000;
    file f = {0};
    bool reading = !strncmp(mode, "demux", 5) || !strcmp(mode, "metadata");
    unsigned frames = !strcmp(mode, "open") || !strcmp(mode, "metadata") ? 0 : 131072;
    faam_custom_tag tags[1024];
    faam_metadata meta = { .struct_size = sizeof(meta), .num_custom_tags = 1024, .custom_tags = tags };
    if (!strcmp(mode, "metadata")) {
        for (unsigned i = 0; i < 1024; i++) tags[i] = (faam_custom_tag){ .struct_size = sizeof(tags[i]), .name = "key", .value = "value" };
        cfg.metadata = &meta;
    }
    if (reading) mux(&cfg, &f, frames, true);
    else f.discard = true;
    /* The runner warms each executable separately. Batch to suppress startup noise. */
#ifdef FAAM_BENCH_CUSTOM
    extern void bench_reset(void);
    extern unsigned bench_rounds;
    bench_reset();
#endif
    double start = now(), elapsed;
    unsigned rounds = 0;
    do {
        if (reading) demux(&f, strcmp(mode, "metadata") != 0);
        else mux(&cfg, &f, frames, strcmp(mode, "open") != 0);
        rounds++;
        elapsed = now() - start;
    } while (elapsed < 0.25);
#ifdef FAAM_BENCH_CUSTOM
    bench_rounds = rounds;
#endif
    printf("%.12f\n", elapsed / rounds);
    free(f.data);
    return 0;
}
