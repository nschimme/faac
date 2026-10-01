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
 * Minimal audio-only MP4 reader for the faad frontend: finds the first AAC
 * track and exposes its AudioSpecificConfig, sample locations and gapless
 * trim. The whole file is already in memory, so boxes are walked in place.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "endian.h"
#include "mp4read.h"

typedef struct {
    const uint8_t *p;
    uint64_t len;
} span;

typedef struct {
    uint32_t timescale;
    uint64_t duration;
    span asc;
    span stsz, stsc, stco;
    bool co64;
    bool has_elst;
    uint64_t elst_dur, elst_time;
} trak_state;

typedef struct {
    const uint8_t *buf;
    uint64_t size;
    MP4Track *out;
    uint32_t movie_timescale;
    uint32_t num_traks;
    bool have_track;
    bool have_smpb;
    trak_state cur;
} reader;

static uint32_t rd32(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return htobe32(v);
}

static uint64_t rd64(const uint8_t *p) {
    return ((uint64_t)rd32(p) << 32) | rd32(p + 4);
}

/* Next child box of [*pos, end): its payload and type. */
static bool next_box(const uint8_t *buf, uint64_t *pos, uint64_t end, char type[4], span *payload)
{
    uint64_t p = *pos, size, hdr = 8;
    if (end - p < 8) return false;
    size = rd32(buf + p);
    memcpy(type, buf + p + 4, 4);
    if (size == 1) {
        if (end - p < 16) return false;
        size = rd64(buf + p + 8);
        hdr = 16;
    } else if (size == 0) {
        size = end - p;
    }
    if (size < hdr || size > end - p) return false;
    payload->p = buf + p + hdr;
    payload->len = size - hdr;
    *pos = p + size;
    return true;
}

/* MPEG-4 descriptor length: up to four 7-bit groups. */
static bool desc_head(span *s, uint8_t *tag, uint64_t *len)
{
    uint32_t n = 0;
    if (s->len < 2) return false;
    *tag = s->p[0];
    s->p++; s->len--;
    for (int i = 0; i < 4 && s->len; i++) {
        uint8_t b = *s->p++;
        s->len--;
        n = (n << 7) | (b & 0x7f);
        if (!(b & 0x80)) { *len = n; return n <= s->len; }
    }
    return false;
}

static void parse_esds(const span *esds, trak_state *t)
{
    span s = { esds->p, esds->len };
    uint8_t tag;
    uint64_t len;
    if (s.len < 4) return;
    s.p += 4; s.len -= 4;
    if (!desc_head(&s, &tag, &len) || tag != 0x03 || len < 3) return;
    uint8_t flags = s.p[2];
    uint64_t skip = 3;
    if (flags & 0x80) skip += 2;
    if (flags & 0x40) skip += 1 + (skip + 1 <= len ? s.p[skip] : 0);
    if (flags & 0x20) skip += 2;
    if (skip > len) return;
    s.p += skip; s.len = len - skip;
    if (!desc_head(&s, &tag, &len) || tag != 0x04 || len < 13 || s.p[0] != 0x40) return;
    s.p += 13; s.len = len - 13;
    if (desc_head(&s, &tag, &len) && tag == 0x05 && len) {
        t->asc.p = s.p;
        t->asc.len = len;
    }
}

static void parse_stsd(const span *stsd, trak_state *t)
{
    uint64_t pos = 8;
    char type[4];
    span entry;
    if (stsd->len < 8 || !next_box(stsd->p, &pos, stsd->len, type, &entry)) return;
    if (memcmp(type, "mp4a", 4) || entry.len < 28) return;
    /* QuickTime sound entries v1/v2 carry extra fields before the children. */
    uint32_t ver = (uint32_t)entry.p[8] << 8 | entry.p[9];
    uint64_t cur = 28 + (ver == 1 ? 16 : ver == 2 ? 36 : 0), end = entry.len;
    span child;
    while (next_box(entry.p, &cur, end, type, &child))
        if (!memcmp(type, "esds", 4)) parse_esds(&child, t);
}

