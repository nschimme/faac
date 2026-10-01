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
 * Audio-only MP4/M4A writer: one AAC track, progressive layout (ftyp, mdat,
 * then moov once the sample tables are complete). Metadata is borrowed until
 * mp4_finish().
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define access _access
#include "charset.h"
#else
#include <unistd.h>
#endif
#include "cli_common.h"
#include "endian.h"

#include "mp4write.h"

enum {
    MP4_EPOCH_OFFSET = 2082844800,
    MP4_FP1616_ONE = 0x00010000,
    MP4_FP0230_ONE = 0x40000000,
    MP4_FP0808_ONE = 0x0100,
    MP4_DESC_HDR = 5,
    MP4_OBJECT_TYPE_AUDIO_ISO_14496_3 = 0x40,
    MP4_STREAM_TYPE_AUDIO = 0x15,
    MP4_DECODER_BUFFER_BYTES_PER_CH = 6144 / 8,
    ISO639_UND_PACKED = 0x55C4,
    /* ftyp (28) + 'wide' (8) + mdat header (8) */
    MDAT_DATA_POS = 44,
};

typedef struct {
    const char *name;
    char *value;
} custom_tag;

static struct {
    FILE *f;
    char iobuf[64 * 1024];
    bool error;

    /* Format, set before or after open */
    uint32_t rate, channels;
    uint16_t sample_bits;
    char language[4];
    const uint8_t *asc;
    uint32_t asc_len;
    uint32_t creation_time;
    bool constant_rate;
    struct { uint32_t delay, padding; uint64_t total; } gapless;

    /* Metadata */
    const char *encoder, *title, *title_sort, *artist, *artist_sort, *album, *album_sort,
        *album_artist, *album_artist_sort, *composer, *composer_sort, *year, *comment;
    uint16_t genre, track, ntracks, disc, ndiscs;
    bool compilation;
    const uint8_t *cover;
    uint32_t cover_bytes;
    custom_tag *custom;
    uint32_t ncustom, custom_cap;

    /* Sample tables and statistics */
    uint32_t *sizes;
    uint32_t nsamples, sizes_cap;
    struct { uint32_t count, delta; } *stts;
    uint32_t nstts, stts_cap;
    uint64_t mdat_size;
    uint64_t total_bytes;
    uint64_t samples;
    uint32_t max_frame_size;
    uint32_t last_frame_samples;
    uint32_t window_max;
    uint64_t window_bytes, window_ticks;
    bool finalized;

    /* moov under construction */
    bool in_moov;
    uint8_t *moov;
    size_t moov_len, moov_cap;
} g = { .rate = 44100, .sample_bits = 16 };

static uint32_t track_rate(void) { return g.rate ? g.rate : 44100; }

/* The moov is assembled in memory and written once: patching atom sizes in
 * the file would turn every atom into a flush and two seeks. */
static void put(const void *data, size_t size) {
    if (g.error || !size) return;
    if (!g.in_moov) {
        if (fwrite(data, 1, size, g.f) != size) g.error = true;
        return;
    }
    if (size > g.moov_cap - g.moov_len) {
        size_t cap = g.moov_cap ? g.moov_cap : 4096;
        while (size > cap - g.moov_len) cap *= 2;
        uint8_t *tmp = (uint8_t *)realloc(g.moov, cap);
        if (!tmp) { g.error = true; return; }
        g.moov = tmp;
        g.moov_cap = cap;
    }
    memcpy(g.moov + g.moov_len, data, size);
    g.moov_len += size;
}

static void put_u8(uint8_t v) { put(&v, 1); }

static void put_u16(uint16_t v) {
    v = htobe16(v);
    put(&v, 2);
}

static void put_u32(uint32_t v) {
    v = htobe32(v);
    put(&v, 4);
}

static void put_u64(uint64_t v) {
    v = htobe64(v);
    put(&v, 8);
}

static void put_time(uint64_t v, bool use64) {
    if (use64) put_u64(v); else put_u32((uint32_t)v);
}

static bool seek_to(uint64_t pos) {
#ifdef _WIN32
    return _fseeki64(g.f, (int64_t)pos, SEEK_SET) == 0;
#else
    return fseeko(g.f, (off_t)pos, SEEK_SET) == 0;
#endif
}

