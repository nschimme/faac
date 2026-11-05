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
 * Chapters for libfaam: one list, always written as the Nero chpl atom and as a QuickTime
 * chapter text track (tref/chap in the first audio trak), shared by the muxer and the
 * in-place updater so every reader sees the same chapters.
 */

#include "endian.h"
#include "libfaam_internal.h"

/* Layout of the text trak ffmpeg writes (gmhd, text sample description), which Apple's and
 * mp4v2's readers accept. */
static const uint8_t k_gmhd[68] = {
    0x00, 0x00, 0x00, 0x18, 0x67, 0x6d, 0x69, 0x6e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x80,
    0x00, 0x80, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2c, 0x74, 0x65,
    0x78, 0x74, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00
};
static const uint8_t k_text_entry[59] = {
    0x00, 0x00, 0x00, 0x3b, 0x74, 0x65, 0x78, 0x74, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0d, 0x66, 0x74, 0x61, 0x62, 0x00, 0x01, 0x00, 0x01, 0x00
};
/* Marks the text UTF-8; without it QuickTime reads Mac Roman. */
static const uint8_t k_encd[12] = { 0, 0, 0, 12, 'e', 'n', 'c', 'd', 0, 0, 1, 0 };

faam_status faam_chapters_check(const faam_chapter *chapters, uint32_t count)
{
    if (count > FAAM_MAX_CHAPTERS || (count && !chapters)) return FAAM_ERR_INVALID_ARGUMENT;
    if (count && chapters->struct_size < FAAM_CHAPTER_BASELINE) return FAAM_ERR_INVALID_ARGUMENT;
    uint64_t prev = 0;
    for (uint32_t c = 0; c < count; c++) {
        const faam_chapter *e = faam_chapter_at(chapters, c);
        if (e->struct_size != chapters->struct_size || !e->title || e->start_ms > UINT32_MAX)
            return FAAM_ERR_INVALID_ARGUMENT;
        /* A text sample has a duration, so starts cannot repeat or run backwards, and the
         * first sample begins the track: ffmpeg's own gapped files disagree between readers. */
        if (c ? e->start_ms <= prev : e->start_ms != 0) return FAAM_ERR_INVALID_ARGUMENT;
        prev = e->start_ms;
    }
    return FAAM_OK;
}

faam_status faam_chapters_fit(const faam_chapter *chapters, uint32_t count, uint32_t movie_ts, uint64_t movie_dur)
{
    if (!count) return FAAM_OK;
    uint64_t total_ms = movie_ts ? movie_dur / movie_ts * 1000 + movie_dur % movie_ts * 1000 / movie_ts : 0;
    return faam_chapter_at(chapters, count - 1)->start_ms < total_ms ? FAAM_OK : FAAM_ERR_INVALID_ARGUMENT;
}

/* Cut on a character boundary so both chapter forms store the same valid UTF-8. */
uint32_t faam_chapter_title_len(const char *title)
{
    size_t n = strlen(title);
    if (n > 255) {
        n = 255;
        while (n && ((uint8_t)title[n] & 0xC0) == 0x80) n--;
    }
    return (uint32_t)n;
}

uint8_t *faam_chapter_chpl_build(const faam_chapter *chapters, uint32_t count, uint32_t *len)
{
    uint8_t *buf = (uint8_t *)AllocMemory(17 + (size_t)count * 265);
    if (!buf) return NULL;
    uint32_t n = 17;
    write_u32_be(buf + 8, 1U << 24); /* ver/flags */
    write_u32_be(buf + 12, 0);
    buf[16] = (uint8_t)count;
    for (uint32_t c = 0; c < count; c++) {
        const faam_chapter *e = faam_chapter_at(chapters, c);
        write_u64_be(buf + n, e->start_ms * 10000ULL); /* 100ns units */
        n += 8;
        uint32_t tlen = faam_chapter_title_len(e->title);
        buf[n++] = (uint8_t)tlen;
        memcpy(buf + n, e->title, tlen);
        n += tlen;
    }
    write_u32_be(buf, n);
    memcpy(buf + 4, "chpl", 4);
    *len = n;
    return buf;
}

uint32_t faam_chapter_samples_len(const faam_chapter *chapters, uint32_t count)
{
    uint32_t n = 0;
    for (uint32_t c = 0; c < count; c++)
        n += 2 + faam_chapter_title_len(faam_chapter_at(chapters, c)->title) + sizeof(k_encd);
    return n;
}

void faam_chapter_samples_fill(const faam_chapter *chapters, uint32_t count, uint8_t *out)
{
    for (uint32_t c = 0; c < count; c++) {
        const char *title = faam_chapter_at(chapters, c)->title;
        uint32_t tlen = faam_chapter_title_len(title);
        write_u16_be(out, (uint16_t)tlen);
        memcpy(out + 2, title, tlen);
        memcpy(out + 2 + tlen, k_encd, sizeof(k_encd));
        out += 2 + tlen + sizeof(k_encd);
    }
}

void faam_chapter_tref_fill(uint32_t track_id, uint8_t out[20])
{
    write_u32_be(out, 20);
    memcpy(out + 4, "tref", 4);
    write_u32_be(out + 8, 12);
    memcpy(out + 12, "chap", 4);
    write_u32_be(out + 16, track_id);
}

typedef struct {
    uint8_t *p;
    uint32_t n;
} wbuf;