static void parse_elst(const span *s, trak_state *t)
{
    if (s->len < 8 || t->has_elst) return;
    uint32_t ver = s->p[0], count = rd32(s->p + 4), esz = ver == 1 ? 20 : 12;
    uint64_t p = 8;
    for (uint32_t i = 0; i < count && s->len - p >= esz; i++, p += esz) {
        uint64_t dur, time, empty;
        if (ver == 1) { dur = rd64(s->p + p); time = rd64(s->p + p + 8); empty = ~0ULL; }
        else { dur = rd32(s->p + p); time = rd32(s->p + p + 4); empty = 0xFFFFFFFFULL; }
        if (time == empty) continue;
        t->elst_dur = dur;
        t->elst_time = time;
        t->has_elst = true;
        break;
    }
}

static void parse_mdhd(const span *s, trak_state *t)
{
    if (s->len < 4) return;
    if (s->p[0] == 1) {
        if (s->len < 32) return;
        t->timescale = rd32(s->p + 20);
        t->duration = rd64(s->p + 24);
    } else {
        if (s->len < 20) return;
        t->timescale = rd32(s->p + 12);
        t->duration = rd32(s->p + 16);
    }
}

static void add_tag(MP4Track *out, const char *name, const char *value, size_t vlen)
{
    MP4Tag *t = (MP4Tag *)realloc(out->tags, ((size_t)out->num_tags + 1) * sizeof(*t));
    if (!t) return;
    out->tags = t;
    t += out->num_tags;
    t->name = strdup(name);
    t->value = (char *)malloc(vlen + 1);
    if (!t->name || !t->value) { free(t->name); free(t->value); return; }
    memcpy(t->value, value, vlen);
    t->value[vlen] = 0;
    out->num_tags++;
}

/* iTunes gapless: " 00000000 <priming> <padding> <length> ..." */
static void parse_itun(const span *s, reader *r)
{
    uint64_t pos = 0;
    char type[4];
    span c, name = {0}, data = {0};
    while (next_box(s->p, &pos, s->len, type, &c)) {
        if (!memcmp(type, "name", 4) && c.len > 4) { name.p = c.p + 4; name.len = c.len - 4; }
        else if (!memcmp(type, "data", 4) && c.len > 8) { data.p = c.p + 8; data.len = c.len - 8; }
    }
    if (name.len && data.len && (name.len != 8 || memcmp(name.p, "iTunSMPB", 8))) {
        char label[64];
        size_t n = name.len < sizeof(label) - 1 ? name.len : sizeof(label) - 1;
        memcpy(label, name.p, n);
        label[n] = 0;
        add_tag(r->out, label, (const char *)data.p, data.len);
        return;
    }
    if (name.len != 8 || !data.len) return;
    char str[128] = { 0 };
    memcpy(str, data.p, data.len < sizeof(str) - 1 ? data.len : sizeof(str) - 1);
    unsigned delay, padding;
    if (sscanf(str, " %*x %x %x", &delay, &padding) == 2) {
        r->out->delay = delay;
        r->out->padding = padding;
        r->have_smpb = true;
    }
}

static const struct { const char *key; const char *label; } text_tags[] = {
    {"\xA9" "nam", "Title"}, {"\xA9" "ART", "Artist"}, {"\xA9" "alb", "Album"},
    {"aART", "Album Artist"}, {"\xA9" "wrt", "Composer"}, {"\xA9" "day", "Year"},
    {"\xA9" "cmt", "Comment"}, {"\xA9" "gen", "Genre"}, {"\xA9" "too", "Encoder"},
    {"sonm", "Title Sort"}, {"soar", "Artist Sort"}, {"soal", "Album Sort"},
    {"soaa", "Album Artist Sort"}, {"soco", "Composer Sort"},
};