static uint64_t tell_pos(void) {
#ifdef _WIN32
    return (uint64_t)_ftelli64(g.f);
#else
    return (uint64_t)ftello(g.f);
#endif
}

static size_t start_atom(const char *name) {
    size_t pos = g.moov_len;
    put_u32(0);
    put(name, 4);
    return pos;
}

static void end_atom(size_t pos) {
    if (g.error) return;
    size_t size = g.moov_len - pos;
    if (size > UINT32_MAX) { g.error = true; return; }
    uint32_t be = htobe32((uint32_t)size);
    memcpy(g.moov + pos, &be, 4);
}

static uint16_t pack_language(const char *lang) {
    if (!lang[0]) return ISO639_UND_PACKED;
    uint16_t packed = 0;
    for (unsigned i = 0; i < 3; i++) {
        unsigned c = (unsigned char)lang[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c < 'a' || c > 'z') return ISO639_UND_PACKED;
        packed = (uint16_t)((packed << 5) | (c - 0x60));
    }
    return packed;
}

static void put_descriptor(uint8_t tag, uint32_t size) {
    uint8_t buf[5];
    buf[0] = tag;
    buf[1] = ((size >> 21) & 0x7f) | 0x80;
    buf[2] = ((size >> 14) & 0x7f) | 0x80;
    buf[3] = ((size >> 7) & 0x7f) | 0x80;
    buf[4] = (size & 0x7f);
    put(buf, 5);
}

/* iTunes metadata atoms */

static void data_tag(const char *name, uint32_t type, const void *data, size_t len) {
    if (!data) return;
    size_t tag = start_atom(name);
    size_t box = start_atom("data");
    put_u32(type);
    put_u32(0);
    put(data, len);
    end_atom(box);
    end_atom(tag);
}

static void text_tag(const char *name, const char *value) {
    if (value) data_tag(name, 1, value, strlen(value));
}

static void index_tag(const char *name, uint16_t num, uint16_t total) {
    uint8_t data[8] = { 0, 0, (uint8_t)(num >> 8), (uint8_t)num, (uint8_t)(total >> 8), (uint8_t)total };
    data_tag(name, 0, data, sizeof(data));
}

static void freeform_tag(const char *mean, const char *name, const char *value) {
    size_t tag = start_atom("----");
    size_t box = start_atom("mean");
    put_u32(0);
    put(mean, strlen(mean));
    end_atom(box);
    box = start_atom("name");
    put_u32(0);
    put(name, strlen(name));
    end_atom(box);
    box = start_atom("data");
    put_u32(1);
    put_u32(0);
    put(value, strlen(value));
    end_atom(box);
    end_atom(tag);
}

static void write_ilst(void) {
    size_t ilst = start_atom("ilst");
    text_tag("\251too", g.encoder);
    text_tag("\251ART", g.artist); text_tag("soar", g.artist_sort);
    text_tag("\251wrt", g.composer); text_tag("soco", g.composer_sort);
    text_tag("\251nam", g.title); text_tag("\251alb", g.album);
    text_tag("aART", g.album_artist); text_tag("soaa", g.album_artist_sort);
    text_tag("soal", g.album_sort); text_tag("\251day", g.year); text_tag("\251cmt", g.comment);
    if (g.genre) {
        uint8_t genre[2] = { (uint8_t)(g.genre >> 8), (uint8_t)g.genre };
        data_tag("gnre", 0, genre, 2);
    }
    text_tag("sonm", g.title_sort);
    if (g.compilation) { uint8_t flag = 1; data_tag("cpil", 0x15, &flag, 1); }
    if (g.track) index_tag("trkn", g.track, g.ntracks);
    if (g.disc) index_tag("disk", g.disc, g.ndiscs);
    data_tag("covr", 0x0d, g.cover, g.cover_bytes);

    /* Always written, even all-zero: players treat a missing iTunSMPB as
     * "no gapless info" rather than "no padding". */
    char smpb[128];
    snprintf(smpb, sizeof(smpb),
        " 00000000 %08X %08X %08X%08X 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000",
        g.gapless.delay, g.gapless.padding, (uint32_t)(g.gapless.total >> 32), (uint32_t)g.gapless.total);
    freeform_tag("com.apple.iTunes", "iTunSMPB", smpb);
    for (uint32_t i = 0; i < g.ncustom; i++)
        freeform_tag("faac", g.custom[i].name, g.custom[i].value);
    end_atom(ilst);
}