static void w_u16(wbuf *w, uint16_t v) { write_u16_be(w->p + w->n, v); w->n += 2; }
static void w_u32(wbuf *w, uint32_t v) { write_u32_be(w->p + w->n, v); w->n += 4; }
static void w_u64(wbuf *w, uint64_t v) { write_u64_be(w->p + w->n, v); w->n += 8; }
static void w_zeros(wbuf *w, uint32_t n) { memset(w->p + w->n, 0, n); w->n += n; }
static void w_data(wbuf *w, const void *d, uint32_t n) { memcpy(w->p + w->n, d, n); w->n += n; }
static void w_time(wbuf *w, uint64_t v, bool wide) { if (wide) w_u64(w, v); else w_u32(w, (uint32_t)v); }
static uint32_t w_open(wbuf *w, const char *type) { uint32_t at = w->n; w_u32(w, 0); w_data(w, type, 4); return at; }
static void w_close(wbuf *w, uint32_t at) { write_u32_be(w->p + at, w->n - at); }

uint8_t *faam_chapter_trak_build(const faam_chapter *chapters, uint32_t count, uint32_t track_id,
                                 uint32_t movie_ts, uint64_t movie_dur, uint64_t creation,
                                 uint64_t chunk_offset, bool frag, uint32_t *len)
{
    uint8_t *buf = (uint8_t *)AllocMemory(1024 + (size_t)count * 16);
    if (!buf) return NULL;
    wbuf w = { buf, 0 };

    /* The track runs at millisecond ticks; the last chapter lasts to the end of the movie. */
    uint64_t total_ms = movie_ts ? movie_dur / movie_ts * 1000 + movie_dur % movie_ts * 1000 / movie_ts : 0;
    uint64_t last_start = count ? faam_chapter_at(chapters, count - 1)->start_ms : 0;
    uint64_t last_dur = total_ms > last_start ? total_ms - last_start : 1;
    uint64_t media_dur = frag ? 0 : last_start + last_dur;
    if (frag) movie_dur = 0;
    bool wide = movie_dur > UINT32_MAX || media_dur > UINT32_MAX || creation > UINT32_MAX;
    bool co64 = !frag && chunk_offset > UINT32_MAX;
    uint32_t ver = wide ? 1U << 24 : 0;

    uint32_t trak = w_open(&w, "trak");
    uint32_t tkhd = w_open(&w, "tkhd");
    w_u32(&w, ver | 2); /* in the movie but not enabled, as ffmpeg writes it */
    w_time(&w, creation, wide); w_time(&w, creation, wide);
    w_u32(&w, track_id); w_zeros(&w, 4);
    w_time(&w, movie_dur, wide);
    w_zeros(&w, 8); w_zeros(&w, 8); /* reserved; layer, alternate group, volume, reserved */
    w_u32(&w, 0x10000); w_zeros(&w, 12);
    w_u32(&w, 0x10000); w_zeros(&w, 12);
    w_u32(&w, 0x40000000);
    w_zeros(&w, 8); /* width, height */
    w_close(&w, tkhd);

    if (!frag) {
        uint32_t edts = w_open(&w, "edts");
        uint32_t elst = w_open(&w, "elst");
        w_u32(&w, ver); w_u32(&w, 1);
        w_time(&w, movie_dur, wide); w_time(&w, 0, wide);
        w_u16(&w, 1); w_u16(&w, 0);
        w_close(&w, elst);
        w_close(&w, edts);
    }

    uint32_t mdia = w_open(&w, "mdia");
    uint32_t mdhd = w_open(&w, "mdhd");
    w_u32(&w, ver);
    w_time(&w, creation, wide); w_time(&w, creation, wide);
    w_u32(&w, 1000); w_time(&w, media_dur, wide);
    w_zeros(&w, 4); /* language 0 and pre_defined, as ffmpeg writes them */
    w_close(&w, mdhd);

    uint32_t hdlr = w_open(&w, "hdlr");
    w_zeros(&w, 8); w_data(&w, "text", 4); w_zeros(&w, 12);
    w_data(&w, "SubtitleHandler", 16);
    w_close(&w, hdlr);

    uint32_t minf = w_open(&w, "minf");
    uint32_t gmhd = w_open(&w, "gmhd");
    w_data(&w, k_gmhd, sizeof(k_gmhd));
    w_close(&w, gmhd);
    uint32_t dinf = w_open(&w, "dinf");
    uint32_t dref = w_open(&w, "dref");
    w_zeros(&w, 4); w_u32(&w, 1);
    w_u32(&w, 12); w_data(&w, "url ", 4); w_u32(&w, 1);
    w_close(&w, dref);
    w_close(&w, dinf);

    uint32_t stbl = w_open(&w, "stbl");
    uint32_t stsd = w_open(&w, "stsd");
    w_zeros(&w, 4); w_u32(&w, 1);
    w_data(&w, k_text_entry, sizeof(k_text_entry));
    w_close(&w, stsd);

    /* Fragmented tables stay empty: the samples live in the trun of the first fragment. */
    uint32_t rows = frag ? 0 : count;
    uint32_t stts = w_open(&w, "stts");
    w_zeros(&w, 4); w_u32(&w, rows);
    for (uint32_t c = 0; c < rows; c++) {
        uint64_t start = faam_chapter_at(chapters, c)->start_ms;
        uint64_t next = c + 1 < count ? faam_chapter_at(chapters, c + 1)->start_ms : start + last_dur;
        w_u32(&w, 1); w_u32(&w, next - start > UINT32_MAX ? UINT32_MAX : (uint32_t)(next - start));
    }
    w_close(&w, stts);

    uint32_t stsc = w_open(&w, "stsc");
    w_zeros(&w, 4); w_u32(&w, frag ? 0 : 1);
    if (!frag) { w_u32(&w, 1); w_u32(&w, count); w_u32(&w, 1); }
    w_close(&w, stsc);

    uint32_t stsz = w_open(&w, "stsz");
    w_zeros(&w, 8); w_u32(&w, rows);
    for (uint32_t c = 0; c < rows; c++)
        w_u32(&w, 2 + faam_chapter_title_len(faam_chapter_at(chapters, c)->title) + (uint32_t)sizeof(k_encd));
    w_close(&w, stsz);

    uint32_t stco = w_open(&w, co64 ? "co64" : "stco");
    w_zeros(&w, 4); w_u32(&w, frag ? 0 : 1);
    if (!frag) { if (co64) w_u64(&w, chunk_offset); else w_u32(&w, (uint32_t)chunk_offset); }
    w_close(&w, stco);

    w_close(&w, stbl);
    w_close(&w, minf);
    w_close(&w, mdia);
    w_close(&w, trak);
    *len = w.n;
    return buf;
}

