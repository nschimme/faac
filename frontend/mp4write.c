/*
 * FAAC - Freeware Advanced Audio Coder
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

/* Large-file semantics for the fopen() below, not only for the seeks in cli_io.c: on 32-bit
   POSIX a plain fopen() fails with EFBIG once an M4A passes 2 GiB. */
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define access _access
#define W_OK 2
#else
#include <unistd.h>
#endif

#include "faam.h"
#include "cli_io.h"
#include "mp4write.h"
#ifdef _WIN32
#include "charset.h"
#endif

enum {
    MP4_TRACK_ID = 1, /* single-track file */

    MP4_IO_BUFSIZE = 65536, /* stdio buffer for the mdat write path, see mp4_open() */
};

/* The muxer is created on the first frame, not in mp4_open(): the sample rate,
   channel count and sample size arrive after mp4_open(), and it fixes them
   when it writes the file header. Everything that is only needed for the
   moov (decoder config, tags, gapless, creation time) is applied in
   mp4_finish(), because the encoder only reports its AudioSpecificConfig then. */
static struct {
    uint32_t samplerate;
    uint32_t channels;
    uint32_t bits;
    bool constant_rate;

    struct {
        const uint8_t *data;
        unsigned long size;
    } asc;

    FILE *fout;
    faam_muxer *mux;
    faam_metadata meta;

    uint32_t creation_time;
    const char *encoder;
    const char *language;
    const char *tags[MP4TAG_COUNT];
    bool compilation;
    uint16_t trackno;
    uint16_t ntracks;
    uint16_t discno;
    uint16_t ndiscs;
    uint16_t genre;

    struct {
        const uint8_t *data;
        uint32_t size;
    } cover;

    struct {
        bool present;
        uint32_t priming;
        uint32_t padding;
        uint64_t original_samples;
    } gapless;

    faam_custom_tag *custom;
    uint32_t customcnt;
    uint32_t customcap;
} g_mp4 = { 0 };

/* Resets per-output-file write state: the muxer and the open output handle.
   Tag config (named metadata, custom tags via mp4_add_custom_tag()) is set by
   the caller and may happen before *or* after mp4_open() depending on the
   frontend, so it must survive this reset -- only mp4_close() clears it,
   once the caller is actually done with this muxer session. */
static void reset_write_state(void) {
    faam_muxer_close(&g_mp4.mux);
    if (g_mp4.fout) {
        fclose(g_mp4.fout);
        g_mp4.fout = NULL;
    }
    memset(&g_mp4.gapless, 0, sizeof(g_mp4.gapless));
}

int mp4_open(const char *path, bool overwrite) {
    reset_write_state(); /* in case of a retry after a failed previous mp4_open() */

#ifdef _WIN32
    if (!overwrite && win32_access_utf8(path, 0) == 0) return 1;
    g_mp4.fout = win32_fopen_utf8(path, "wb");
#else
    if (!overwrite && access(path, 0) == 0) return 1;
    g_mp4.fout = fopen(path, "wb");
#endif
    if (!g_mp4.fout) return 1;
    setvbuf(g_mp4.fout, NULL, _IOFBF, MP4_IO_BUFSIZE);
    return 0;
}

static bool open_muxer(void) {
    if (g_mp4.mux) return true;
    if (!g_mp4.fout) return false;

    faam_muxer_config cfg;
    if (faam_muxer_config_init(&cfg, sizeof(cfg)) != FAAM_OK) return false;
    if (g_mp4.constant_rate) cfg.flags |= FAAM_MUXER_CONSTANT_RATE;
    cfg.metadata = &g_mp4.meta; /* filled in by mp4_finish(), which the muxer reads when it finalizes */

    faam_track_config track = { .struct_size = sizeof(track) };
    track.track_type = FAAM_TRACK_AUDIO;
    track.codec_id = FAAM_CODEC_AAC;
    track.track_id = MP4_TRACK_ID;
    track.timescale = g_mp4.samplerate;
    track.sample_rate = g_mp4.samplerate;
    track.channels = g_mp4.channels;
    cfg.tracks = &track;
    cfg.num_tracks = 1;

    /* The muxer only writes; offsets are 64-bit so an M4A past 2 GiB still patches its mdat size and stco. */
    faam_io io = cli_faam_io(g_mp4.fout);
    io.read = NULL;
    if (faam_muxer_open(&cfg, &io, &g_mp4.mux) != FAAM_OK) return false;
    if (g_mp4.bits) {
        faam_muxer_update_params update = { .struct_size = sizeof(update), .flags = FAAM_UPDATE_AUDIO_SAMPLE_SIZE };
        update.track_id = MP4_TRACK_ID;
        update.audio_sample_size = (uint16_t)g_mp4.bits;
        faam_muxer_update(g_mp4.mux, &update);
    }
    return true;
}