/* Bitrates */

/* Exact while the numerator fits 64 bits; beyond that the recording is long
 * enough that dividing first costs well under 1 % of the rate. */
static uint32_t average_bitrate(void) {
    if (!g.samples) return 0;
    uint64_t scale = (uint64_t)track_rate() * 8;
    uint64_t rate = g.total_bytes <= UINT64_MAX / scale ? g.total_bytes * scale / g.samples
                                                         : g.total_bytes / g.samples * scale;
    return rate > UINT32_MAX ? UINT32_MAX : (uint32_t)rate;
}

uint32_t mp4_avg_bitrate(void) { return average_bitrate(); }

uint32_t mp4_max_bitrate(void) {
    if (g.constant_rate || !g.window_max) return average_bitrate();
    return g.window_max;
}

static void build_moov(void) {
    const uint32_t ts = track_rate();
    const uint32_t channels = g.channels ? g.channels : 2;
    const uint32_t now = g.creation_time ? g.creation_time + MP4_EPOCH_OFFSET : 0;
    const uint64_t duration = g.samples;
    const bool use64_time = duration > 0xFFFFFFFFULL;
    const uint32_t time_flags = use64_time ? (1U << 24) : 0;
    const uint32_t max_bitrate = mp4_max_bitrate(), avg_bitrate = average_bitrate();

    size_t moov = start_atom("moov");
    size_t mvhd = start_atom("mvhd");
    put_u32(time_flags);
    put_time(now, use64_time); put_time(now, use64_time);
    put_u32(ts); put_time(duration, use64_time);
    put_u32(MP4_FP1616_ONE); put_u16(MP4_FP0808_ONE); put_u16(0); put_u32(0); put_u32(0);
    put_u32(MP4_FP1616_ONE); put_u32(0); put_u32(0);
    put_u32(0); put_u32(MP4_FP1616_ONE); put_u32(0);
    put_u32(0); put_u32(0); put_u32(MP4_FP0230_ONE);
    for (int i = 0; i < 6; i++) put_u32(0);
    put_u32(2); /* next track id */
    end_atom(mvhd);

    size_t trak = start_atom("trak");
    size_t tkhd = start_atom("tkhd");
    put_u32(time_flags | 1);
    put_time(now, use64_time); put_time(now, use64_time);
    put_u32(1); put_u32(0);
    put_time(duration, use64_time);
    put_u32(0); put_u32(0);
    put_u16(0); put_u16(0); put_u16(MP4_FP0808_ONE); put_u16(0);
    put_u32(MP4_FP1616_ONE); put_u32(0); put_u32(0);
    put_u32(0); put_u32(MP4_FP1616_ONE); put_u32(0);
    put_u32(0); put_u32(0); put_u32(MP4_FP0230_ONE);
    put_u32(0); put_u32(0); /* width, height */
    end_atom(tkhd);

    if (g.gapless.delay > 0) {
        size_t edts = start_atom("edts");
        size_t elst = start_atom("elst");
        put_u32(time_flags);
        put_u32(1);
        put_time(g.gapless.total, use64_time);
        put_time(g.gapless.delay, use64_time);
        put_u16(1); put_u16(0);
        end_atom(elst);
        end_atom(edts);
    }

    size_t mdia = start_atom("mdia");
    size_t mdhd = start_atom("mdhd");
    put_u32(time_flags);
    put_time(now, use64_time); put_time(now, use64_time);
    put_u32(ts); put_time(g.samples, use64_time);
    put_u16(pack_language(g.language)); put_u16(0);
    end_atom(mdhd);

    size_t hdlr = start_atom("hdlr");
    put_u32(0); put_u32(0);
    put("soun", 4);
    put_u32(0); put_u32(0); put_u32(0); put_u8(0);
    end_atom(hdlr);

    size_t minf = start_atom("minf");
    size_t smhd = start_atom("smhd");
    put_u32(0); put_u16(0); put_u16(0);
    end_atom(smhd);

    size_t dinf = start_atom("dinf");
    size_t dref = start_atom("dref");
    put_u32(0); put_u32(1);
    size_t url = start_atom("url ");
    put_u32(1); /* self-contained */
    end_atom(url);
    end_atom(dref);
    end_atom(dinf);

    size_t stbl = start_atom("stbl");
    size_t stsd = start_atom("stsd");
    put_u32(0); put_u32(1);
    size_t mp4a = start_atom("mp4a");
    for (int i = 0; i < 6; i++) put_u8(0);
    put_u16(1); put_u32(0); put_u32(0);
    put_u16((uint16_t)channels);
    put_u16(g.sample_bits);
    put_u16(0); put_u16(0);
    put_u16((uint16_t)(ts > UINT16_MAX ? UINT16_MAX : ts));
    put_u16(0);

    size_t esds = start_atom("esds");
    put_u32(0);
    put_descriptor(3, 3 + MP4_DESC_HDR + 13 + MP4_DESC_HDR + g.asc_len + MP4_DESC_HDR + 1);
    put_u16(0); put_u8(0);
    put_descriptor(4, 13 + MP4_DESC_HDR + g.asc_len);
    put_u8(MP4_OBJECT_TYPE_AUDIO_ISO_14496_3); put_u8(MP4_STREAM_TYPE_AUDIO);
    uint32_t buffer_size = MP4_DECODER_BUFFER_BYTES_PER_CH * channels;
    put_u8((uint8_t)(buffer_size >> 16));
    put_u8((uint8_t)(buffer_size >> 8));
    put_u8((uint8_t)buffer_size);
    put_u32(max_bitrate); put_u32(avg_bitrate);
    put_descriptor(5, g.asc_len);
    put(g.asc, g.asc_len);
    put_descriptor(6, 1); put_u8(2);
    end_atom(esds);
    end_atom(mp4a);
    end_atom(stsd);

    size_t stts = start_atom("stts");
    put_u32(0); put_u32(g.nstts);
    for (uint32_t i = 0; i < g.nstts; i++) {
        put_u32(g.stts[i].count);
        put_u32(g.stts[i].delta);
    }
    end_atom(stts);

    /* One chunk holds every sample. */
    size_t stsc = start_atom("stsc");
    put_u32(0); put_u32(1);
    put_u32(1); put_u32(g.nsamples); put_u32(1);
    end_atom(stsc);

    size_t stsz = start_atom("stsz");
    put_u32(0); put_u32(0); put_u32(g.nsamples);
    for (uint32_t i = 0; i < g.nsamples; i++) put_u32(g.sizes[i]);
    end_atom(stsz);

    if (MDAT_DATA_POS + g.mdat_size > UINT32_MAX) {
        size_t co64 = start_atom("co64");
        put_u32(0); put_u32(1); put_u64(MDAT_DATA_POS);
        end_atom(co64);
    } else {
        size_t stco = start_atom("stco");
        put_u32(0); put_u32(1); put_u32(MDAT_DATA_POS);
        end_atom(stco);
    }
    end_atom(stbl);
    end_atom(minf);
    end_atom(mdia);
    end_atom(trak);

    size_t udta = start_atom("udta");
    size_t meta = start_atom("meta");
    put_u32(0);
    size_t hdlr2 = start_atom("hdlr");
    put_u32(0); put_u32(0); put("mdirappl", 8);
    put_u32(0); put_u32(0); put_u8(0);
    end_atom(hdlr2);
    write_ilst();
    end_atom(meta);
    end_atom(udta);
    end_atom(moov);
}