#ifdef FAAM_MUXER_FRAGMENTED
uint8_t *faam_chapter_frag_build(const faam_chapter *chapters, uint32_t count, uint32_t track_id, uint32_t seq,
                                 uint32_t last_dur_ms, uint32_t *len, uint32_t *last_dur_off)
{
    uint32_t trun = 20 + count * 8, moof = 8 + 16 + 8 + 16 + 16 + trun;
    uint32_t slen = faam_chapter_samples_len(chapters, count);
    uint8_t *buf = (uint8_t *)AllocMemory((size_t)moof + 8 + slen);
    if (!buf) return NULL;
    wbuf w = { buf, 0 };
    w_u32(&w, moof); w_data(&w, "moof", 4);
    w_u32(&w, 16); w_data(&w, "mfhd", 4); w_u32(&w, 0); w_u32(&w, seq);
    w_u32(&w, 8 + 16 + 16 + trun); w_data(&w, "traf", 4);
    w_u32(&w, 16); w_data(&w, "tfhd", 4); w_u32(&w, 0x020000 /* default-base-is-moof */); w_u32(&w, track_id);
    w_u32(&w, 16); w_data(&w, "tfdt", 4); w_u32(&w, 0); w_u32(&w, 0);
    w_u32(&w, trun); w_data(&w, "trun", 4);
    w_u32(&w, 0x000301 /* data offset, duration, size */); w_u32(&w, count); w_u32(&w, moof + 8);
    for (uint32_t c = 0; c < count; c++) {
        const faam_chapter *e = faam_chapter_at(chapters, c);
        w_u32(&w, c + 1 < count ? (uint32_t)(faam_chapter_at(chapters, c + 1)->start_ms - e->start_ms) : last_dur_ms);
        w_u32(&w, 2 + faam_chapter_title_len(e->title) + (uint32_t)sizeof(k_encd));
    }
    w_u32(&w, 8 + slen); w_data(&w, "mdat", 4);
    faam_chapter_samples_fill(chapters, count, buf + w.n);
    *len = moof + 8 + slen;
    *last_dur_off = moof - 8;
    return buf;
}
#endif

/* ---- in-place update ---- */

typedef struct {
    bool fragmented;       /* moov has mvex: the samples are in fragments, not in stbl */
    faam_atom_ref owner;   /* trak that carries (or will carry) the tref/chap */
    faam_atom_ref tref;    /* inside owner; size 0 when absent */
    bool has_chap;
    uint32_t chap_id;
    faam_atom_ref ctrak;   /* the trak chap names; size 0 when it does not exist */
    uint32_t max_id;
    faam_atom_ref mvhd;
    uint32_t movie_ts;
    uint64_t movie_dur;
} chap_loc;

static bool read_at(const faam_io *io, uint64_t off, void *buf, uint32_t n)
{
    return io->seek(io->user_data, off) && faam_io_read_full(io, buf, n) == (int32_t)n;
}

static faam_status child_status(int found)
{
    return found == -2 ? FAAM_ERR_UNSUPPORTED : found < 0 ? FAAM_ERR_IO_READ : FAAM_OK;
}

/* Position of the first child named `name`; size stays 0 when there is none. */
static faam_status find_in(const faam_io *io, const faam_atom_ref *parent, const char name[4], faam_atom_ref *out)
{
    out->offset = out->size = 0;
    int f = faam_atom_find_child(io, parent->offset + 8, parent->offset + parent->size, name, out);
    if (f <= 0) out->size = 0;
    return child_status(f);
}

static faam_status trak_id(const faam_io *io, const faam_atom_ref *trak, uint32_t *id)
{
    faam_atom_ref tkhd;
    faam_status st = find_in(io, trak, "tkhd", &tkhd);
    uint8_t b[4];
    *id = 0;
    if (st != FAAM_OK || tkhd.size < 24) return st;
    uint8_t ver;
    if (!read_at(io, tkhd.offset + 8, &ver, 1) || !read_at(io, tkhd.offset + (ver == 1 ? 28 : 20), b, 4))
        return FAAM_ERR_IO_READ;
    *id = read_u32_be(b);
    return FAAM_OK;
}

static faam_status trak_handler(const faam_io *io, const faam_atom_ref *trak, uint8_t type[4])
{
    faam_atom_ref mdia, hdlr = { 0, 0 };
    memset(type, 0, 4);
    faam_status st = find_in(io, trak, "mdia", &mdia);
    if (st == FAAM_OK && mdia.size) st = find_in(io, &mdia, "hdlr", &hdlr);
    if (st != FAAM_OK || hdlr.size < 20) return st;
    return read_at(io, hdlr.offset + 16, type, 4) ? FAAM_OK : FAAM_ERR_IO_READ;
}

