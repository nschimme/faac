/*
 * FAAD - Freeware Advanced Audio Decoder
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
 * MP4 reader for the faad frontend: a thin adapter over the libfaam demuxer
 * that finds the first AAC track and exposes its AudioSpecificConfig, sample
 * locations, gapless trim and tags through either file or memory callbacks.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "faam.h"
#include "mp4read.h"
#include "cli_io.h"

typedef struct {
    const uint8_t *buf;
    uint64_t size;
    uint64_t pos;
} mem_stream;

static int32_t mem_read(void *user, void *dst, uint32_t bytes)
{
    mem_stream *m = (mem_stream *)user;
    uint64_t avail = m->pos < m->size ? m->size - m->pos : 0;
    uint32_t n = bytes < avail ? bytes : (uint32_t)avail;
    memcpy(dst, m->buf + m->pos, n);
    m->pos += n;
    return (int32_t)n;
}

static bool mem_seek(void *user, uint64_t offset)
{
    mem_stream *m = (mem_stream *)user;
    if (offset > m->size) return false;
    m->pos = offset;
    return true;
}

static uint64_t mem_tell(void *user)
{
    return ((mem_stream *)user)->pos;
}

static void add_tag(MP4Track *out, const char *name, const char *value)
{
    MP4Tag *t = (MP4Tag *)realloc(out->tags, ((size_t)out->num_tags + 1) * sizeof(*t));
    if (!t) return;
    out->tags = t;
    t += out->num_tags;
    t->name = strdup(name);
    t->value = strdup(value);
    if (!t->name || !t->value) { free(t->name); free(t->value); return; }
    out->num_tags++;
}

static void add_text(MP4Track *out, const char *label, const char *value)
{
    if (value) add_tag(out, label, value);
}

static void add_index(MP4Track *out, const char *label, uint16_t num, uint16_t total)
{
    char buf[32];
    if (!num && !total) return;
    snprintf(buf, sizeof(buf), "%u/%u", num, total);
    add_tag(out, label, buf);
}

/* Listed in the order the faac muxer writes them, so a faac file reads back
 * in the order it was tagged. */
static void add_tags(MP4Track *out, faam_demuxer *d, const faam_metadata *m)
{
    add_text(out, "Encoder", m->encoder);
    add_text(out, "Artist", m->artist);
    add_text(out, "Artist Sort", m->artist_sort);
    add_text(out, "Composer", m->composer);
    add_text(out, "Composer Sort", m->composer_sort);
    add_text(out, "Title", m->title);
    add_text(out, "Album", m->album);
    add_text(out, "Album Artist", m->album_artist);
    add_text(out, "Album Artist Sort", m->album_artist_sort);
    add_text(out, "Album Sort", m->album_sort);
    add_text(out, "Genre", m->genre_str);
    add_text(out, "Year", m->year);
    add_text(out, "Comment", m->comment);
    add_text(out, "Title Sort", m->title_sort);
    if (m->genre_code) {
        char buf[32];
        snprintf(buf, sizeof(buf), "#%u", m->genre_code - 1u);
        add_tag(out, "Genre", buf);
    }
    if (m->compilation) add_tag(out, "Compilation", "Yes");
    add_index(out, "Track", m->track_num, m->track_total);
    add_index(out, "Disc", m->disc_num, m->disc_total);
    for (uint32_t i = 0; i < m->num_custom_tags; i++) {
        faam_custom_tag c = { .struct_size = sizeof(c) };
        char label[64];
        if (faam_demuxer_get_custom_tag(d, i, &c) != FAAM_OK) continue;
        if (!c.name || !c.value || !c.value[0]) continue;
        snprintf(label, sizeof(label), "%s", c.name);
        add_tag(out, label, c.value);
    }
    out->cover_bytes = m->cover_bytes;
}

/* Samples of the chosen track, stopping at the first one outside the file so
 * a corrupt table can't point the decoder off-buffer. */
static void collect_samples(faam_demuxer *d, uint32_t track_id, uint64_t file_size, MP4Track *out)
{
    uint32_t cap = 0;
    faam_frame_loc loc = { .struct_size = sizeof(loc) };
    while (faam_demuxer_next_frame_loc(d, &loc) == FAAM_OK) {
        uint32_t skipped;
        if (faam_demuxer_read_frame(d, NULL, 0, &skipped) != FAAM_OK) break;
        if (loc.track_id != track_id) continue;
        if (loc.file_offset > file_size || loc.frame_bytes > file_size - loc.file_offset) break;
        if (out->num_samples == cap) {
            if (cap > UINT32_MAX / 2) break;
            uint32_t ncap = cap ? cap * 2 : 1024;
            if ((uint64_t)ncap * sizeof(MP4Sample) > SIZE_MAX) break;
            MP4Sample *p = (MP4Sample *)realloc(out->samples, (size_t)ncap * sizeof(*p));
            if (!p) break;
            out->samples = p;
            cap = ncap;
        }
        out->samples[out->num_samples].offset = loc.file_offset;
        out->samples[out->num_samples].size = loc.frame_bytes;
        out->num_samples++;
    }
}