static void write_moov(void) {
    g.in_moov = true;
    g.moov_len = 0;
    build_moov();
    g.in_moov = false;
    if (!g.error && fwrite(g.moov, 1, g.moov_len, g.f) != g.moov_len) g.error = true;
}

static bool start_file(void) {
    static const uint8_t header[44] = {
        0x00, 0x00, 0x00, 0x1c, 'f', 't', 'y', 'p',
        /* Apple's decoders only honour iTunSMPB/edit-list gapless trimming
         * in files branded M4A/M4B. */
        'M', '4', 'A', ' ', 0, 0, 0, 0, 'M', '4', 'A', ' ', 'm', 'p', '4', '2', 'i', 's', 'o', 'm',
        /* 'wide' placeholder so a >4 GiB mdat header can later grow in place */
        0x00, 0x00, 0x00, 0x08, 'w', 'i', 'd', 'e',
        0x00, 0x00, 0x00, 0x00, 'm', 'd', 'a', 't',
    };
    put(header, sizeof(header));
    return !g.error;
}

/* Public API */

int mp4_open(const char *path, bool overwrite) {
#ifdef _WIN32
    if (!overwrite && win32_access_utf8(path, 0) == 0) return 1;
#else
    if (!overwrite && access(path, 0) == 0) return 1;
#endif
    g.f = cli_fopen(path, "wb");
    if (!g.f) return 1;

    /* Frames are a few hundred bytes each; batch them into large sequential
     * writes instead of the platform's default stdio buffer. */
    setvbuf(g.f, g.iobuf, _IOFBF, sizeof(g.iobuf));
    g.rate = 44100;
    g.channels = 0;
    g.language[0] = 0;
    g.creation_time = 0;
    g.constant_rate = false;
    memset(&g.gapless, 0, sizeof(g.gapless));
    return 0;
}