static void parse_ilst(const span *s, reader *r)
{
    uint64_t pos = 0;
    char type[4];
    span item;
    while (next_box(s->p, &pos, s->len, type, &item)) {
        if (!memcmp(type, "----", 4)) {
            parse_itun(&item, r);
            continue;
        }
        /* item = data box: size, "data", type, locale, value */
        if (item.len < 16 || memcmp(item.p + 4, "data", 4)) continue;
        const uint8_t *v = item.p + 16;
        size_t n = (size_t)item.len - 16;
        char buf[32];
        if (!memcmp(type, "covr", 4)) {
            r->out->cover_bytes = (uint32_t)n;
            continue;
        }
        for (size_t i = 0; i < sizeof(text_tags) / sizeof(text_tags[0]); i++)
            if (!memcmp(type, text_tags[i].key, 4)) add_tag(r->out, text_tags[i].label, (const char *)v, n);
        if (!memcmp(type, "trkn", 4) || !memcmp(type, "disk", 4)) {
            if (n >= 6) {
                snprintf(buf, sizeof(buf), "%u/%u", (v[2] << 8) | v[3], (v[4] << 8) | v[5]);
                add_tag(r->out, type[0] == 't' ? "Track" : "Disc", buf, strlen(buf));
            }
        } else if (!memcmp(type, "gnre", 4) && n >= 2 && ((v[0] << 8) | v[1])) {
            snprintf(buf, sizeof(buf), "#%u", ((v[0] << 8) | v[1]) - 1);
            add_tag(r->out, "Genre", buf, strlen(buf));
        } else if (!memcmp(type, "cpil", 4) && n >= 1 && v[0]) {
            add_tag(r->out, "Compilation", "Yes", 3);
        }
    }
}

/* Chunk table -> per-sample file offsets; stops at the first location
 * outside the file so a corrupt table can't point the decoder off-buffer. */
static void build_samples(reader *r, const trak_state *t)
{
    MP4Track *out = r->out;
    if (t->stsz.len < 12 || t->stsc.len < 8 || t->stco.len < 8) return;
    uint32_t fixed = rd32(t->stsz.p + 4), count = rd32(t->stsz.p + 8);
    uint32_t nsc = rd32(t->stsc.p + 4), nco = rd32(t->stco.p + 4);
    uint32_t cosz = t->co64 ? 8 : 4;
    if (!fixed && (t->stsz.len - 12) / 4 < count) return;
    if (count > r->size) return;
    if ((t->stsc.len - 8) / 12 < nsc || (t->stco.len - 8) / cosz < nco) return;
    out->samples = (MP4Sample *)calloc(count ? count : 1, sizeof(MP4Sample));
    if (!out->samples) return;

    uint32_t s = 0;
    for (uint32_t e = 0; e < nsc && s < count; e++) {
        const uint8_t *ent = t->stsc.p + 8 + (size_t)e * 12;
        uint32_t first = rd32(ent), per = rd32(ent + 4);
        uint32_t last = e + 1 < nsc ? rd32(ent + 12) : nco + 1;
        if (first < 1 || last < first) return;
        for (uint32_t c = first; c < last && c <= nco && s < count; c++) {
            const uint8_t *co = t->stco.p + 8 + (size_t)(c - 1) * cosz;
            uint64_t off = t->co64 ? rd64(co) : rd32(co);
            for (uint32_t i = 0; i < per && s < count; i++, s++) {
                uint32_t sz = fixed ? fixed : rd32(t->stsz.p + 12 + (size_t)s * 4);
                if (off > r->size || sz > r->size - off) return;
                out->samples[out->num_samples].offset = off;
                out->samples[out->num_samples].size = sz;
                out->num_samples++;
                off += sz;
            }
        }
    }
}