static bool read_track_io(faam_io *io, const uint8_t *prefix, uint64_t file_size,
                          uint32_t want_track_id, MP4Track *track)
{
    memset(track, 0, sizeof(*track));
    faam_demuxer *d = NULL;
    if (faam_demuxer_open(NULL, io, &d) != FAAM_OK)
        return !memcmp(prefix + 4, "ftyp", 4) || !memcmp(prefix + 4, "moov", 4);

    uint32_t num_tracks = 0, total_tracks = 0;
    faam_demuxer_get_num_tracks(d, &num_tracks, &total_tracks);
    track->num_tracks = total_tracks;
    track->num_listed = num_tracks < MP4_MAX_TRACK_SUMMARIES ? num_tracks : MP4_MAX_TRACK_SUMMARIES;

    uint32_t track_id = 0;
    uint8_t asc[FAAM_CODEC_DATA_MAX];
    for (uint32_t i = 0; i < num_tracks; i++) {
        faam_track_info info = { 0 };
        uint32_t asc_len = 0;
        info.struct_size = sizeof(info);
        if (faam_demuxer_get_track_info(d, i, &info) != FAAM_OK) continue;
        bool usable = info.track_type == FAAM_TRACK_AUDIO && info.codec_id == FAAM_CODEC_AAC &&
                      faam_demuxer_get_codec_data(d, info.track_id, asc, sizeof(asc), &asc_len) == FAAM_OK && asc_len;
        if (i < MP4_MAX_TRACK_SUMMARIES) {
            MP4TrackSummary *sum = &track->summaries[i];
            sum->id = info.track_id;
            sum->fourcc = info.fourcc;
            sum->kind = info.track_type == FAAM_TRACK_AUDIO ? 'a' : info.track_type == FAAM_TRACK_VIDEO ? 'v' : 'o';
            sum->decodable = usable;
        }
        if (track_id || !usable || (want_track_id && info.track_id != want_track_id)) continue;
        track->asc_buf = (uint8_t *)malloc(asc_len);
        if (!track->asc_buf) break;
        memcpy(track->asc_buf, asc, asc_len);
        track->asc_len = asc_len;
        track->timescale = info.timescale;
        track_id = info.track_id;
        track->track_id = track_id;
    }

    if (track_id) {
        char brand[5];
        faam_gapless_info gapless = { .struct_size = sizeof(gapless) };
        faam_metadata meta = { .struct_size = sizeof(meta) };
        if (faam_demuxer_get_major_brand(d, brand) == FAAM_OK) {
            size_t n = strlen(brand);
            while (n && brand[n - 1] == ' ') brand[--n] = '\0';
            memcpy(track->major_brand, brand, n + 1);
        }
        if (faam_demuxer_get_track_gapless(d, track_id, &gapless) == FAAM_OK) {
            track->delay = gapless.encoder_delay;
            track->padding = gapless.end_padding;
        }
        if (faam_demuxer_get_metadata(d, &meta) == FAAM_OK) add_tags(track, d, &meta);
        collect_samples(d, track_id, file_size, track);
    }

    faam_demuxer_close(&d);
    /* A valid MP4 without AAC audio is still an MP4: the caller reports that.
     * Without any track the input is not a container at all (e.g. ADTS), so
     * hand it back to be treated as a raw stream. */
    return total_tracks > 0;
}

bool mp4_read_track_buf(const uint8_t *buf, uint64_t file_size, uint32_t want_track_id, MP4Track *track)
{
    memset(track, 0, sizeof(*track));
    if (!buf || file_size < 32) return false;
    mem_stream ms = { buf, file_size, 0 };
    faam_io io = { sizeof(io), &ms, mem_read, NULL, mem_seek, mem_tell, NULL };
    return read_track_io(&io, buf, file_size, want_track_id, track);
}

bool mp4_read_track_file(FILE *f, uint64_t file_size, uint32_t want_track_id, MP4Track *track)
{
    uint8_t prefix[32];
    memset(track, 0, sizeof(*track));
    if (!cli_fseek(f, 0) || fread(prefix, 1, sizeof(prefix), f) != sizeof(prefix) ||
        !cli_fseek(f, 0)) return false;
    faam_io io = cli_faam_io(f);
    return read_track_io(&io, prefix, file_size, want_track_id, track);
}

void mp4_free_track(MP4Track *track)
{
    for (uint32_t i = 0; i < track->num_tags; i++) {
        free(track->tags[i].name);
        free(track->tags[i].value);
    }
    free(track->tags);
    free(track->asc_buf);
    free(track->samples);
    memset(track, 0, sizeof(*track));
}