void mp4_set_creation_time(uint32_t t) { g.creation_time = t; }

void mp4_set_format(uint32_t samplerate, uint32_t channels, uint32_t bits) {
    g.rate = samplerate;
    g.channels = channels;
    g.sample_bits = (uint16_t)bits;
}

void mp4_set_constant_rate(bool constant) { g.constant_rate = constant; }

void mp4_set_decoder_config(const uint8_t *asc, unsigned long size) {
    g.asc = asc;
    g.asc_len = (uint32_t)size;
}

void mp4_set_encoder(const char *value) { g.encoder = value; }

void mp4_set_tag(mp4_tag_id_t id, const char *value) {
    if (!value) return;
    switch (id) {
    case MP4TAG_ARTIST: g.artist = value; break;
    case MP4TAG_ARTISTSORT: g.artist_sort = value; break;
    case MP4TAG_TITLE: g.title = value; break;
    case MP4TAG_ALBUM: g.album = value; break;
    case MP4TAG_ALBUMSORT: g.album_sort = value; break;
    case MP4TAG_ALBUMARTIST: g.album_artist = value; break;
    case MP4TAG_ALBUMARTISTSORT: g.album_artist_sort = value; break;
    case MP4TAG_COMPOSER: g.composer = value; break;
    case MP4TAG_COMPOSERSORT: g.composer_sort = value; break;
    case MP4TAG_YEAR: g.year = value; break;
    case MP4TAG_COMMENT: g.comment = value; break;
    default: break;
    }
}

void mp4_set_genre(uint16_t genre) { g.genre = genre; }

void mp4_set_language(const char *lang) {
    memset(g.language, 0, sizeof(g.language));
    if (lang && strlen(lang) >= 3) memcpy(g.language, lang, 3);
}

void mp4_set_compilation(bool flag) { g.compilation = flag; }

void mp4_set_track(uint16_t num, uint16_t total) { g.track = num; g.ntracks = total; }

void mp4_set_disc(uint16_t num, uint16_t total) { g.disc = num; g.ndiscs = total; }

void mp4_set_cover(const uint8_t *data, uint32_t size) { g.cover = data; g.cover_bytes = size; }

void mp4_set_gapless(uint32_t priming, uint32_t padding, uint64_t original_samples) {
    g.gapless.delay = priming;
    g.gapless.padding = padding;
    g.gapless.total = original_samples;
}

/* The name is borrowed; the value is copied because callers free their
 * UTF-8 conversion right after the call. */
int mp4_add_custom_tag(const char *name, const char *value) {
    if (!name || !value) return -1;
    if (g.ncustom == g.custom_cap) {
        uint32_t cap = g.custom_cap ? g.custom_cap * 2 : 8;
        custom_tag *tags = (custom_tag *)realloc(g.custom, (size_t)cap * sizeof(*tags));
        if (!tags) return -1;
        g.custom = tags;
        g.custom_cap = cap;
    }
    char *copy = (char *)malloc(strlen(value) + 1);
    if (!copy) return -1;
    memcpy(copy, value, strlen(value) + 1);
    g.custom[g.ncustom].name = name;
    g.custom[g.ncustom].value = copy;
    g.ncustom++;
    return 0;
}