static void finish_trak(reader *r)
{
    trak_state *t = &r->cur;
    MP4Track *out = r->out;
    if (r->have_track || !t->asc.len) return;
    r->have_track = true;
    out->timescale = t->timescale;
    out->asc_buf = (uint8_t *)malloc(t->asc.len);
    if (!out->asc_buf) return;
    memcpy(out->asc_buf, t->asc.p, t->asc.len);
    out->asc_len = (uint32_t)t->asc.len;
    build_samples(r, t);

    /* Without iTunSMPB the edit list's media time is the priming, and
     * whatever media runs past the edit is padding. */
    if (!r->have_smpb && t->has_elst && t->elst_time < 0xFFFFFFFFULL) {
        out->delay = (uint32_t)t->elst_time;
        if (t->timescale && r->movie_timescale && t->elst_dur) {
            uint64_t used = t->elst_time + t->elst_dur * t->timescale / r->movie_timescale;
            if (t->duration > used) out->padding = (uint32_t)(t->duration - used);
        }
    }
}

static void walk(reader *r, const span *s, int depth)
{
    uint64_t pos = 0;
    char type[4];
    span c;
    while (next_box(s->p, &pos, s->len, type, &c)) {
        if (!memcmp(type, "trak", 4)) {
            if (depth > 4) continue;
            r->num_traks++;
            memset(&r->cur, 0, sizeof(r->cur));
            walk(r, &c, depth + 1);
            finish_trak(r);
        } else if (!memcmp(type, "moov", 4) || !memcmp(type, "mdia", 4) || !memcmp(type, "minf", 4) ||
                   !memcmp(type, "stbl", 4) || !memcmp(type, "edts", 4) || !memcmp(type, "udta", 4) ||
                   !memcmp(type, "ilst", 4)) {
            if (depth > 8) continue;
            if (!memcmp(type, "ilst", 4)) parse_ilst(&c, r);
            else walk(r, &c, depth + 1);
        } else if (!memcmp(type, "meta", 4)) {
            if (depth <= 8 && c.len > 4) {
                span in = { c.p + 4, c.len - 4 };
                walk(r, &in, depth + 1);
            }
        } else if (!memcmp(type, "mvhd", 4)) {
            if (c.len >= 20) r->movie_timescale = rd32(c.p + (c.p[0] == 1 ? 20 : 12));
        } else if (!memcmp(type, "mdhd", 4)) parse_mdhd(&c, &r->cur);
        else if (!memcmp(type, "elst", 4)) parse_elst(&c, &r->cur);
        else if (!memcmp(type, "stsd", 4)) parse_stsd(&c, &r->cur);
        else if (!memcmp(type, "stsz", 4)) r->cur.stsz = c;
        else if (!memcmp(type, "stsc", 4)) r->cur.stsc = c;
        else if (!memcmp(type, "stco", 4)) { r->cur.stco = c; r->cur.co64 = false; }
        else if (!memcmp(type, "co64", 4)) { r->cur.stco = c; r->cur.co64 = true; }
        else if (!memcmp(type, "ftyp", 4) && depth == 0 && c.len >= 4) {
            memcpy(r->out->major_brand, c.p, 4);
            for (int i = 3; i >= 0 && r->out->major_brand[i] == ' '; i--) r->out->major_brand[i] = 0;
        }
    }
}

bool mp4_read_track_buf(const uint8_t *buf, long file_size, MP4Track *track)
{
    memset(track, 0, sizeof(*track));
    if (!buf || file_size < 32) return false;

    reader r;
    memset(&r, 0, sizeof(r));
    r.buf = buf;
    r.size = (uint64_t)file_size;
    r.out = track;
    span all = { buf, r.size };
    walk(&r, &all, 0);

    /* A valid MP4 without AAC audio is still an MP4: the caller reports that.
     * Without any track the input is not a container at all (e.g. ADTS), so
     * hand it back to be treated as a raw stream. */
    return r.num_traks > 0;
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