static faam_status locate(const faam_io *io, const faam_atom_ref *moov, chap_loc *loc)
{
    memset(loc, 0, sizeof(*loc));
    faam_status st = find_in(io, moov, "mvhd", &loc->mvhd);
    if (st != FAAM_OK) return st;
    if (loc->mvhd.size >= 32) {
        uint8_t m[20];
        uint8_t ver;
        if (!read_at(io, loc->mvhd.offset + 8, &ver, 1) ||
            !read_at(io, loc->mvhd.offset + 8 + 4 + (ver == 1 ? 16 : 8), m, ver == 1 ? 12 : 8))
            return FAAM_ERR_IO_READ;
        loc->movie_ts = read_u32_be(m);
        loc->movie_dur = ver == 1 ? read_u64_be(m + 4) : read_u32_be(m + 4);
    }

    faam_atom_ref mvex;
    if ((st = find_in(io, moov, "mvex", &mvex)) != FAAM_OK) return st;
    loc->fragmented = mvex.size > 0;

    faam_atom_ref first = { 0, 0 }, audio = { 0, 0 };
    for (int pass = 0; pass < 2; pass++) {
        uint64_t pos = moov->offset + 8, end = moov->offset + moov->size;
        faam_atom_ref trak;
        char type[4];
        int r;
        while ((r = faam_atom_child_at(io, pos, end, type, &trak)) > 0) {
            pos += trak.size;
            if (memcmp(type, "trak", 4)) continue;
            uint32_t id;
            if ((st = trak_id(io, &trak, &id)) != FAAM_OK) return st;
            if (pass == 1) {
                if (loc->has_chap && id == loc->chap_id && !loc->ctrak.size) loc->ctrak = trak;
                continue;
            }
            if (id > loc->max_id) loc->max_id = id;
            if (!first.size) first = trak;
            uint8_t kind[4];
            if ((st = trak_handler(io, &trak, kind)) != FAAM_OK) return st;
            if (!memcmp(kind, "soun", 4) && !audio.size) audio = trak;
            if (loc->has_chap) continue;
            faam_atom_ref tref, chap;
            if ((st = find_in(io, &trak, "tref", &tref)) != FAAM_OK) return st;
            if (!tref.size) continue;
            if ((st = find_in(io, &tref, "chap", &chap)) != FAAM_OK) return st;
            uint8_t idb[4];
            /* A second id names a second chapter track, which an update would leave stale. */
            if (chap.size > 12) return FAAM_ERR_UNSUPPORTED;
            if (chap.size >= 12) {
                if (!read_at(io, chap.offset + 8, idb, 4)) return FAAM_ERR_IO_READ;
                loc->has_chap = true;
                loc->chap_id = read_u32_be(idb);
                loc->owner = trak;
                loc->tref = tref;
            }
        }
        if (r < 0) return child_status(r);
    }
    if (!loc->has_chap) {
        loc->owner = audio.size ? audio : first;
        if (loc->owner.size && (st = find_in(io, &loc->owner, "tref", &loc->tref)) != FAAM_OK) return st;
    }
    return FAAM_OK;
}

static faam_status scan_and_locate(const faam_io *io, faam_atom_ref *moov, faam_atom_ref *mdat,
                                   uint64_t *file_size, chap_loc *loc)
{
    faam_status st = faam_atom_scan_top(io, moov, mdat, file_size);
    if (st != FAAM_OK) return st;
    if (moov->size < 8) return FAAM_ERR_BAD_CONTAINER;
    return locate(io, moov, loc);
}

/* Turns an atom into a free box in place; its bytes stay but nothing reads them. */
static faam_status erase_atom(const faam_io *io, const faam_atom_ref *a)
{
    uint8_t hdr[8];
    write_u32_be(hdr, (uint32_t)a->size);
    memcpy(hdr + 4, "free", 4);
    if (!io->seek(io->user_data, a->offset) || io->write(io->user_data, hdr, 8) != 8) return FAAM_ERR_IO_WRITE;
    return FAAM_OK;
}

/* Drops the chap reference from the owner's tref, and the tref itself when nothing else is in it. */
static faam_status strip_chap(const faam_io *io, const faam_atom_ref *moov, const faam_atom_ref *mdat,
                              uint64_t file_size, const chap_loc *loc)
{
    if (!loc->has_chap || !loc->tref.size) return FAAM_OK;
    if (loc->tref.size > 4096) return FAAM_ERR_UNSUPPORTED;
    uint32_t tsize = (uint32_t)loc->tref.size;
    uint8_t *old = (uint8_t *)AllocMemory(tsize * 2);
    if (!old) return FAAM_ERR_NO_MEMORY;
    uint8_t *out = old + tsize;
    faam_status st = FAAM_OK;
    uint32_t kept = 8;
    if (!read_at(io, loc->tref.offset, old, tsize)) st = FAAM_ERR_IO_READ;
    for (uint32_t p = 8; st == FAAM_OK && p + 8 <= tsize;) {
        uint32_t sz = read_u32_be(old + p);
        if (sz < 8 || sz > tsize - p) { st = FAAM_ERR_BAD_CONTAINER; break; }
        if (memcmp(old + p + 4, "chap", 4)) {
            memcpy(out + kept, old + p, sz);
            kept += sz;
        }
        p += sz;
    }
    if (st == FAAM_OK) {
        if (kept == 8) {
            st = erase_atom(io, &loc->tref);
        } else {
            write_u32_be(out, kept);
            memcpy(out + 4, "tref", 4);
            uint64_t ancestors[2] = { loc->owner.offset, moov->offset };
            st = faam_atom_resize(io, loc->tref.offset, loc->tref.size, out, kept, ancestors, 2, moov, mdat, file_size);
        }
    }
    FreeMemory(old);
    return st;
}