/* ISO/IEC 14496-1 spells a constant-rate stream as maxBitrate == avgBitrate. */
void mp4_set_constant_rate(bool constant) { g_mp4.constant_rate = constant; }

void mp4_set_creation_time(uint32_t t) { g_mp4.creation_time = t; }

void mp4_set_format(uint32_t samplerate, uint32_t channels, uint32_t bits) {
    g_mp4.samplerate = samplerate;
    g_mp4.channels = channels;
    g_mp4.bits = bits;
}

void mp4_set_decoder_config(const uint8_t *asc, unsigned long size) {
    g_mp4.asc.data = asc;
    g_mp4.asc.size = size;
}

void mp4_set_encoder(const char *value) { g_mp4.encoder = value; }

void mp4_set_tag(mp4_tag_id_t id, const char *value) {
    if (id < MP4TAG_COUNT)
        g_mp4.tags[id] = value;
}

void mp4_set_genre(uint16_t genre) { g_mp4.genre = genre; }

void mp4_set_language(const char *lang) { g_mp4.language = lang; }

void mp4_set_compilation(bool flag) { g_mp4.compilation = flag; }

void mp4_set_track(uint16_t num, uint16_t total) {
    g_mp4.trackno = num;
    g_mp4.ntracks = total;
}

void mp4_set_disc(uint16_t num, uint16_t total) {
    g_mp4.discno = num;
    g_mp4.ndiscs = total;
}

void mp4_set_cover(const uint8_t *data, uint32_t size) {
    g_mp4.cover.data = data;
    g_mp4.cover.size = size;
}

void mp4_set_gapless(uint32_t priming, uint32_t padding, uint64_t original_samples) {
    g_mp4.gapless.present = true;
    g_mp4.gapless.priming = priming;
    g_mp4.gapless.padding = padding;
    g_mp4.gapless.original_samples = original_samples;
}

int mp4_add_custom_tag(const char *name, const char *value) {
    if (g_mp4.customcnt >= g_mp4.customcap) {
        uint32_t new_cap = g_mp4.customcap ? g_mp4.customcap * 2 : 8;
        void *tmp = realloc(g_mp4.custom, (size_t)new_cap * sizeof(*g_mp4.custom));
        if (!tmp) return -1;
        g_mp4.custom = tmp;
        g_mp4.customcap = new_cap;
    }
    /* Callers free their strings once this returns, so keep our own copies. */
    char *name_copy = strdup(name);
    char *value_copy = strdup(value);
    if (!name_copy || !value_copy) {
        free(name_copy);
        free(value_copy);
        return -1;
    }
    g_mp4.custom[g_mp4.customcnt].struct_size = sizeof(g_mp4.custom[0]);
    g_mp4.custom[g_mp4.customcnt].mean = "faac"; /* master's namespace */
    g_mp4.custom[g_mp4.customcnt].name = name_copy;
    g_mp4.custom[g_mp4.customcnt].value = value_copy;
    g_mp4.customcnt++;
    return 0;
}

int mp4_write_frame(const uint8_t *data, uint32_t size, uint32_t samples) {
    if (!open_muxer()) return -1;
    return faam_muxer_write_frame(g_mp4.mux, MP4_TRACK_ID, data, size, samples, 0, FAAM_FRAME_KEYFRAME) == FAAM_OK ? 0 : -1;
}

