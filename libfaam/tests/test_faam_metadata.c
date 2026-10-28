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

/* Public-API metadata contracts, including preservation and unknown atom types. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include "faam.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define OK(x) CHECK((x) == FAAM_OK)
#define LYRICS FAAM_METADATA_KEY_LYRICS
#define FREEFORM FAAM_METADATA_KEY_FREEFORM
#define UNKNOWN FAAM_FOURCC('x', 't', 's', 't')
static const uint8_t frame[] = {0x21, 0x10, 0x04, 0x60, 0x8c, 0x1c};
static const uint8_t binary[] = {0, 0xff, 0x80, 0x01};
static const char initial_lyrics[] = "First line\nDeuxi\xc3\xa8me ligne\n\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\n";

static int32_t rd(void *u, void *p, uint32_t n) { return (int32_t)fread(p, 1, n, u); }
static unsigned writes;
static int32_t wr(void *u, const void *p, uint32_t n) { writes++; return (int32_t)fwrite(p, 1, n, u); }
static bool sk(void *u, uint64_t n) { return n <= LONG_MAX && fseek(u, (long)n, SEEK_SET) == 0; }
static uint64_t tl(void *u) { return (uint64_t)ftell(u); }
static faam_io file_io(FILE *f) {
    faam_io io = { .struct_size = sizeof(io), .user_data = f, .read = rd, .write = wr, .seek = sk, .tell = tl };
    return io;
}
static faam_metadata_entry value(uint32_t key, uint32_t type, const void *data, uint32_t bytes) {
    faam_metadata_entry e = { .struct_size = sizeof(e), .key = key, .data_type = type, .data = data, .data_bytes = bytes };
    return e;
}
static void fixture(FILE *f, uint32_t flags, bool fragmented) {
    const uint8_t asc[] = {0x12, 0x10}, tempo[] = {0, 120}, png[] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    faam_metadata_entry entries[] = {
        value(FAAM_METADATA_KEY_TITLE, FAAM_METADATA_UTF8, "Original", 8),
        value(LYRICS, FAAM_METADATA_UTF8, initial_lyrics, sizeof(initial_lyrics) - 1),
        value(FAAM_METADATA_KEY_BPM, FAAM_METADATA_INTEGER, tempo, sizeof(tempo)),
        value(UNKNOWN, 0x123456, binary, sizeof(binary)),
        value(FAAM_METADATA_KEY_COVER_ART, FAAM_METADATA_PNG, png, sizeof(png)),
        value(FAAM_METADATA_KEY_COVER_ART, FAAM_METADATA_JPEG, binary, sizeof(binary)),
        value(FREEFORM, FAAM_METADATA_UTF8, "first", 5),
        value(FREEFORM, FAAM_METADATA_UTF8, "second", 6),
        value(FREEFORM, FAAM_METADATA_IMPLICIT, binary, sizeof(binary)),
    };
    entries[3].locale = 0x12345678;
    entries[6].mean = entries[7].mean = "org.example";
    entries[6].name = entries[7].name = "Identifier";
    entries[8].mean = "other.example"; entries[8].name = "Identifier";

    faam_track_config track = { .struct_size = sizeof(track), .track_type = FAAM_TRACK_AUDIO,
        .codec_id = FAAM_CODEC_AAC, .timescale = 44100, .sample_rate = 44100, .channels = 2,
        .codec_data = asc, .codec_data_len = sizeof(asc) };
    faam_chapter chapter = { .struct_size = sizeof(chapter), .title = "Chapter" };
    faam_gapless_info gapless = { .struct_size = sizeof(gapless), .encoder_delay = 1024,
        .end_padding = 512, .total_samples = 6656 };
    faam_muxer_config cfg;
    OK(faam_muxer_config_init(&cfg, sizeof(cfg)));
    cfg.tracks = &track; cfg.num_tracks = 1; cfg.metadata = entries; cfg.num_metadata = sizeof(entries) / sizeof(entries[0]);
    cfg.chapters = &chapter; cfg.num_chapters = 1; cfg.gapless = &gapless;
    cfg.flags = flags; cfg.fragment_ms = fragmented ? 50 : 0;
    faam_io io = file_io(f);
    faam_muxer *m = NULL;
    OK(faam_muxer_open(&cfg, &io, &m));
    for (unsigned i = 0; i < 8; i++) OK(faam_muxer_write_frame(m, 1, frame, sizeof(frame), 1024, 0, 0));
    OK(faam_muxer_finalize(m)); OK(faam_muxer_close(&m));
}
static faam_demuxer *open_demux(FILE *f) {
    faam_demuxer *d = NULL; faam_io io = file_io(f);
    OK(faam_demuxer_open(NULL, &io, &d)); return d;
}
static uint32_t count_key(faam_demuxer *d, uint32_t key, const char *mean, const char *name,
                           const void *data, uint32_t len) {
    uint32_t m_count = 0; OK(faam_demuxer_get_num_metadata(d, &m_count));
    uint32_t count = 0;
    for (uint32_t i = 0; i < m_count; i++) {
        faam_metadata_entry e = { .struct_size = sizeof(e) }; OK(faam_demuxer_get_metadata_entry(d, i, &e));
        if (e.key != key || (name && (!e.name || strcmp(name, e.name))) ||
            (mean && (!e.mean || strcmp(mean, e.mean)))) continue;
        if (data) CHECK(e.data_bytes == len && !memcmp(e.data, data, len));
        if (key == UNKNOWN) CHECK(e.locale == 0x12345678 && e.data_type == 0x123456);
        count++;
    }
    return count;
}
static void verify_media(FILE *f, bool gapless) {
    faam_demuxer *d = open_demux(f);
    uint32_t chapters = 0; OK(faam_demuxer_get_num_chapters(d, &chapters)); CHECK(chapters == 1);
    faam_chapter ch = { .struct_size = sizeof(ch) }; OK(faam_demuxer_get_chapter(d, 0, &ch)); CHECK(!strcmp(ch.title, "Chapter"));
    if (gapless) {
        faam_gapless_info g = { .struct_size = sizeof(g) }; OK(faam_demuxer_get_track_gapless(d, 0, &g));
        CHECK(g.encoder_delay == 1024 && g.end_padding == 512 && g.total_samples == 6656);
    }
    faam_frame_loc first = { .struct_size = sizeof(first) };
    faam_frame_loc again = { .struct_size = sizeof(again) };
    OK(faam_demuxer_next_frame_loc(d, &first));
    OK(faam_demuxer_next_frame_loc(d, &again));
    CHECK(first.file_offset == again.file_offset && first.frame_bytes == sizeof(frame));
    uint8_t short_buf[1]; uint32_t bytes = 0;
    CHECK(faam_demuxer_read_frame(d, short_buf, sizeof(short_buf), &bytes) == FAAM_ERR_OUTPUT_TOO_SMALL);
    CHECK(bytes == sizeof(frame));
    OK(faam_demuxer_next_frame_loc(d, &again));
    CHECK(first.file_offset == again.file_offset); /* Retry leaves the frame pending. */
    OK(faam_demuxer_read_frame(d, NULL, 0, &bytes)); /* NULL skips and consumes it. */
    OK(faam_demuxer_next_frame_loc(d, &again));
    CHECK(first.file_offset != again.file_offset);
    unsigned frames = 1;
    for (;;) {
        uint8_t buf[32]; uint32_t bytes = 0;
        faam_status st = faam_demuxer_read_frame(d, buf, sizeof(buf), &bytes);
        if (st == FAAM_END_OF_STREAM) break;
        OK(st); CHECK(bytes == sizeof(frame) && !memcmp(buf, frame, bytes)); frames++;
    }
    CHECK(frames == 8); OK(faam_demuxer_close(&d));
}
static void exercise(uint32_t flags) {
    FILE *f = tmpfile(); CHECK(f); fixture(f, flags, false);
    faam_io io = file_io(f);
    faam_demuxer *d = open_demux(f);
    CHECK(count_key(d, LYRICS, NULL, NULL, initial_lyrics, sizeof(initial_lyrics) - 1) == 1);
    CHECK(count_key(d, FREEFORM, "org.example", "Identifier", NULL, 0) == 2);
    CHECK(count_key(d, UNKNOWN, NULL, NULL, binary, sizeof(binary)) == 1);
    /* A larger caller buffer reports the library struct size and keeps the bytes past it. */
    struct { faam_metadata_entry entry; uint8_t canary[8]; } output;
    memset(&output, 0xa5, sizeof(output)); output.entry.struct_size = sizeof(output);
    OK(faam_demuxer_get_metadata_entry(d, 0, &output.entry));
    CHECK(output.entry.struct_size == sizeof(faam_metadata_entry));
    for (unsigned i = 0; i < sizeof(output.canary); i++) CHECK(output.canary[i] == 0xa5);
    output.entry.struct_size = FAAM_METADATA_ENTRY_BASELINE - 1;
    CHECK(faam_demuxer_get_metadata_entry(d, 0, &output.entry) == FAAM_ERR_INVALID_ARGUMENT);
    OK(faam_demuxer_close(&d));

    /* Unknown keys/types/locales and every cover value survive a large growth. */
    char *long_text = malloc(100001); CHECK(long_text); memset(long_text, 'x', 100000); long_text[100000] = 0;
    faam_metadata_entry change = value(LYRICS, FAAM_METADATA_UTF8, long_text, 100000);
    OK(faam_update_metadata_stream(&io, &change, 1, 0));
    d = open_demux(f);
    CHECK(count_key(d, LYRICS, NULL, NULL, long_text, 100000) == 1);
    CHECK(count_key(d, UNKNOWN, NULL, NULL, binary, sizeof(binary)) == 1);
    CHECK(count_key(d, FAAM_METADATA_KEY_COVER_ART, NULL, NULL, NULL, 0) == 2);
    OK(faam_demuxer_close(&d)); free(long_text); verify_media(f, true);

    /* Target a freeform namespace without affecting another namespace or lyrics. */
    faam_metadata_entry replacement[2] = { value(FREEFORM, 1, "one", 3), value(FREEFORM, 1, "two", 3) };
    for (unsigned i = 0; i < 2; i++) { replacement[i].name = "Identifier"; replacement[i].mean = "org.example"; }
    OK(faam_update_metadata_stream(&io, replacement, 2, 0));
    d = open_demux(f);
    CHECK(count_key(d, FREEFORM, "org.example", "Identifier", NULL, 0) == 2);
    CHECK(count_key(d, FREEFORM, "other.example", "Identifier", binary, sizeof(binary)) == 1);
    OK(faam_demuxer_close(&d));
    replacement[0].flags = FAAM_METADATA_REMOVE; replacement[0].data = NULL; replacement[0].data_bytes = 0;
    OK(faam_update_metadata_stream(&io, replacement, 1, 0));
    d = open_demux(f); CHECK(count_key(d, FREEFORM, "org.example", "Identifier", NULL, 0) == 0); OK(faam_demuxer_close(&d));

    /* Invalid edits fail before touching the file. */
    CHECK(sk(f, 0)); CHECK(fseek(f, 0, SEEK_END) == 0); long before = ftell(f);
    unsigned writes_before = writes;
    CHECK(faam_update_metadata_stream(&io, replacement, 2, 0) == FAAM_ERR_INVALID_ARGUMENT);
    change.data_type = 0x1000000;
    CHECK(faam_update_metadata_stream(&io, &change, 1, 0) == FAAM_ERR_INVALID_ARGUMENT);
    CHECK(fseek(f, 0, SEEK_END) == 0 && ftell(f) == before);
    CHECK(faam_update_metadata_stream(&io, NULL, 1, 0) == FAAM_ERR_INVALID_ARGUMENT);
    CHECK(faam_update_metadata_stream(&io, NULL, 0, 2) == FAAM_ERR_INVALID_ARGUMENT);
    change = value(FREEFORM, 1, "data", 4); /* missing name */
    CHECK(faam_update_metadata_stream(&io, &change, 1, 0) == FAAM_ERR_INVALID_ARGUMENT);
    change = value(LYRICS, 1, "data", 4); change.mean = "invalid-for-native-key";
    CHECK(faam_update_metadata_stream(&io, &change, 1, 0) == FAAM_ERR_INVALID_ARGUMENT);
    change = value(LYRICS, 1, binary, UINT32_MAX);
    CHECK(faam_update_metadata_stream(&io, &change, 1, 0) == FAAM_ERR_UNSUPPORTED);
    CHECK(writes == writes_before);
    OK(faam_update_metadata_stream(&io, NULL, 0, 0));

    /* Empty text is an explicit empty data value, distinct from removal. */
    change = value(LYRICS, 1, NULL, 0); OK(faam_update_metadata_stream(&io, &change, 1, 0));
    d = open_demux(f); CHECK(count_key(d, LYRICS, NULL, NULL, "", 0) == 1); OK(faam_demuxer_close(&d));
    change.flags = FAAM_METADATA_REMOVE; OK(faam_update_metadata_stream(&io, &change, 1, 0));
    d = open_demux(f); CHECK(count_key(d, LYRICS, NULL, NULL, NULL, 0) == 0); OK(faam_demuxer_close(&d));
    verify_media(f, true);
    OK(faam_update_metadata_stream(&io, NULL, 0, FAAM_METADATA_UPDATE_CLEAR));
    d = open_demux(f); uint32_t m_count = 0; OK(faam_demuxer_get_num_metadata(d, &m_count)); CHECK(!m_count);
    OK(faam_demuxer_close(&d)); verify_media(f, false); fclose(f);
}
static void exercise_future_entries(void) {
    FILE *f = tmpfile(); CHECK(f); fixture(f, 0, false);
    faam_io io = file_io(f);
    struct future_entry { faam_metadata_entry e; uint64_t future[2]; } entries[2];
    memset(entries, 0xa5, sizeof(entries));
    entries[0].e = value(LYRICS, FAAM_METADATA_UTF8, "a", 1);
    entries[1].e = value(LYRICS, FAAM_METADATA_UTF8, "b", 1);
    entries[0].e.struct_size = entries[1].e.struct_size = sizeof(entries[0]);
    OK(faam_update_metadata_stream(&io, &entries[0].e, 2, 0));
    faam_demuxer *d = open_demux(f);
    CHECK(count_key(d, LYRICS, NULL, NULL, NULL, 0) == 2);
    OK(faam_demuxer_close(&d));
    entries[1].e.struct_size = sizeof(faam_metadata_entry);
    CHECK(faam_update_metadata_stream(&io, &entries[0].e, 2, 0) == FAAM_ERR_INVALID_ARGUMENT);

    /* The default freeform namespace and its explicit spelling address one key. */
    faam_metadata_entry e = value(FREEFORM, FAAM_METADATA_UTF8, "first", 5);
    e.name = "DefaultNamespace"; e.mean = "";
    OK(faam_update_metadata_stream(&io, &e, 1, 0));
    e.mean = "com.apple.iTunes"; e.data = "second"; e.data_bytes = 6;
    OK(faam_update_metadata_stream(&io, &e, 1, 0));
    d = open_demux(f); CHECK(count_key(d, FREEFORM, "com.apple.iTunes", e.name, "second", 6) == 1);
    OK(faam_demuxer_close(&d)); fclose(f);
}

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "--fixture")) {
        FILE *f = fopen(argv[2], "w+b"); CHECK(f); fixture(f, 0, false); CHECK(fclose(f) == 0); return 0;
    }
    CHECK(argc == 1); exercise_future_entries(); exercise(0); exercise(FAAM_MUXER_FASTSTART);
    faam_library_info info = { .struct_size = sizeof(info) }; OK(faam_get_library_info(&info));
    if (info.flags & FAAM_LIB_FRAGMENTED) {
        FILE *f = tmpfile(); CHECK(f); fixture(f, 0, true); verify_media(f, true); fclose(f);
    }
    puts("generic metadata contracts passed"); return 0;
}