#ifdef FAAM_MUXER_FRAGMENTED
/* The chapter samples of a fragmented file are unreferenced once their traf is a free box; a
 * moof that carried nothing else goes with its mdat. max_seq is the highest number still in use. */
static faam_status drop_text_fragments(const faam_io *io, uint32_t track_id, uint64_t file_size, bool free_index,
                                       uint32_t *max_seq)
{
    uint64_t pos = 0;
    char type[4];
    faam_atom_ref box;
    int r;
    *max_seq = 0;
    while ((r = faam_atom_child_at(io, pos, file_size, type, &box)) > 0) {
        pos += box.size;
        /* A fragment appended behind mfra would leave mfro off the end of the file, where readers look for it. */
        if (free_index && !memcmp(type, "mfra", 4)) {
            faam_status st = erase_atom(io, &box);
            if (st != FAAM_OK) return st;
            continue;
        }
        if (memcmp(type, "moof", 4)) continue;
        uint8_t b[4];
        if (!read_at(io, box.offset + 20, b, 4)) return FAAM_ERR_IO_READ;
        uint32_t seq = read_u32_be(b);
        faam_atom_ref traf, text = { 0, 0 };
        uint32_t trafs = 0;
        uint64_t at = box.offset + 8;
        char ctype[4];
        int c;
        while ((c = faam_atom_child_at(io, at, box.offset + box.size, ctype, &traf)) > 0) {
            at += traf.size;
            if (memcmp(ctype, "traf", 4)) continue;
            trafs++;
            if (!read_at(io, traf.offset + 20, b, 4)) return FAAM_ERR_IO_READ;
            if (read_u32_be(b) == track_id) text = traf;
        }
        if (c < 0) return child_status(c);
        /* AVFoundation drops chapters when the numbers have a gap, so a freed moof gives its number back. */
        if (!text.size || trafs > 1) {
            if (seq > *max_seq) *max_seq = seq;
            if (!text.size) continue;
        }
        faam_status st = erase_atom(io, trafs == 1 ? &box : &text);
        if (st == FAAM_OK && trafs == 1) {
            faam_atom_ref mdat;
            if (faam_atom_child_at(io, pos, file_size, type, &mdat) > 0 && !memcmp(type, "mdat", 4)) {
                st = erase_atom(io, &mdat);
                pos += mdat.size;
            }
        }
        if (st != FAAM_OK) return st;
    }
    return child_status(r);
}

/* Length of the audio in ms, from mehd; 0 while the file is unfinalized. */
static faam_status fragmented_total_ms(const faam_io *io, const faam_atom_ref *moov, const chap_loc *loc, uint64_t *ms)
{
    faam_atom_ref mvex, mehd;
    *ms = 0;
    faam_status st = find_in(io, moov, "mvex", &mvex);
    if (st == FAAM_OK && mvex.size) st = find_in(io, &mvex, "mehd", &mehd);
    if (st != FAAM_OK || !mvex.size || mehd.size < 16 || !loc->movie_ts) return st;
    uint8_t ver, d[8];
    if (!read_at(io, mehd.offset + 8, &ver, 1) || !read_at(io, mehd.offset + 12, d, ver == 1 ? 8 : 4)) return FAAM_ERR_IO_READ;
    uint64_t ticks = ver == 1 ? read_u64_be(d) : read_u32_be(d);
    *ms = ticks / loc->movie_ts * 1000 + ticks % loc->movie_ts * 1000 / loc->movie_ts;
    return FAAM_OK;
}

/* Fragments name their track defaults in mvex, so the chapter track needs its own trex. */
static faam_status trex_for(const faam_io *io, uint32_t track_id, bool add)
{
    faam_atom_ref moov, mdat, mvex, trex;
    uint64_t file_size, at;
    chap_loc loc;
    faam_status st = scan_and_locate(io, &moov, &mdat, &file_size, &loc);
    if (st == FAAM_OK) st = find_in(io, &moov, "mvex", &mvex);
    if (st != FAAM_OK || !mvex.size) return st;
    char type[4];
    int r;
    for (at = mvex.offset + 8; (r = faam_atom_child_at(io, at, mvex.offset + mvex.size, type, &trex)) > 0; at += trex.size) {
        uint8_t b[4];
        if (memcmp(type, "trex", 4)) continue;
        if (!read_at(io, trex.offset + 12, b, 4)) return FAAM_ERR_IO_READ;
        if (read_u32_be(b) != track_id) continue;
        return add ? FAAM_OK : erase_atom(io, &trex);
    }
    if (r < 0) return child_status(r);
    if (!add) return FAAM_OK;
    uint8_t buf[32] = { 0, 0, 0, 32, 't', 'r', 'e', 'x', 0, 0, 0, 0 };
    write_u32_be(buf + 12, track_id);
    write_u32_be(buf + 16, 1);
    uint64_t ancestors[2] = { mvex.offset, moov.offset };
    return faam_atom_resize(io, mvex.offset + mvex.size, 0, buf, sizeof(buf), ancestors, 2, &moov, &mdat, file_size);
}
#endif

static faam_status reject_media_trak(const faam_io *io, const chap_loc *loc);