/* Returns 0 on success, 1 on failure (mirroring mp4_open()'s convention). */
int mp4_finish(void) {
    if (!open_muxer()) return 1;

    faam_metadata *m = &g_mp4.meta;
    memset(m, 0, sizeof(*m));
    m->struct_size = sizeof(*m);
    m->encoder = g_mp4.encoder;
    m->artist = g_mp4.tags[MP4TAG_ARTIST];
    m->artist_sort = g_mp4.tags[MP4TAG_ARTISTSORT];
    m->composer = g_mp4.tags[MP4TAG_COMPOSER];
    m->composer_sort = g_mp4.tags[MP4TAG_COMPOSERSORT];
    m->title = g_mp4.tags[MP4TAG_TITLE];
    m->album = g_mp4.tags[MP4TAG_ALBUM];
    m->album_artist = g_mp4.tags[MP4TAG_ALBUMARTIST];
    m->album_artist_sort = g_mp4.tags[MP4TAG_ALBUMARTISTSORT];
    m->album_sort = g_mp4.tags[MP4TAG_ALBUMSORT];
    m->genre_str = g_mp4.tags[MP4TAG_GENRE];
    m->genre_code = g_mp4.genre;
    m->year = g_mp4.tags[MP4TAG_YEAR];
    m->comment = g_mp4.tags[MP4TAG_COMMENT];
    m->compilation = g_mp4.compilation;
    m->track_num = g_mp4.trackno;
    m->track_total = g_mp4.ntracks;
    m->disc_num = g_mp4.discno;
    m->disc_total = g_mp4.ndiscs;
    m->cover_art = g_mp4.cover.data;
    m->cover_bytes = g_mp4.cover.size;
    /* cover_type stays AUTO: a PNG is labelled PNG, where master labelled every cover JPEG. */
    m->custom_tags = g_mp4.custom;
    m->num_custom_tags = g_mp4.customcnt;

    faam_gapless_info gapless = {
        .struct_size = sizeof(gapless),
        .encoder_delay = g_mp4.gapless.priming,
        .end_padding = g_mp4.gapless.padding,
        .total_samples = g_mp4.gapless.original_samples,
    };
    faam_muxer_update_params update = { .struct_size = sizeof(update) };
    update.flags = FAAM_UPDATE_CODEC_DATA | FAAM_UPDATE_LANGUAGE | FAAM_UPDATE_CREATION_TIME;
    update.track_id = MP4_TRACK_ID;
    update.codec_data = g_mp4.asc.data;
    update.codec_data_len = (uint32_t)g_mp4.asc.size;
    update.creation_time = g_mp4.creation_time;
    for (unsigned i = 0; g_mp4.language && i < 3 && g_mp4.language[i]; i++) update.language[i] = g_mp4.language[i];
    if (g_mp4.gapless.present) {
        update.flags |= FAAM_UPDATE_GAPLESS;
        update.gapless = &gapless;
    }
    if (faam_muxer_update(g_mp4.mux, &update) != FAAM_OK) return 1;
    return faam_muxer_finalize(g_mp4.mux) == FAAM_OK ? 0 : 1;
}

/* Custom tags (mp4_add_custom_tag()) are freed here, not in
   reset_write_state(), so mp4_open() doesn't wipe them if a caller sets
   them before the first open. A future multi-file/batch session reusing
   this muxer across encodes would need to re-add custom tags after each
   mp4_close() -- they don't survive it. */
int mp4_close(void) {
    reset_write_state();
    for (uint32_t i = 0; i < g_mp4.customcnt; i++) {
        free((void *)g_mp4.custom[i].name);
        free((void *)g_mp4.custom[i].value);
    }
    free(g_mp4.custom);
    g_mp4.custom = NULL;
    g_mp4.customcnt = 0;
    g_mp4.customcap = 0;
    return 0;
}

static bool track_info(faam_muxer_info *info) {
    info->struct_size = sizeof(*info);
    return g_mp4.mux && faam_muxer_get_info(g_mp4.mux, MP4_TRACK_ID, info) == FAAM_OK;
}

uint32_t mp4_frame_count(void) {
    faam_muxer_info info;
    return track_info(&info) ? info.frame_count : 0;
}

uint64_t mp4_sample_count(void) {
    faam_muxer_info info;
    return track_info(&info) ? info.duration_ticks : 0;
}

uint32_t mp4_max_bitrate(void) {
    faam_muxer_info info;
    return track_info(&info) ? info.max_bitrate : 0;
}

uint32_t mp4_avg_bitrate(void) {
    faam_muxer_info info;
    return track_info(&info) ? info.avg_bitrate : 0;
}

uint16_t mp4_max_frame_size(void) {
    faam_muxer_info info;
    return track_info(&info) ? (uint16_t)info.max_frame_size : 0;
}