int mp4_write_frame(const uint8_t *data, uint32_t size, uint32_t samples) {
    if (g.error || g.finalized || !data || !size || g.asc_len > 256) return -1;
    if (!g.nsamples && !g.mdat_size && !start_file()) return -1;

    put(data, size);
    if (g.error) return -1;
    g.mdat_size += size;
    g.total_bytes += size;
    g.samples += samples;

    if (g.nsamples == g.sizes_cap) {
        uint32_t cap = g.sizes_cap ? g.sizes_cap * 2 : 1024;
        if (cap < g.sizes_cap) return -1;
        uint32_t *tmp = (uint32_t *)realloc(g.sizes, (size_t)cap * sizeof(*tmp));
        if (!tmp) return -1;
        g.sizes = tmp;
        g.sizes_cap = cap;
    }
    g.sizes[g.nsamples++] = size;

    /* Short drain frames do not contribute to a full-length bitrate window. */
    if (g.last_frame_samples <= samples) {
        uint32_t ts = track_rate();
        g.window_bytes += size;
        g.window_ticks += samples;
        if (g.window_ticks >= ts) {
            uint32_t rate = (uint32_t)(8 * g.window_bytes * ts / g.window_ticks);
            if (g.window_max < rate) g.window_max = rate;
            g.window_bytes = g.window_ticks = 0;
        }
        g.last_frame_samples = samples;
    }
    if (g.max_frame_size < size) g.max_frame_size = size;

    if (g.nstts && g.stts[g.nstts - 1].delta == samples) {
        g.stts[g.nstts - 1].count++;
    } else {
        if (g.nstts == g.stts_cap) {
            uint32_t cap = g.stts_cap ? g.stts_cap * 2 : 16;
            void *tmp = realloc(g.stts, (size_t)cap * sizeof(*g.stts));
            if (!tmp) return -1;
            g.stts = tmp;
            g.stts_cap = cap;
        }
        g.stts[g.nstts].count = 1;
        g.stts[g.nstts].delta = samples;
        g.nstts++;
    }
    return 0;
}

int mp4_finish(void) {
    if (!g.f || g.error || !g.nsamples) return 1;
    if (g.finalized) return 0;

    uint64_t pos = tell_pos();
    bool large = g.mdat_size > UINT32_MAX - 8ULL;
    if (!seek_to(MDAT_DATA_POS - (large ? 16 : 8))) return 1;
    if (large) {
        put_u32(1);
        put("mdat", 4);
        put_u64(g.mdat_size + 16);
    } else {
        put_u32((uint32_t)(g.mdat_size + 8));
    }
    if (!seek_to(pos)) return 1;

    write_moov();
    if (!g.error && fflush(g.f) != 0) g.error = true;
    g.finalized = !g.error;
    return g.error ? 1 : 0;
}

int mp4_close(void) {
    for (uint32_t i = 0; i < g.ncustom; i++) free(g.custom[i].value);
    free(g.custom);
    g.custom = NULL;
    g.ncustom = g.custom_cap = 0;
    free(g.moov);
    g.moov = NULL;
    g.moov_len = g.moov_cap = 0;
    free(g.sizes);
    g.sizes = NULL;
    free(g.stts);
    g.stts = NULL;
    g.nsamples = g.sizes_cap = g.nstts = g.stts_cap = 0;
    g.mdat_size = g.total_bytes = g.samples = 0;
    g.max_frame_size = g.last_frame_samples = g.window_max = 0;
    g.window_bytes = g.window_ticks = 0;
    g.finalized = g.error = false;
    if (g.f) {
        int result = fclose(g.f);
        g.f = NULL;
        return result != 0;
    }
    return 0;
}

uint32_t mp4_frame_count(void) { return g.nsamples; }
uint64_t mp4_sample_count(void) { return g.samples; }
uint32_t mp4_max_frame_size(void) { return g.max_frame_size; }