static faam_status remove_chapters(const faam_io *io)
{
    faam_atom_ref moov, mdat;
    uint64_t file_size;
    chap_loc loc;
    faam_status st = scan_and_locate(io, &moov, &mdat, &file_size, &loc);
    if (st != FAAM_OK || (st = reject_media_trak(io, &loc)) != FAAM_OK) return st;

    faam_atom_ref udta, chpl = { 0, 0 };
    if ((st = find_in(io, &moov, "udta", &udta)) != FAAM_OK) return st;
    if (udta.size && (st = find_in(io, &udta, "chpl", &chpl)) != FAAM_OK) return st;
    if (chpl.size && (st = erase_atom(io, &chpl)) != FAAM_OK) return st;
#ifdef FAAM_MUXER_FRAGMENTED
    if (loc.fragmented && loc.has_chap) {
        uint32_t seq;
        if ((st = drop_text_fragments(io, loc.chap_id, file_size, false, &seq)) != FAAM_OK) return st;
        if ((st = trex_for(io, loc.chap_id, false)) != FAAM_OK) return st;
    }
#else
    if (loc.fragmented) return FAAM_ERR_UNSUPPORTED; /* its text fragment would be left behind */
#endif
    if (loc.ctrak.size && (st = erase_atom(io, &loc.ctrak)) != FAAM_OK) return st;
    return strip_chap(io, &moov, &mdat, file_size, &loc);
}

static faam_status write_chpl(const faam_io *io, const faam_chapter *chapters, uint32_t count)
{
    faam_atom_ref moov, mdat;
    uint64_t file_size;
    faam_status st = faam_atom_scan_top(io, &moov, &mdat, &file_size);
    if (st != FAAM_OK) return st;
    if (moov.size < 8) return FAAM_ERR_BAD_CONTAINER;

    faam_atom_ref udta = { 0, 0 }, chpl = { 0, 0 };
    int found = faam_atom_find_child(io, moov.offset + 8, moov.offset + moov.size, "udta", &udta);
    if (found > 0)
        found = faam_atom_find_child(io, udta.offset + 8, udta.offset + udta.size, "chpl", &chpl);
    if (found < 0) return child_status(found);

    uint32_t len;
    uint8_t *buf = faam_chapter_chpl_build(chapters, count, &len);
    if (!buf) return FAAM_ERR_NO_MEMORY;
    if (chpl.size >= 8) {
        uint64_t ancestors[2] = { udta.offset, moov.offset };
        st = faam_atom_resize(io, chpl.offset, chpl.size, buf, len, ancestors, 2, &moov, &mdat, file_size);
    } else {
        st = faam_atom_append_udta_child(io, &udta, &moov, &mdat, file_size, buf, len);
    }
    FreeMemory(buf);
    return st;
}

/* Makes sure the owner trak names track_id as its chapter track. */
static faam_status ensure_tref(const faam_io *io, uint32_t track_id)
{
    faam_atom_ref moov, mdat;
    uint64_t file_size;
    chap_loc loc;
    faam_status st = scan_and_locate(io, &moov, &mdat, &file_size, &loc);
    if (st != FAAM_OK) return st;
    if (!loc.owner.size) return FAAM_ERR_BAD_CONTAINER;
    if (loc.has_chap) return FAAM_OK;

    if (loc.tref.size) {
        if (!faam_atom_children_fill(io, loc.tref.offset + 8, loc.tref.offset + loc.tref.size))
            return FAAM_ERR_BAD_CONTAINER;
        uint8_t chap[12];
        write_u32_be(chap, 12);
        memcpy(chap + 4, "chap", 4);
        write_u32_be(chap + 8, track_id);
        uint64_t ancestors[3] = { loc.tref.offset, loc.owner.offset, moov.offset };
        return faam_atom_resize(io, loc.tref.offset + loc.tref.size, 0, chap, 12, ancestors, 3, &moov, &mdat, file_size);
    }

    uint8_t tref[20];
    faam_chapter_tref_fill(track_id, tref);
    /* Before mdia, where ffmpeg puts it; the end of the trak when it has none. */
    faam_atom_ref mdia;
    if ((st = find_in(io, &loc.owner, "mdia", &mdia)) != FAAM_OK) return st;
    uint64_t at = mdia.size ? mdia.offset : loc.owner.offset + loc.owner.size;
    if (!mdia.size && !faam_atom_children_fill(io, loc.owner.offset + 8, at)) return FAAM_ERR_BAD_CONTAINER;
    uint64_t ancestors[2] = { loc.owner.offset, moov.offset };
    return faam_atom_resize(io, at, 0, tref, 20, ancestors, 2, &moov, &mdat, file_size);
}

/* Replaces the chapter trak in place, or appends one to moov. */
static faam_status install_trak(const faam_io *io, const faam_atom_ref *moov, const faam_atom_ref *mdat,
                                uint64_t file_size, const chap_loc *loc, const uint8_t *trak, uint32_t len)
{
    uint64_t ancestors[1] = { moov->offset };
    if (loc->ctrak.size)
        return faam_atom_resize(io, loc->ctrak.offset, loc->ctrak.size, trak, len, ancestors, 1, moov, mdat, file_size);
    if (!faam_atom_children_fill(io, moov->offset + 8, moov->offset + moov->size)) return FAAM_ERR_BAD_CONTAINER;
    uint64_t at = moov->offset + moov->size;
    /* AVFoundation ignores a chapter trak that follows mvex. */
    faam_atom_ref mvex;
    if (loc->fragmented) {
        faam_status st = find_in(io, moov, "mvex", &mvex);
        if (st != FAAM_OK) return st;
        if (mvex.size) at = mvex.offset;
    }
    return faam_atom_resize(io, at, 0, trak, len, ancestors, 1, moov, mdat, file_size);
}

