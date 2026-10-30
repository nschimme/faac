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
#include "charset.h"
#include "endian.h"

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

static void add_index(MP4Track *out, const char *label, uint16_t num, uint16_t total)
{
    char buf[32];
    if (!num && !total) return;
    snprintf(buf, sizeof(buf), "%u/%u", num, total);
    add_tag(out, label, buf);
}

/* Display names belong to the frontend; libfaam exposes typed atom values. */
static void add_tags(MP4Track *out, faam_demuxer *d, uint32_t count)
{
    static const struct { uint32_t key; const char *label; } labels[] = {
        { FAAM_METADATA_KEY_ENCODER, "Encoder" }, { FAAM_METADATA_KEY_ARTIST, "Artist" },
        { FAAM_METADATA_KEY_ARTIST_SORT, "Artist Sort" }, { FAAM_METADATA_KEY_COMPOSER, "Composer" },
        { FAAM_METADATA_KEY_COMPOSER_SORT, "Composer Sort" }, { FAAM_METADATA_KEY_TITLE, "Title" },
        { FAAM_METADATA_KEY_ALBUM, "Album" }, { FAAM_METADATA_KEY_ALBUM_ARTIST, "Album Artist" },
        { FAAM_METADATA_KEY_ALBUM_ARTIST_SORT, "Album Artist Sort" },
        { FAAM_METADATA_KEY_ALBUM_SORT, "Album Sort" }, { FAAM_METADATA_KEY_GENRE, "Genre" },
        { FAAM_METADATA_KEY_YEAR, "Year" }, { FAAM_METADATA_KEY_COMMENT, "Comment" },
        { FAAM_METADATA_KEY_TITLE_SORT, "Title Sort" }, { FAAM_METADATA_KEY_LYRICS, "Lyrics" },
        { FAAM_METADATA_KEY_GROUPING, "Grouping" }, { FAAM_METADATA_KEY_COPYRIGHT, "Copyright" },
        { FAAM_METADATA_KEY_DESCRIPTION, "Description" },
        { FAAM_METADATA_KEY_LONG_DESCRIPTION, "Long Description" },
        { FAAM_METADATA_KEY_WORK, "Work" }, { FAAM_METADATA_KEY_MOVEMENT, "Movement" },
        { FAAM_METADATA_KEY_BPM, "BPM" }, { FAAM_METADATA_KEY_MOVEMENT_NUMBER, "Movement Number" },
        { FAAM_METADATA_KEY_MOVEMENT_TOTAL, "Movement Total" },
        { FAAM_METADATA_KEY_SHOW_WORK_MOVEMENT, "Show Work/Movement" }
    };
    for (uint32_t i = 0; i < count; i++) {
        faam_metadata_entry e = { .struct_size = sizeof(e) };
        if (faam_demuxer_get_metadata_entry(d, i, &e) != FAAM_OK) continue;
        const uint8_t *v = e.data;
        if (e.key == FAAM_METADATA_KEY_COVER_ART) { out->cover_bytes = e.data_bytes; continue; }
        if (e.key == FAAM_METADATA_KEY_TRACK && e.data_bytes >= 6)
            add_index(out, "Track", read_u16_be(v + 2), read_u16_be(v + 4));
        else if (e.key == FAAM_METADATA_KEY_DISC && e.data_bytes >= 6)
            add_index(out, "Disc", read_u16_be(v + 2), read_u16_be(v + 4));
        else if (e.key == FAAM_METADATA_KEY_COMPILATION && e.data_bytes && v[0]) add_tag(out, "Compilation", "Yes");
        else if (e.key == FAAM_METADATA_KEY_GENRE_CODE && e.data_bytes == 2 && read_u16_be(v)) {
            const char *name = id3_genre_name(read_u16_be(v) - 1u);
            char value[32];
            if (!name) {
                snprintf(value, sizeof(value), "#%u", read_u16_be(v) - 1u);
                name = value;
            }
            add_tag(out, "Genre", name);
        } else if (e.data_type == FAAM_METADATA_UTF8 && !memchr(v, 0, e.data_bytes)) {
            const char *label = e.name;
            if (label && !strcmp(label, "iTunSMPB") && (!e.mean || !strcmp(e.mean, "com.apple.iTunes"))) continue;
            for (size_t j = 0; !label && j < sizeof(labels) / sizeof(labels[0]); j++)
                if (e.key == labels[j].key) label = labels[j].label;
            if (label) add_tag(out, label, e.data);
        }
    }
}

/* Collect every sample of the chosen track, or report an incomplete index. */
static void collect_samples(faam_demuxer *d, uint32_t track_id, uint64_t file_size, MP4Track *out)
{
    uint32_t cap = 0;
    faam_frame_loc loc = { .struct_size = sizeof(loc) };
    faam_status status;
    out->sample_error = true;
    while ((status = faam_demuxer_next_frame_loc(d, &loc)) == FAAM_OK) {
        uint32_t skipped;
        if (faam_demuxer_read_frame(d, NULL, 0, &skipped) != FAAM_OK) return;
        if (loc.track_id != track_id) continue;
        if (loc.file_offset > file_size || loc.frame_bytes > file_size - loc.file_offset) return;
        if (out->num_samples == cap) {
            if (cap > UINT32_MAX / 2) return;
            uint32_t ncap = cap ? cap * 2 : 1024;
            if ((uint64_t)ncap * sizeof(MP4Sample) > SIZE_MAX) return;
            MP4Sample *p = (MP4Sample *)realloc(out->samples, (size_t)ncap * sizeof(*p));
            if (!p) return;
            out->samples = p;
            cap = ncap;
        }
        out->samples[out->num_samples].offset = loc.file_offset;
        out->samples[out->num_samples].size = loc.frame_bytes;
        out->num_samples++;
    }
    out->sample_error = status != FAAM_END_OF_STREAM;
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
        uint32_t meta_count = 0;
        if (faam_demuxer_get_major_brand(d, brand) == FAAM_OK) {
            size_t n = strlen(brand);
            while (n && brand[n - 1] == ' ') brand[--n] = '\0';
            memcpy(track->major_brand, brand, n + 1);
        }
        if (faam_demuxer_get_track_gapless(d, track_id, &gapless) == FAAM_OK) {
            track->delay = gapless.encoder_delay;
            track->padding = gapless.end_padding;
        }
        if (faam_demuxer_get_num_metadata(d, &meta_count) == FAAM_OK) add_tags(track, d, meta_count);
        if (track->delay == 0 && track->tags) {
            for (uint32_t i = 0; i < track->num_tags; i++) {
                if ((!strcmp(track->tags[i].name, "Encoder") || !strcmp(track->tags[i].name, "Tool")) &&
                    track->tags[i].value && strstr(track->tags[i].value, "FAAC 1.")) {
                    track->delay = 1024;
                    break;
                }
            }
        }
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
    faam_io io = { .struct_size = sizeof(io), .user_data = &ms, .read = mem_read, .write = NULL, .seek = mem_seek, .tell = mem_tell, .flush = NULL };
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