/* A chap reference to an audio or video trak is not a chapter track to overwrite. */
static faam_status reject_media_trak(const faam_io *io, const chap_loc *loc)
{
    uint8_t kind[4];
    if (!loc->ctrak.size) return FAAM_OK;
    faam_status st = trak_handler(io, &loc->ctrak, kind);
    if (st == FAAM_OK && (!memcmp(kind, "soun", 4) || !memcmp(kind, "vide", 4))) return FAAM_ERR_UNSUPPORTED;
    return st;
}

static faam_status write_trak(const faam_io *io, const faam_chapter *chapters, uint32_t count, uint32_t track_id)
{
    faam_atom_ref moov, mdat;
    uint64_t file_size;
    chap_loc loc;
    faam_status st = scan_and_locate(io, &moov, &mdat, &file_size, &loc);
    if (st != FAAM_OK) return st;
    if ((st = reject_media_trak(io, &loc)) != FAAM_OK) return st;

    /* New samples go behind everything as their own mdat: no existing offset moves. */
    uint32_t slen = faam_chapter_samples_len(chapters, count);
    uint8_t *samples = (uint8_t *)AllocMemory(8 + (size_t)slen);
    if (!samples) return FAAM_ERR_NO_MEMORY;
    write_u32_be(samples, 8 + slen);
    memcpy(samples + 4, "mdat", 4);
    faam_chapter_samples_fill(chapters, count, samples + 8);
    if (!io->seek(io->user_data, file_size) || io->write(io->user_data, samples, 8 + slen) != (int32_t)(8 + slen))
        st = FAAM_ERR_IO_WRITE;
    FreeMemory(samples);
    if (st != FAAM_OK) return st;
    mdat.offset = file_size;
    mdat.size = 8 + (uint64_t)slen;
    file_size += mdat.size;

    /* Growing moov pushes the samples back by what it gains, and the trak must name their
     * final position; that position only changes its size across the 4 GiB stco/co64 line. */
    uint64_t old_size = loc.ctrak.size, shift = 0;
    uint8_t *trak;
    uint32_t len;
    for (;;) {
        trak = faam_chapter_trak_build(chapters, count, track_id, loc.movie_ts, loc.movie_dur, 0,
                                       file_size - slen + shift, false, &len);
        if (!trak) return FAAM_ERR_NO_MEMORY;
        uint64_t need = len > old_size ? len - old_size : 0;
        if (need == shift) break;
        shift = need;
        FreeMemory(trak);
    }

    st = install_trak(io, &moov, &mdat, file_size, &loc, trak, len);
    FreeMemory(trak);
    return st;
}

/* mvhd's next_track_ID must stay above every track in the movie. */
static faam_status reserve_track_id(const faam_io *io, uint32_t track_id)
{
    faam_atom_ref moov, mdat;
    uint64_t file_size;
    chap_loc loc;
    faam_status st = scan_and_locate(io, &moov, &mdat, &file_size, &loc);
    if (st != FAAM_OK || loc.mvhd.size < 32) return st;
    uint8_t b[4];
    uint64_t at = loc.mvhd.offset + loc.mvhd.size - 4;
    if (!read_at(io, at, b, 4)) return FAAM_ERR_IO_READ;
    if (read_u32_be(b) > track_id) return FAAM_OK;
    write_u32_be(b, track_id + 1);
    return io->seek(io->user_data, at) && io->write(io->user_data, b, 4) == 4 ? FAAM_OK : FAAM_ERR_IO_WRITE;
}

/* Everything that can reject the file is checked before the first write, so a container the
 * edit cannot extend is left as it was. */
static faam_status preflight(const faam_io *io, const faam_atom_ref *moov, const faam_atom_ref *mdat,
                             uint64_t file_size, const chap_loc *loc)
{
    faam_status st = faam_atom_offsets_check(io, moov, mdat, file_size);
    if (st != FAAM_OK) return st;
    faam_atom_ref udta, chpl = { 0, 0 };
    st = find_in(io, moov, "udta", &udta);
    if (st == FAAM_OK && udta.size) st = find_in(io, &udta, "chpl", &chpl);
    if (st != FAAM_OK) return st;
    bool ok = true;
    if (!chpl.size) {
        const faam_atom_ref *parent = udta.size ? &udta : moov;
        ok = faam_atom_children_fill(io, parent->offset + 8, parent->offset + parent->size);
    }
    if (ok && !loc->ctrak.size) ok = faam_atom_children_fill(io, moov->offset + 8, moov->offset + moov->size);
    if (ok && !loc->has_chap) {
        if (loc->tref.size) {
            ok = faam_atom_children_fill(io, loc->tref.offset + 8, loc->tref.offset + loc->tref.size);
        } else {
            faam_atom_ref mdia;
            if ((st = find_in(io, &loc->owner, "mdia", &mdia)) != FAAM_OK) return st;
            ok = mdia.size || faam_atom_children_fill(io, loc->owner.offset + 8, loc->owner.offset + loc->owner.size);
        }
    }
    return ok ? FAAM_OK : FAAM_ERR_BAD_CONTAINER;
}

#ifdef FAAM_MUXER_FRAGMENTED
/* A recording that never finalized ends in an mdat of size 0, which the top-level scan stops at: a
 * fragment appended there would overwrite its header. Any unparsed tail is refused the same way. */
static faam_status reject_unparsed_tail(const faam_io *io, uint64_t file_size)
{
    uint8_t b;
    return read_at(io, file_size, &b, 1) ? FAAM_ERR_UNSUPPORTED : FAAM_OK;
}

/* The text samples of a fragmented file are one more fragment at the end: the old ones are
 * freed in place, so no offset moves and the audio fragments stay as they were. The trak is
 * rebuilt in milliseconds either way, which keeps ffmpeg-made files consistent with ours. */
static faam_status update_fragmented(const faam_io *io, const faam_chapter *chapters, uint32_t count)
{
    faam_atom_ref moov, mdat, mvex;
    uint64_t file_size;
    chap_loc loc;
    faam_status st = scan_and_locate(io, &moov, &mdat, &file_size, &loc);
    if (st != FAAM_OK) return st;
    if (!loc.owner.size || !loc.movie_ts) return FAAM_ERR_BAD_CONTAINER;
    if (!loc.has_chap && loc.max_id >= UINT32_MAX - 1) return FAAM_ERR_UNSUPPORTED;
    uint32_t track_id = loc.has_chap ? loc.chap_id : loc.max_id + 1;
    if ((st = reject_media_trak(io, &loc)) != FAAM_OK) return st;
    if ((st = preflight(io, &moov, &mdat, file_size, &loc)) != FAAM_OK) return st;
    if ((st = reject_unparsed_tail(io, file_size)) != FAAM_OK) return st;
    if ((st = find_in(io, &moov, "mvex", &mvex)) != FAAM_OK) return st;
    if (!faam_atom_children_fill(io, mvex.offset + 8, mvex.offset + mvex.size)) return FAAM_ERR_BAD_CONTAINER;

    if ((st = write_chpl(io, chapters, count)) != FAAM_OK) return st;
    uint32_t len;
    uint8_t *trak = faam_chapter_trak_build(chapters, count, track_id, loc.movie_ts, 0, 0, 0, true, &len);
    if (!trak) return FAAM_ERR_NO_MEMORY;
    if ((st = scan_and_locate(io, &moov, &mdat, &file_size, &loc)) == FAAM_OK)
        st = install_trak(io, &moov, &mdat, file_size, &loc, trak, len);
    FreeMemory(trak);
    if (st != FAAM_OK || (st = ensure_tref(io, track_id)) != FAAM_OK) return st;
    if ((st = reserve_track_id(io, track_id)) != FAAM_OK || (st = trex_for(io, track_id, true)) != FAAM_OK) return st;

    uint32_t seq;
    uint64_t total_ms;
    if ((st = scan_and_locate(io, &moov, &mdat, &file_size, &loc)) != FAAM_OK) return st;
    if ((st = drop_text_fragments(io, track_id, file_size, true, &seq)) != FAAM_OK) return st;
    if ((st = fragmented_total_ms(io, &moov, &loc, &total_ms)) != FAAM_OK) return st;
    uint64_t last_start = faam_chapter_at(chapters, count - 1)->start_ms;
    uint32_t last_off;
    uint8_t *frag = faam_chapter_frag_build(chapters, count, track_id, seq + 1,
                                            (uint32_t)(total_ms > last_start ? total_ms - last_start : 1), &len, &last_off);
    if (!frag) return FAAM_ERR_NO_MEMORY;
    if (!io->seek(io->user_data, file_size) || io->write(io->user_data, frag, len) != (int32_t)len) st = FAAM_ERR_IO_WRITE;
    FreeMemory(frag);
    return st;
}
#endif

faam_status faam_update_chapters_stream(const faam_io *caller_io, const faam_chapter *chapters, uint32_t count, uint32_t flags)
{
    faam_io loaded;
    const faam_io *io = &loaded;
    if (flags || !caller_io || !faam_io_load(&loaded, caller_io)) return FAAM_ERR_INVALID_ARGUMENT;
    faam_status st = faam_chapters_check(chapters, count);
    if (st != FAAM_OK) return st;
    if (!io->read || !io->write || !io->seek || !io->tell) return FAAM_ERR_UNSUPPORTED;
    if (!count) return remove_chapters(io);

    faam_atom_ref moov, mdat;
    uint64_t file_size;
    chap_loc loc;
    if ((st = scan_and_locate(io, &moov, &mdat, &file_size, &loc)) != FAAM_OK) return st;
#ifdef FAAM_MUXER_FRAGMENTED
    if (loc.fragmented) return update_fragmented(io, chapters, count);
#else
    if (loc.fragmented) return FAAM_ERR_UNSUPPORTED;
#endif
    if (!loc.owner.size) return FAAM_ERR_BAD_CONTAINER;
    if (!loc.has_chap && loc.max_id >= UINT32_MAX - 1) return FAAM_ERR_UNSUPPORTED;
    uint32_t track_id = loc.has_chap ? loc.chap_id : loc.max_id + 1;
    if (!loc.movie_ts) return FAAM_ERR_BAD_CONTAINER;
    if ((st = faam_chapters_fit(chapters, count, loc.movie_ts, loc.movie_dur)) != FAAM_OK) return st;
    if ((st = preflight(io, &moov, &mdat, file_size, &loc)) != FAAM_OK) return st;

    if ((st = ensure_tref(io, track_id)) != FAAM_OK) return st;
    if ((st = reserve_track_id(io, track_id)) != FAAM_OK) return st;
    if ((st = write_trak(io, chapters, count, track_id)) != FAAM_OK) return st;
    return write_chpl(io, chapters, count);
}
