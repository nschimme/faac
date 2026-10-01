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
 * Full Thread-Safe Multi-Track ISO BMFF Muxer Engine for libfaam
 * Supports audio (AAC) and video (H.264/AVC, H.265/HEVC) tracks.
 */

#include <stdio.h>
#include "libfaam_internal.h"
#include "endian.h"

enum {
    MP4_EPOCH_OFFSET = 2082844800,
    MP4_FP1616_ONE = 0x00010000,
    MP4_FP0230_ONE = 0x40000000,
    MP4_FP0808_ONE = 0x0100,
    MP4_DESC_HDR = 5,
    ITUNES_DATA_BINARY = 0,
    ITUNES_DATA_TEXT   = 1,
    ITUNES_DATA_UINT8  = 0x15,
    ITUNES_DATA_IMAGE  = 0x0d,
    MP4_OBJECT_TYPE_AUDIO_ISO_14496_3 = 0x40,
    MP4_STREAM_TYPE_AUDIO             = 0x15,
    MP4_DECODER_BUFFER_BYTES_PER_CH   = 6144 / 8,
    MP4_URL_SELF_CONTAINED = 1,
    ISO639_UND_PACKED  = 0x55C4,
};

#ifdef FAAM_MUXER_FRAGMENTED
#define FRAGMENTED(m) ((m)->fragmented)
#else
#define FRAGMENTED(m) false
#endif

static void flush_staging(faam_muxer *m) {
    if (m->error || !m->staged_bytes) return;
    if (m->io.write(m->io.user_data, m->staging, m->staged_bytes) != (int32_t)m->staged_bytes)
        m->error = FAAM_ERR_IO_WRITE;
    m->staged_bytes = 0;
}

static void mem_write(faam_muxer *m, const void *data, size_t size) {
    const uint8_t *p = (const uint8_t *)data;
    while (size && !m->error) {
        uint32_t n = (uint32_t)(size < sizeof(m->staging) - m->staged_bytes ?
            size : sizeof(m->staging) - m->staged_bytes);
        memcpy(m->staging + m->staged_bytes, p, n);
        m->staged_bytes += n;
        p += n;
        size -= n;
        if (m->staged_bytes == sizeof(m->staging)) flush_staging(m);
    }
}

static inline void put_u32(faam_muxer *m, uint32_t val) {
    uint8_t buf[4];
    write_u32_be(buf, val);
    mem_write(m, buf, 4);
}

static inline void put_u16(faam_muxer *m, uint16_t val) {
    uint8_t buf[2];
    write_u16_be(buf, val);
    mem_write(m, buf, 2);
}

static inline void put_u64(faam_muxer *m, uint64_t val) {
    uint8_t buf[8];
    write_u64_be(buf, val);
    mem_write(m, buf, 8);
}

static inline void put_time(faam_muxer *m, uint64_t val, bool use64) {
    if (use64) put_u64(m, val); else put_u32(m, (uint32_t)val);
}

static inline void put_u8(faam_muxer *m, uint8_t val) { mem_write(m, &val, 1); }
static inline void put_data(faam_muxer *m, const void *data, size_t size) { mem_write(m, data, size); }

static uint64_t start_atom(faam_muxer *m, const char *name) {
    uint64_t pos = m->io.tell(m->io.user_data) + m->staged_bytes;
    put_u32(m, 0);
    put_data(m, name, 4);
    return pos;
}

static void end_atom(faam_muxer *m, uint64_t pos) {
    if (m->error) return;
    flush_staging(m);
    if (m->error) return;
    uint64_t curr = m->io.tell(m->io.user_data);
    if (curr - pos > UINT32_MAX) { m->error = FAAM_ERR_UNSUPPORTED; return; }
    if (!m->io.seek(m->io.user_data, pos)) { m->error = FAAM_ERR_UNSUPPORTED; return; }
    uint8_t size[4];
    write_u32_be(size, (uint32_t)(curr - pos));
    if (m->io.write(m->io.user_data, size, 4) != 4) m->error = FAAM_ERR_IO_WRITE;
    if (!m->io.seek(m->io.user_data, curr)) m->error = FAAM_ERR_UNSUPPORTED;
}

#ifdef FAAM_MUXER_VIDEO
/* With B-frames the first displayed frame trails the first decoded one; the
 * edit list starts the media at that frame so it plays at t=0. */
static uint64_t edit_media_time(const faam_muxer_track *tr)
{
    return tr->ctts_entries && tr->min_pts > 0 ? (uint64_t)tr->min_pts : 0;
}
#endif

static uint32_t track_timescale(const faam_muxer_track *tr)
{
    return tr->cfg.timescale ? tr->cfg.timescale : tr->cfg.track_type == FAAM_TRACK_AUDIO ? 44100 : 90000;
}

/* Container durations retain the encoded media length, including priming. */
static uint64_t presentation_samples(const faam_muxer *m, const faam_muxer_track *tr) {
    (void)m;
    uint64_t ticks = tr->bitrate_window.samples;
#ifdef FAAM_MUXER_VIDEO
    uint64_t skip = edit_media_time(tr);
    ticks = ticks > skip ? ticks - skip : 0;
#endif
    return ticks;
}

static uint16_t pack_language(const char *lang) {
    if (!lang || strlen(lang) < 3) return ISO639_UND_PACKED;
    uint16_t packed = 0;
    for (unsigned i = 0; i < 3; i++) {
        unsigned c = (unsigned char)lang[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c < 'a' || c > 'z') return ISO639_UND_PACKED;
        packed = (uint16_t)((packed << 5) | (c - 0x60));
    }
    return packed;
}

static void put_descriptor(faam_muxer *m, uint8_t tag, uint32_t size) {
    uint8_t buf[5];
    buf[0] = tag;
    buf[1] = ((size >> 21) & 0x7f) | 0x80;
    buf[2] = ((size >> 14) & 0x7f) | 0x80;
    buf[3] = ((size >> 7) & 0x7f) | 0x80;
    buf[4] = (size & 0x7f);
    mem_write(m, buf, 5);
}

static faam_status write_ilst_bytes(void *user, const void *data, uint32_t size) {
    faam_muxer *m = (faam_muxer *)user;
    mem_write(m, data, size);
    return m->error;
}

faam_status faam_muxer_config_init(faam_muxer_config *cfg, uint32_t caller_size)
{
    if (!cfg || caller_size < sizeof(faam_muxer_config)) return FAAM_ERR_INVALID_ARG;
    memset(cfg, 0, caller_size);
    cfg->struct_size = caller_size;

    return FAAM_OK;
}

/* Video is a compile-time option; a disabled build reports UNSUPPORTED, not a malformed track. */
static faam_status check_track(const faam_track_config *tr) {
#ifndef FAAM_MUXER_VIDEO
    if (tr->track_type == FAAM_TRACK_VIDEO) return FAAM_ERR_UNSUPPORTED;
#endif
    if (tr->track_id == UINT32_MAX || tr->codec_data_len > 256 ||
        (tr->codec_data_len && !tr->codec_data)) return FAAM_ERR_INVALID_ARG;
    if (tr->track_type == FAAM_TRACK_AUDIO)
        return tr->codec_id == FAAM_CODEC_AAC && !tr->flags ? FAAM_OK : FAAM_ERR_INVALID_ARG;
    if (tr->track_type != FAAM_TRACK_VIDEO || !tr->width || !tr->height) return FAAM_ERR_INVALID_ARG;
    if (tr->codec_id != FAAM_CODEC_H264 && tr->codec_id != FAAM_CODEC_H265) return FAAM_ERR_INVALID_ARG;
    if (tr->flags & ~(uint32_t)FAAM_TRACK_ANNEXB) return FAAM_ERR_INVALID_ARG;
    /* Annex-B H.264 streams carry their own SPS/PPS, so avcC may be derived. */
    bool derive = (tr->flags & FAAM_TRACK_ANNEXB) && tr->codec_id == FAAM_CODEC_H264;
    return (tr->codec_data && tr->codec_data_len) || (derive && !tr->codec_data_len) ?
        FAAM_OK : FAAM_ERR_INVALID_ARG;
}

faam_status faam_muxer_config_add_track(faam_muxer_config *cfg, const faam_track_config *track, uint32_t *out_track_id)
{
    if (!cfg || !track) return FAAM_ERR_INVALID_ARG;
    faam_status st = check_track(track);
    if (st != FAAM_OK) return st;
    if (cfg->num_tracks >= 8) return FAAM_ERR_INVALID_ARG;
    uint32_t id = track->track_id ? track->track_id : cfg->num_tracks + 1;
    for (uint32_t t = 0; t < cfg->num_tracks; t++)
        if (cfg->tracks[t].track_id == id) return FAAM_ERR_INVALID_ARG;

    uint32_t idx = cfg->num_tracks;
    cfg->tracks[idx] = *track;
    cfg->tracks[idx].track_id = track->track_id ? track->track_id : (idx + 1);
    cfg->num_tracks++;

    if (out_track_id) *out_track_id = cfg->tracks[idx].track_id;
    return FAAM_OK;
}

#ifdef FAAM_MUXER_FRAGMENTED
enum {
    FRAG_SAMPLES_PER_SEC = 128,
    FRAG_MIN_SAMPLES = 64,
    FRAG_MAX_SAMPLES = 1 << 18,
    FRAG_MAX_BYTES = 1 << 30, /* keeps trun data offsets inside int32 */
    SAMPLE_FLAGS_SYNC = 0x02000000,     /* depends on no other sample */
    SAMPLE_FLAGS_NON_SYNC = 0x01010000, /* depends on others, not a sync sample */
    TRUN_FLAGS = 0x701,                 /* data offset, duration, size and flags per sample */
    TRUN_CTS = 0x800,
    TFHD_DEFAULT_BASE_IS_MOOF = 0x20000,
};

static uint32_t frag_capacity(uint32_t ms)
{
    uint64_t n = (uint64_t)ms * FRAG_SAMPLES_PER_SEC / 1000;
    return n < FRAG_MIN_SAMPLES ? FRAG_MIN_SAMPLES : n > FRAG_MAX_SAMPLES ? FRAG_MAX_SAMPLES : (uint32_t)n;
}

/* Index bytes per sample: offset, duration, flags, track, plus cts for video files. */
static uint32_t frag_index_bytes(const faam_muxer_config *cfg)
{
    bool video = false;
    for (uint32_t t = 0; t < cfg->num_tracks; t++)
        if (cfg->tracks[t].track_type == FAAM_TRACK_VIDEO) video = true;
    return frag_capacity(cfg->fragment_ms) * (video ? 14 : 10);
}

/* The moof is written once the fragment closes, so room for the largest one the
 * index can describe is held back ahead of mdat: moof + mfhd, a traf per track and,
 * in the worst case, a run (trun header plus a full entry) per sample, and a free
 * box header so the unused tail is always a valid box. */
static uint32_t frag_reserve_bytes(uint32_t tracks, uint32_t cap)
{
    return 8 + 16 + tracks * 44 + cap * (20 + 16) + 8;
}
#endif

faam_status faam_muxer_get_state_size(const faam_muxer_config *cfg, uint32_t *state_bytes)
{
    if (!cfg || !state_bytes || cfg->num_tracks > FAAM_MAX_TRACKS) return FAAM_ERR_INVALID_ARG;
    uint32_t count = cfg->num_tracks ? cfg->num_tracks : 1;
    uint64_t bytes = sizeof(struct faam_muxer) + count * sizeof(faam_muxer_track);
    if (cfg->fragment_ms) {
#ifdef FAAM_MUXER_FRAGMENTED
        bytes += frag_index_bytes(cfg);
#else
        return FAAM_ERR_UNSUPPORTED;
#endif
    }
    *state_bytes = (uint32_t)bytes;
    return FAAM_OK;
}

static void write_moov(faam_muxer *m, bool frag);

faam_status faam_muxer_init(void *mem_buf, uint32_t mem_bytes, const faam_muxer_config *cfg, const faam_io *io, faam_muxer **out_muxer)
{
    uint32_t required;
    if (!cfg || faam_muxer_get_state_size(cfg, &required) != FAAM_OK ||
        !mem_buf || mem_bytes < required || !io || !out_muxer) {
        return FAAM_ERR_INVALID_ARG;
    }

    *out_muxer = NULL;
#ifdef FAAM_MUXER_FRAGMENTED
    if (cfg->fragment_ms && (!io->seek || !io->tell)) return FAAM_ERR_UNSUPPORTED;
#endif
    if (!io->write || cfg->num_tracks > FAAM_MAX_TRACKS || cfg->num_chapters > 255 ||
        (cfg->num_chapters && !cfg->chapters)) return FAAM_ERR_INVALID_ARG;
    for (uint32_t t = 0; t < cfg->num_tracks; t++) {
        faam_status ts = check_track(&cfg->tracks[t]);
        if (ts != FAAM_OK) return ts;
        if (!cfg->tracks[t].track_id) return FAAM_ERR_INVALID_ARG;
        /* The moov precedes every frame, so a fragmented file cannot wait for an avcC. */
        if (cfg->fragment_ms && cfg->tracks[t].track_type == FAAM_TRACK_VIDEO && !cfg->tracks[t].codec_data_len)
            return FAAM_ERR_INVALID_ARG;
        for (uint32_t k = 0; k < t; k++)
            if (cfg->tracks[k].track_id == cfg->tracks[t].track_id) return FAAM_ERR_INVALID_ARG;
    }
    struct faam_muxer *m = (struct faam_muxer *)mem_buf;
    memset(m, 0, required);
    m->cfg.creation_time = cfg->creation_time;
    m->cfg.constant_rate = cfg->constant_rate;
    m->cfg.is_m4b = cfg->is_m4b;
    m->cfg.gapless = cfg->gapless;
    m->cfg.metadata = cfg->metadata;
    m->cfg.chapters = cfg->chapters;
    m->cfg.num_chapters = cfg->num_chapters;
    m->gapless_present = cfg->gapless.encoder_delay || cfg->gapless.end_padding || cfg->gapless.total_samples;
    m->io = *io;
#ifdef FAAM_MUXER_FRAGMENTED
    if (cfg->fragment_ms) {
        m->fragmented = true;
        m->fragment_ms = cfg->fragment_ms;
        m->frag_cap = frag_capacity(cfg->fragment_ms);
        m->frag_reserve = frag_reserve_bytes(cfg->num_tracks ? cfg->num_tracks : 1, m->frag_cap);
        for (uint32_t t = 0; t < cfg->num_tracks; t++)
            if (cfg->tracks[t].track_type == FAAM_TRACK_VIDEO) m->has_video = true;
        uint8_t *arena = (uint8_t *)&m->tracks[cfg->num_tracks ? cfg->num_tracks : 1];
        m->fi_off = (uint32_t *)arena; arena += m->frag_cap * sizeof(uint32_t);
        m->fi_dur = (uint32_t *)arena; arena += m->frag_cap * sizeof(uint32_t);
        if (m->has_video) { m->fi_cts = (int32_t *)arena; arena += m->frag_cap * sizeof(int32_t); }
        m->fi_flags = arena; arena += m->frag_cap;
        m->fi_track = arena;
    }
#endif

    faam_track_config def_track = {0};
    def_track.track_type = FAAM_TRACK_AUDIO;
    def_track.codec_id = FAAM_CODEC_AAC;
    def_track.timescale = def_track.sample_rate = 44100;
    def_track.channels = 2;
    def_track.track_id = 1;
    m->num_tracks = cfg->num_tracks ? cfg->num_tracks : 1;
    for (uint32_t t = 0; t < m->num_tracks; t++) {
        faam_muxer_track *tr = &m->tracks[t];
        tr->cfg = cfg->num_tracks ? cfg->tracks[t] : def_track;
        tr->audio_sample_size = 16;
        if (tr->cfg.codec_data && tr->cfg.codec_data_len > 0) {
            uint32_t len = tr->cfg.codec_data_len < sizeof(tr->codec_data) ? tr->cfg.codec_data_len : (uint32_t)sizeof(tr->codec_data);
            memcpy(tr->codec_data, tr->cfg.codec_data, len);
            tr->codec_data_len = len;
        }

#ifdef FAAM_MUXER_FRAGMENTED
        if (m->fragmented) continue;
#endif
        tr->sample_capacity = 1024;
        tr->sample_sizes = (uint32_t *)AllocMemory(tr->sample_capacity * sizeof(uint32_t));
        if (m->num_tracks > 1)
            tr->sample_offsets = (uint64_t *)AllocMemory(tr->sample_capacity * sizeof(uint64_t));
        if (!tr->sample_sizes || (m->num_tracks > 1 && !tr->sample_offsets)) {
            faam_muxer_close(&m);
            return FAAM_ERR_INSUFFICIENT_MEM;
        }

        tr->stts_capacity = 16;
        tr->stts_entries = (faam_stts_entry *)AllocMemoryFast(tr->stts_capacity * sizeof(faam_stts_entry));
        if (!tr->stts_entries) {
            faam_muxer_close(&m);
            return FAAM_ERR_INSUFFICIENT_MEM;
        }
        memset(tr->stts_entries, 0, tr->stts_capacity * sizeof(faam_stts_entry));

#ifdef FAAM_MUXER_VIDEO
        tr->stss_capacity = 16;
        tr->stss_entries = (uint32_t *)AllocMemoryFast(tr->stss_capacity * sizeof(uint32_t));
        if (!tr->stss_entries) {
            faam_muxer_close(&m);
            return FAAM_ERR_INSUFFICIENT_MEM;
        }
        memset(tr->stss_entries, 0, tr->stss_capacity * sizeof(uint32_t));
#endif
    }

    /* Apple's decoders only honour iTunSMPB/edit-list gapless trimming in
     * files branded M4A/M4B, so audio-only files carry that brand. */
    bool audio_only = true;
    for (uint32_t t = 0; t < m->num_tracks; t++)
        if (m->tracks[t].cfg.track_type != FAAM_TRACK_AUDIO) audio_only = false;
#ifdef FAAM_MUXER_FRAGMENTED
    if (m->fragmented) {
        /* iso5 is the first brand that allows default-base-is-moof. */
        put_data(m, "\0\0\0\x20" "ftypisom\0\0\x02\0isomiso5iso6mp41", 32);
        write_moov(m, true);
        flush_staging(m);
        if (!m->error && m->io.flush && !m->io.flush(m->io.user_data)) m->error = FAAM_ERR_IO_WRITE;
        if (m->error) {
            faam_status st = m->error;
            faam_muxer_close(&m);
            return st;
        }
        *out_muxer = m;
        return FAAM_OK;
    }
#endif
    const char *brands = !audio_only    ? "isom\0\0\0\0isomiso2mp41"
                       : m->cfg.is_m4b  ? "M4B \0\0\0\0M4B mp42isom"
                                        : "M4A \0\0\0\0M4A mp42isom";
    uint8_t hdr[36] = { 0x00, 0x00, 0x00, 0x1c, 'f', 't', 'y', 'p' };
    memcpy(hdr + 8, brands, 20);
    /* 'wide' placeholder so a >4 GiB mdat header can later grow in place. */
    memcpy(hdr + 28, "\0\0\0\x08wide", 8);
    if (m->io.write(m->io.user_data, hdr, sizeof(hdr)) != sizeof(hdr)) {
        faam_muxer_close(&m); return FAAM_ERR_IO_WRITE;
    }

    uint8_t mdat_hdr[8] = { 0x00, 0x00, 0x00, 0x00, 'm', 'd', 'a', 't' };
    if (m->io.write(m->io.user_data, mdat_hdr, 8) != 8) {
        faam_muxer_close(&m); return FAAM_ERR_IO_WRITE;
    }
    m->mdat_pos = m->io.tell ? m->io.tell(m->io.user_data) : 44;

    *out_muxer = m;
    return FAAM_OK;
}

faam_status faam_muxer_set_creation_time(faam_muxer *m, uint32_t unix_time) {
    if (!m) return FAAM_ERR_INVALID_ARG;
    if (FRAGMENTED(m)) return FAAM_ERR_UNSUPPORTED; /* moov is already on disk */
    m->cfg.creation_time = unix_time;
    return FAAM_OK;
}

FAAMAPI faam_status faam_muxer_set_gapless(faam_muxer *m, const faam_gapless_info *gapless) {
    if (!m || !gapless) return FAAM_ERR_INVALID_ARG;
    if (FRAGMENTED(m)) return FAAM_ERR_UNSUPPORTED;
    m->cfg.gapless = *gapless;
    m->gapless_present = true;
    return FAAM_OK;
}

faam_status faam_muxer_set_audio_sample_size(faam_muxer *m, uint32_t track_id, uint16_t bits) {
    if (!m || !bits || m->finalized) return FAAM_ERR_INVALID_ARG;
    if (FRAGMENTED(m)) return FAAM_ERR_UNSUPPORTED;
    for (uint32_t t = 0; t < m->num_tracks; t++) {
        if (m->tracks[t].cfg.track_id == track_id && m->tracks[t].cfg.track_type == FAAM_TRACK_AUDIO) {
            m->tracks[t].audio_sample_size = bits;
            return FAAM_OK;
        }
    }
    return FAAM_ERR_NO_TRACK;
}

#ifdef FAAM_MUXER_VIDEO
/* Zero offsets are only counted until the first non-zero one, at which point
 * the leading zero run becomes the first entry. */
static bool add_ctts(faam_muxer *m, faam_muxer_track *tr, int32_t offset)
{
    if (!tr->ctts_entries && offset == 0) return true;
    if (!tr->ctts_entries) {
        tr->ctts_capacity = 16;
        tr->ctts_entries = (faam_ctts_entry *)AllocMemory(tr->ctts_capacity * sizeof(faam_ctts_entry));
        if (!tr->ctts_entries) { m->error = FAAM_ERR_INSUFFICIENT_MEM; return false; }
        if (tr->sample_count) {
            tr->ctts_entries[0].count = tr->sample_count;
            tr->ctts_entries[0].offset = 0;
            tr->ctts_count = 1;
        }
    }
    if (tr->ctts_count && tr->ctts_entries[tr->ctts_count - 1].offset == offset) {
        tr->ctts_entries[tr->ctts_count - 1].count++;
        return true;
    }
    if (tr->ctts_count >= tr->ctts_capacity) {
        uint32_t new_cap = tr->ctts_capacity * 2;
        faam_ctts_entry *tmp = (faam_ctts_entry *)ReallocMemory(tr->ctts_entries, new_cap * sizeof(faam_ctts_entry));
        if (!tmp) { m->error = FAAM_ERR_INSUFFICIENT_MEM; return false; }
        tr->ctts_entries = tmp;
        tr->ctts_capacity = new_cap;
    }
    tr->ctts_entries[tr->ctts_count].count = 1;
    tr->ctts_entries[tr->ctts_count].offset = offset;
    tr->ctts_count++;
    return true;
}

#endif

#ifdef FAAM_MUXER_VIDEO
/* Returns 1 with the next NAL unit's payload in [*start, *start + *len), 0 at the end
 * and -1 when non-zero bytes precede the first start code. Zero bytes ahead of the
 * next start code (zero_byte, trailing_zero_8bits) are not part of the NAL unit
 * because an RBSP never ends in 0x00. */
static int annexb_next(const uint8_t *b, uint32_t n, uint32_t *pos, uint32_t *start, uint32_t *len)
{
    uint32_t i = *pos;
    while (i + 3 <= n && !(b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 1)) i++;
    if (i + 3 > n) return 0;
    if (!*pos)
        for (uint32_t k = 0; k < i; k++)
            if (b[k]) return -1;
    uint32_t s = i + 3, e = s;
    while (e + 3 <= n && !(b[e] == 0 && b[e + 1] == 0 && b[e + 2] == 1)) e++;
    if (e + 3 > n) e = n;
    *pos = e;
    while (e > s && b[e - 1] == 0) e--;
    *start = s;
    *len = e - s;
    return 1;
}

/* avcC wants the first SPS and the first PPS after it; both are held in
 * codec_data (SPS in its final place) until the PPS completes the record. */
static void capture_avcc(faam_muxer_track *tr, const uint8_t *nal, uint32_t n)
{
    if (tr->codec_data_len || n < 4) return;
    uint32_t type = nal[0] & 0x1F;
    if (type == 7 && !tr->sps_len && 8 + n + 3 < sizeof(tr->codec_data)) {
        memcpy(tr->codec_data + 8, nal, n);
        tr->sps_len = n;
    } else if (type == 8 && tr->sps_len && 8 + tr->sps_len + 3 + n <= sizeof(tr->codec_data)) {
        uint8_t *c = tr->codec_data;
        c[0] = 1;
        memcpy(c + 1, c + 9, 3);   /* profile, compatibility, level follow the NAL header byte */
        c[4] = 0xFF;               /* 4-byte NAL lengths */
        c[5] = 0xE1;               /* one SPS */
        write_u16_be(c + 6, (uint16_t)tr->sps_len);
        uint32_t o = 8 + tr->sps_len;
        c[o++] = 1;                /* one PPS */
        write_u16_be(c + o, (uint16_t)n);
        memcpy(c + o + 2, nal, n);
        tr->codec_data_len = o + 2 + n;
    }
}

/* Validates the access unit and reports its length-prefixed size. */
static faam_status annexb_scan(faam_muxer_track *tr, const uint8_t *buf, uint32_t n, uint32_t *out_bytes)
{
    uint64_t total = 0;
    uint32_t pos = 0, start, len;
    int r;
    while ((r = annexb_next(buf, n, &pos, &start, &len)) > 0) {
        if (!len) continue;
        total += 4 + (uint64_t)len;
        if (tr->cfg.codec_id == FAAM_CODEC_H264) capture_avcc(tr, buf + start, len);
    }
    if (r < 0 || !total || total > UINT32_MAX) return FAAM_ERR_INVALID_ARG;
    *out_bytes = (uint32_t)total;
    return FAAM_OK;
}

/* Small NAL units share a staging block with their length so they cost one
 * write; large ones go straight to the output. */
static void write_annexb(faam_muxer *m, const uint8_t *buf, uint32_t n)
{
    uint32_t pos = 0, start, len;
    while (!m->error && annexb_next(buf, n, &pos, &start, &len) > 0) {
        if (!len) continue;
        put_u32(m, len);
        if (len > sizeof(m->staging)) {
            flush_staging(m);
            if (!m->error && m->io.write(m->io.user_data, buf + start, len) != (int32_t)len)
                m->error = FAAM_ERR_IO_WRITE;
        } else {
            mem_write(m, buf + start, len);
        }
    }
    flush_staging(m);
}
#endif

#ifdef FAAM_MUXER_FRAGMENTED
/* [*start, *end) is the next stretch of consecutive index entries of track t at or after *from. */
static bool next_run(const faam_muxer *m, uint32_t t, uint32_t *from, uint32_t *start, uint32_t *end)
{
    uint32_t i = *from;
    while (i < m->frag_count && m->fi_track[i] != t) i++;
    if (i == m->frag_count) return false;
    uint32_t j = i;
    while (j < m->frag_count && m->fi_track[j] == t) j++;
    *start = i; *end = j; *from = j;
    return true;
}

static uint32_t trun_size(const faam_muxer *m, uint32_t s, uint32_t e, bool *cts, bool *negative)
{
    *cts = *negative = false;
    for (uint32_t i = s; m->fi_cts && i < e; i++) {
        if (m->fi_cts[i]) *cts = true;
        if (m->fi_cts[i] < 0) *negative = true;
    }
    return 20 + (e - s) * (*cts ? 16 : 12);
}

static uint32_t traf_size(const faam_muxer *m, uint32_t t)
{
    uint32_t from = 0, s, e, size = 0;
    bool cts, negative;
    while (next_run(m, t, &from, &s, &e)) size += trun_size(m, s, e, &cts, &negative);
    return size ? size + 44 : 0;
}

/* Samples of different tracks interleave in mdat, so a track's samples form one
 * trun per consecutive run, all inside that track's single traf. */
static void write_moof(faam_muxer *m, uint32_t moof_bytes)
{
    put_u32(m, moof_bytes); put_data(m, "moof", 4);
    put_u32(m, 16); put_data(m, "mfhd", 4); put_u32(m, 0); put_u32(m, m->frag_seq + 1);
    for (uint32_t t = 0; t < m->num_tracks; t++) {
        uint32_t traf = traf_size(m, t);
        if (!traf) continue;
        const faam_muxer_track *tr = &m->tracks[t];
        put_u32(m, traf); put_data(m, "traf", 4);
        put_u32(m, 16); put_data(m, "tfhd", 4); put_u32(m, TFHD_DEFAULT_BASE_IS_MOOF); put_u32(m, tr->cfg.track_id);
        put_u32(m, 20); put_data(m, "tfdt", 4); put_u32(m, 1U << 24); put_u64(m, tr->frag_dts0);
        uint32_t from = 0, s, e;
        bool cts, negative;
        while (next_run(m, t, &from, &s, &e)) {
            put_u32(m, trun_size(m, s, e, &cts, &negative)); put_data(m, "trun", 4);
            put_u32(m, (negative ? 1U << 24 : 0) | TRUN_FLAGS | (cts ? TRUN_CTS : 0));
            put_u32(m, e - s);
            put_u32(m, m->frag_reserve + 8 + m->fi_off[s]); /* from the moof start to this run */
            for (uint32_t i = s; i < e; i++) {
                put_u32(m, m->fi_dur[i]);
                put_u32(m, (i + 1 < m->frag_count ? m->fi_off[i + 1] : m->frag_bytes) - m->fi_off[i]);
                put_u32(m, m->fi_flags[i] & 1 ? SAMPLE_FLAGS_SYNC : SAMPLE_FLAGS_NON_SYNC);
                if (cts) put_u32(m, (uint32_t)m->fi_cts[i]);
            }
        }
    }
}

static void open_fragment(faam_muxer *m)
{
    uint64_t pos = m->io.tell(m->io.user_data);
    put_u32(m, m->frag_reserve); put_data(m, "free", 4);
    flush_staging(m);
    uint32_t left = m->frag_reserve - 8;
    memset(m->staging, 0, sizeof(m->staging));
    while (left && !m->error) {
        uint32_t n = left < sizeof(m->staging) ? left : (uint32_t)sizeof(m->staging);
        if (m->io.write(m->io.user_data, m->staging, n) != (int32_t)n) m->error = FAAM_ERR_IO_WRITE;
        left -= n;
    }
    put_u32(m, 0); put_data(m, "mdat", 4); /* size patched when the fragment closes */
    flush_staging(m);
    if (m->error) return;
    m->frag_moof_pos = pos;
    m->frag_open = true;
    m->frag_count = m->frag_bytes = 0;
    for (uint32_t t = 0; t < m->num_tracks; t++) m->tracks[t].frag_dts0 = m->tracks[t].bitrate_window.samples;
}

/* Completes the open fragment in the order that keeps earlier ones intact: moof
 * and filler into the reserved space, then the mdat size, then back to the end. */
static void close_fragment(faam_muxer *m)
{
    if (!m->frag_open || !m->frag_count) return;
    uint32_t moof = 24;
    for (uint32_t t = 0; t < m->num_tracks; t++) moof += traf_size(m, t);
    uint64_t mdat_pos = m->frag_moof_pos + m->frag_reserve;
    uint64_t end = mdat_pos + 8 + m->frag_bytes;
    if (!m->io.seek(m->io.user_data, m->frag_moof_pos)) { m->error = FAAM_ERR_UNSUPPORTED; return; }
    write_moof(m, moof);
    put_u32(m, m->frag_reserve - moof); put_data(m, "free", 4);
    flush_staging(m);
    if (m->error) return;
    if (!m->io.seek(m->io.user_data, mdat_pos)) { m->error = FAAM_ERR_UNSUPPORTED; return; }
    put_u32(m, m->frag_bytes + 8);
    flush_staging(m);
    if (m->error) return;
    if (!m->io.seek(m->io.user_data, end)) { m->error = FAAM_ERR_UNSUPPORTED; return; }
    if (m->io.flush && !m->io.flush(m->io.user_data)) { m->error = FAAM_ERR_IO_WRITE; return; }
    m->frag_open = false;
    m->frag_seq++;
}

/* A video keyframe is where a fragment can start independently, so video drives the
 * cut; without video the audio clock does. A full index or byte budget cuts regardless. */
static bool frag_should_close(const faam_muxer *m, const faam_muxer_track *tr, uint32_t bytes, bool keyframe)
{
    if (m->frag_count >= m->frag_cap || bytes > (uint32_t)FRAG_MAX_BYTES - m->frag_bytes) return true;
    if (tr->cfg.track_type == FAAM_TRACK_VIDEO ? !keyframe : m->has_video) return false;
    return (tr->bitrate_window.samples - tr->frag_dts0) * 1000 >= (uint64_t)m->fragment_ms * track_timescale(tr);
}
#endif /* FAAM_MUXER_FRAGMENTED */

faam_status faam_muxer_write_frame(faam_muxer *m, uint32_t track_id, const uint8_t *frame_buf, uint32_t frame_bytes, uint32_t duration_ticks, int32_t cts_offset, bool is_keyframe)
{
    if (m && m->error) return m->error;
    if (!m || m->finalized || !frame_buf || frame_bytes == 0) return FAAM_ERR_INVALID_ARG;

    faam_muxer_track *tr = NULL;
    for (uint32_t t = 0; t < m->num_tracks; t++) {
        if (m->tracks[t].cfg.track_id == track_id) {
            tr = &m->tracks[t];
            break;
        }
    }
    if (!tr) return FAAM_ERR_NO_TRACK;
    if (cts_offset != 0) {
#ifdef FAAM_MUXER_VIDEO
        if (tr->cfg.track_type != FAAM_TRACK_VIDEO) return FAAM_ERR_INVALID_ARG;
#else
        return FAAM_ERR_UNSUPPORTED;
#endif
    }

    /* Annex-B input shrinks or grows once length-prefixed; every size below is the stored one. */
    uint32_t out_bytes = frame_bytes;
#ifdef FAAM_MUXER_VIDEO
    bool annexb = tr->cfg.flags & FAAM_TRACK_ANNEXB;
    if (annexb) {
        faam_status st = annexb_scan(tr, frame_buf, frame_bytes, &out_bytes);
        if (st != FAAM_OK) return st;
    }
#endif

#ifdef FAAM_MUXER_FRAGMENTED
    if (m->fragmented) {
        if (out_bytes > FRAG_MAX_BYTES) return FAAM_ERR_INVALID_ARG;
        if (m->frag_open && frag_should_close(m, tr, out_bytes, is_keyframe)) close_fragment(m);
        if (!m->error && !m->frag_open) open_fragment(m);
        if (m->error) return m->error;
    }
#endif

    if (m->io.write) {
#ifdef FAAM_MUXER_VIDEO
        if (annexb) {
            write_annexb(m, frame_buf, frame_bytes);
            if (m->error) return m->error;
        } else
#endif
        if (m->io.write(m->io.user_data, frame_buf, frame_bytes) != (int32_t)frame_bytes) return m->error = FAAM_ERR_IO_WRITE;
    }

    m->mdat_size += out_bytes;
    uint64_t dts = tr->bitrate_window.samples;
    tr->bitrate_window.samples += duration_ticks;

#ifdef FAAM_MUXER_FRAGMENTED
    if (m->fragmented) {
        uint32_t i = m->frag_count++;
        m->fi_off[i] = m->frag_bytes;
        m->fi_dur[i] = duration_ticks;
        if (m->fi_cts) m->fi_cts[i] = cts_offset;
        m->fi_flags[i] = tr->cfg.track_type == FAAM_TRACK_VIDEO ? is_keyframe : 1;
        m->fi_track[i] = (uint8_t)(tr - m->tracks);
        m->frag_bytes += out_bytes;
    } else
#endif
    {
        if (tr->sample_count >= tr->sample_capacity) {
            uint32_t new_cap = tr->sample_capacity * 2;
            if (new_cap < tr->sample_capacity || new_cap > UINT32_MAX / sizeof(uint64_t))
                return m->error = FAAM_ERR_INSUFFICIENT_MEM;
            uint32_t *tmp = (uint32_t *)ReallocMemory(tr->sample_sizes, (size_t)new_cap * sizeof(uint32_t));
            if (!tmp) return m->error = FAAM_ERR_INSUFFICIENT_MEM;
            tr->sample_sizes = tmp;
            if (m->num_tracks > 1) {
                uint64_t *offsets = (uint64_t *)ReallocMemory(tr->sample_offsets, (size_t)new_cap * sizeof(uint64_t));
                if (!offsets) return m->error = FAAM_ERR_INSUFFICIENT_MEM;
                tr->sample_offsets = offsets;
            }
            tr->sample_capacity = new_cap;
        }

        if (tr->sample_offsets)
            tr->sample_offsets[tr->sample_count] = m->mdat_pos + m->mdat_size - out_bytes;
        tr->sample_sizes[tr->sample_count] = out_bytes;
    }
    tr->total_bytes += out_bytes;
    /* Short drain frames do not contribute to a full-length bitrate window. */
    if (tr->last_frame_samples <= duration_ticks) {
        tr->bitrate_window.size += out_bytes;
        tr->window_ticks += duration_ticks;
        uint32_t ts = tr->cfg.timescale ? tr->cfg.timescale : 44100;
        if (tr->window_ticks >= ts) {
            uint32_t rate = (uint32_t)(8 * tr->bitrate_window.size * ts / tr->window_ticks);
            if (tr->bitrate_window.max < rate) tr->bitrate_window.max = rate;
            tr->bitrate_window.size = tr->window_ticks = 0;
        }
        tr->last_frame_samples = duration_ticks;
    }
    if (tr->max_frame_size < out_bytes) tr->max_frame_size = out_bytes;
    if (FRAGMENTED(m)) {
        tr->sample_count++;
        return FAAM_OK;
    }
#ifdef FAAM_MUXER_VIDEO
    if (tr->cfg.track_type == FAAM_TRACK_VIDEO) {
        int64_t pts = (int64_t)dts + cts_offset;
        if (!tr->sample_count || pts < tr->min_pts) tr->min_pts = pts;
        if (cts_offset < 0) tr->cts_negative = true;
        if (!add_ctts(m, tr, cts_offset)) return m->error;
    }
#else
    (void)dts;
#endif
    tr->sample_count++;

#ifdef FAAM_MUXER_VIDEO
    if (is_keyframe && tr->cfg.track_type == FAAM_TRACK_VIDEO) {
        if (tr->stss_count >= tr->stss_capacity) {
            uint32_t new_cap = tr->stss_capacity * 2;
            uint32_t *tmp = (uint32_t *)ReallocMemory(tr->stss_entries, new_cap * sizeof(uint32_t));
            if (!tmp) return m->error = FAAM_ERR_INSUFFICIENT_MEM;
            tr->stss_entries = tmp;
            tr->stss_capacity = new_cap;
        }
        tr->stss_entries[tr->stss_count++] = tr->sample_count; /* 1-based index */
    }
#else
    (void)is_keyframe;
#endif

    if (tr->stts_count > 0 && tr->stts_entries[tr->stts_count - 1].delta == duration_ticks) {
        tr->stts_entries[tr->stts_count - 1].count++;
    } else {
        if (tr->stts_count >= tr->stts_capacity) {
            uint32_t new_cap = tr->stts_capacity * 2;
            faam_stts_entry *tmp = (faam_stts_entry *)ReallocMemory(tr->stts_entries, new_cap * sizeof(faam_stts_entry));
            if (!tmp) return m->error = FAAM_ERR_INSUFFICIENT_MEM;
            tr->stts_entries = tmp;
            tr->stts_capacity = new_cap;
        }
        tr->stts_entries[tr->stts_count].count = 1;
        tr->stts_entries[tr->stts_count].delta = duration_ticks;
        tr->stts_count++;
    }

    return FAAM_OK;
}

/* Exact while the numerator fits 64 bits; beyond that the recording is long
 * enough that dividing first costs well under 1 % of the rate. */
static uint32_t average_bitrate(uint64_t bytes, uint64_t ticks, uint32_t ts) {
    if (!ticks) return 0;
    uint64_t scale = (uint64_t)ts * 8;
    uint64_t rate = bytes <= UINT64_MAX / scale ? bytes * scale / ticks
                                                : bytes / ticks * scale;
    return rate > UINT32_MAX ? UINT32_MAX : (uint32_t)rate;
}

static void update_bitrates(faam_muxer_track *tr) {
    uint32_t ts = tr->cfg.timescale ? tr->cfg.timescale : 44100;
    tr->avg_bitrate = average_bitrate(tr->total_bytes, tr->bitrate_window.samples, ts);
    tr->max_bitrate = tr->bitrate_window.max ? tr->bitrate_window.max : tr->avg_bitrate;
}

/* The audio rate keeps the gapless edit sample-exact; milliseconds would
 * truncate segment_duration. */
static uint32_t movie_timescale_of(const faam_muxer *m)
{
    for (uint32_t t = 0; t < m->num_tracks; t++)
        if (m->tracks[t].cfg.track_type == FAAM_TRACK_AUDIO && m->tracks[t].cfg.timescale)
            return m->tracks[t].cfg.timescale;
    return 1000;
}

/* Progressive files write moov once the sample tables are complete; fragmented
 * ones write it up front with empty tables and mvex, so durations stay 0 there. */
static void write_moov(faam_muxer *m, bool frag)
{
    uint32_t movie_timescale = movie_timescale_of(m);
    uint64_t max_movie_dur = 0;

    uint64_t moov = start_atom(m, "moov");
    uint64_t mvhd = start_atom(m, "mvhd");
    uint32_t now = m->cfg.creation_time ? m->cfg.creation_time + MP4_EPOCH_OFFSET : 0;

    for (uint32_t t = 0; t < m->num_tracks; t++) {
        faam_muxer_track *tr = &m->tracks[t];
        update_bitrates(tr);
        if (m->cfg.constant_rate) tr->max_bitrate = tr->avg_bitrate;
        uint32_t ts = track_timescale(tr);
        uint64_t dur_mv = frag ? 0 : (presentation_samples(m, tr) * (uint64_t)movie_timescale) / ts;
        if (dur_mv > max_movie_dur) max_movie_dur = dur_mv;
    }

    bool use64_time = (max_movie_dur > 0xFFFFFFFFULL);

    put_u32(m, use64_time ? (1U << 24) : 0);
    put_time(m, now, use64_time); put_time(m, now, use64_time);
    put_u32(m, movie_timescale); put_time(m, max_movie_dur, use64_time);
    put_u32(m, MP4_FP1616_ONE); put_u16(m, MP4_FP0808_ONE); put_u16(m, 0); put_u32(m, 0); put_u32(m, 0);
    put_u32(m, MP4_FP1616_ONE); put_u32(m, 0); put_u32(m, 0);
    put_u32(m, 0); put_u32(m, MP4_FP1616_ONE); put_u32(m, 0);
    put_u32(m, 0); put_u32(m, 0); put_u32(m, MP4_FP0230_ONE);
    put_u32(m, 0); put_u32(m, 0); put_u32(m, 0); put_u32(m, 0); put_u32(m, 0); put_u32(m, 0);
    uint32_t max_id = 0;
    for (uint32_t t = 0; t < m->num_tracks; t++)
        if (m->tracks[t].cfg.track_id > max_id) max_id = m->tracks[t].cfg.track_id;
    put_u32(m, max_id + 1);
    end_atom(m, mvhd);

    for (uint32_t t = 0; t < m->num_tracks; t++) {
        faam_muxer_track *tr = &m->tracks[t];
        uint32_t ts = track_timescale(tr);
        uint64_t track_dur_mv = frag ? 0 : (presentation_samples(m, tr) * (uint64_t)movie_timescale) / ts;

        uint64_t trak = start_atom(m, "trak");
        uint64_t tkhd = start_atom(m, "tkhd");
        put_u32(m, (use64_time ? (1U << 24) : 0) | 1);
        put_time(m, now, use64_time); put_time(m, now, use64_time);
        put_u32(m, tr->cfg.track_id); put_u32(m, 0);
        put_time(m, track_dur_mv, use64_time);
        put_u32(m, 0); put_u32(m, 0);
        put_u16(m, 0); put_u16(m, 0); put_u16(m, tr->cfg.track_type == FAAM_TRACK_AUDIO ? MP4_FP0808_ONE : 0); put_u16(m, 0);
        put_u32(m, MP4_FP1616_ONE); put_u32(m, 0); put_u32(m, 0);
        put_u32(m, 0); put_u32(m, MP4_FP1616_ONE); put_u32(m, 0);
        put_u32(m, 0); put_u32(m, 0); put_u32(m, MP4_FP0230_ONE);
        put_u32(m, (uint32_t)tr->cfg.width << 16); put_u32(m, (uint32_t)tr->cfg.height << 16);
        end_atom(m, tkhd);

        bool edit = false;
        uint64_t edit_duration = 0, edit_start = 0;
        /* A fragmented file can only describe the edit when its length is known up front. */
        if (tr->cfg.track_type == FAAM_TRACK_AUDIO && m->cfg.gapless.encoder_delay > 0 &&
            (!frag || m->cfg.gapless.total_samples)) {
            edit = true;
            edit_duration = m->cfg.gapless.total_samples;
            edit_start = m->cfg.gapless.encoder_delay;
        }
#ifdef FAAM_MUXER_VIDEO
        else if (edit_media_time(tr)) {
            edit = true;
            edit_duration = track_dur_mv;
            edit_start = edit_media_time(tr);
        }
#endif
        if (edit) {
            uint64_t edts = start_atom(m, "edts");
            uint64_t elst = start_atom(m, "elst");
            put_u32(m, use64_time ? (1U << 24) : 0);
            put_u32(m, 1);
            put_time(m, edit_duration, use64_time);
            put_time(m, edit_start, use64_time);
            put_u16(m, 1); put_u16(m, 0);
            end_atom(m, elst);
            end_atom(m, edts);
        }

        uint64_t mdia = start_atom(m, "mdia");
        uint64_t mdhd = start_atom(m, "mdhd");
        put_u32(m, use64_time ? (1U << 24) : 0);
        put_time(m, now, use64_time); put_time(m, now, use64_time);
        put_u32(m, ts); put_time(m, frag ? 0 : tr->bitrate_window.samples, use64_time);
        put_u16(m, pack_language(tr->cfg.language)); put_u16(m, 0);
        end_atom(m, mdhd);

        uint64_t hdlr = start_atom(m, "hdlr");
        put_u32(m, 0); put_u32(m, 0);
#ifdef FAAM_MUXER_VIDEO
        if (tr->cfg.track_type == FAAM_TRACK_VIDEO) put_data(m, "vide", 4);
        else
#endif
        put_data(m, "soun", 4);
        put_u32(m, 0); put_u32(m, 0); put_u32(m, 0); put_u8(m, 0);
        end_atom(m, hdlr);

        uint64_t minf = start_atom(m, "minf");
#ifdef FAAM_MUXER_VIDEO
        if (tr->cfg.track_type == FAAM_TRACK_VIDEO) {
            uint64_t vmhd = start_atom(m, "vmhd");
            put_u32(m, 1); put_u16(m, 0); put_u16(m, 0); put_u16(m, 0); put_u16(m, 0);
            end_atom(m, vmhd);
        } else
#endif
        {
            uint64_t smhd = start_atom(m, "smhd");
            put_u32(m, 0); put_u16(m, 0); put_u16(m, 0);
            end_atom(m, smhd);
        }

        uint64_t dinf = start_atom(m, "dinf");
        uint64_t dref = start_atom(m, "dref");
        put_u32(m, 0); put_u32(m, 1);
        uint64_t url = start_atom(m, "url ");
        put_u32(m, MP4_URL_SELF_CONTAINED);
        end_atom(m, url);
        end_atom(m, dref);
        end_atom(m, dinf);

        uint64_t stbl = start_atom(m, "stbl");
        uint64_t stsd = start_atom(m, "stsd");
        put_u32(m, 0); put_u32(m, 1);

#ifdef FAAM_MUXER_VIDEO
        if (tr->cfg.track_type == FAAM_TRACK_VIDEO) {
            const char *v_tag = (tr->cfg.codec_id == FAAM_CODEC_H265) ? "hvc1" : "avc1";
            const char *cfg_tag = (tr->cfg.codec_id == FAAM_CODEC_H265) ? "hvcC" : "avcC";
            uint64_t v_box = start_atom(m, v_tag);
            put_u8(m, 0); put_u8(m, 0); put_u8(m, 0); put_u8(m, 0); put_u8(m, 0); put_u8(m, 0);
            put_u16(m, 1); put_u16(m, 0); put_u16(m, 0);
            put_u32(m, 0); put_u32(m, 0); put_u32(m, 0);
            put_u16(m, tr->cfg.width ? tr->cfg.width : 1920);
            put_u16(m, tr->cfg.height ? tr->cfg.height : 1080);
            put_u32(m, 0x00480000); put_u32(m, 0x00480000);
            put_u32(m, 0); put_u16(m, 1);
            put_u8(m, 0); put_data(m, "FAAM Video", 10);
            for (int k = 10; k < 31; k++) put_u8(m, 0);
            put_u16(m, 0x0018); put_u16(m, 0xFFFF);

            if (tr->codec_data_len > 0) {
                uint64_t c_box = start_atom(m, cfg_tag);
                put_data(m, tr->codec_data, tr->codec_data_len);
                end_atom(m, c_box);
            }
            end_atom(m, v_box);
        } else
#endif
        {
            uint64_t mp4a = start_atom(m, "mp4a");
            put_u8(m, 0); put_u8(m, 0); put_u8(m, 0); put_u8(m, 0); put_u8(m, 0); put_u8(m, 0);
            put_u16(m, 1); put_u32(m, 0); put_u32(m, 0);
            put_u16(m, (uint16_t)(tr->cfg.channels ? tr->cfg.channels : 2));
            put_u16(m, tr->audio_sample_size);
            put_u16(m, 0); put_u16(m, 0);
            put_u16(m, (uint16_t)(ts > UINT16_MAX ? UINT16_MAX : ts));
            put_u16(m, 0);

            uint64_t esds = start_atom(m, "esds");
            put_u32(m, 0);
            put_descriptor(m, 3, 3 + MP4_DESC_HDR + 13 + MP4_DESC_HDR + tr->codec_data_len + MP4_DESC_HDR + 1);
            put_u16(m, 0); put_u8(m, 0);
            put_descriptor(m, 4, 13 + MP4_DESC_HDR + tr->codec_data_len);
            put_u8(m, MP4_OBJECT_TYPE_AUDIO_ISO_14496_3); put_u8(m, MP4_STREAM_TYPE_AUDIO);
            uint32_t bufferSizeDB = MP4_DECODER_BUFFER_BYTES_PER_CH * (tr->cfg.channels ? tr->cfg.channels : 2);
            put_u8(m, (uint8_t)(bufferSizeDB >> 16));
            put_u8(m, (uint8_t)(bufferSizeDB >> 8));
            put_u8(m, (uint8_t)(bufferSizeDB & 0xff));
            put_u32(m, tr->max_bitrate); put_u32(m, tr->avg_bitrate);
            put_descriptor(m, 5, tr->codec_data_len);
            put_data(m, tr->codec_data, tr->codec_data_len);
            put_descriptor(m, 6, 1); put_u8(m, 2);
            end_atom(m, esds);
            end_atom(m, mp4a);
        }
        end_atom(m, stsd);

        uint64_t stts = start_atom(m, "stts");
        put_u32(m, 0); put_u32(m, tr->stts_count);
        for (uint32_t i = 0; i < tr->stts_count; i++) {
            put_u32(m, tr->stts_entries[i].count);
            put_u32(m, tr->stts_entries[i].delta);
        }
        end_atom(m, stts);

#ifdef FAAM_MUXER_VIDEO
        if (tr->stss_count > 0) {
            uint64_t stss = start_atom(m, "stss");
            put_u32(m, 0); put_u32(m, tr->stss_count);
            for (uint32_t i = 0; i < tr->stss_count; i++) {
                put_u32(m, tr->stss_entries[i]);
            }
            end_atom(m, stss);
        }
        if (tr->ctts_entries) {
            uint64_t ctts = start_atom(m, "ctts");
            put_u32(m, tr->cts_negative ? 1U << 24 : 0); put_u32(m, tr->ctts_count);
            for (uint32_t i = 0; i < tr->ctts_count; i++) {
                put_u32(m, tr->ctts_entries[i].count);
                put_u32(m, (uint32_t)tr->ctts_entries[i].offset);
            }
            end_atom(m, ctts);
        }
#endif

        uint64_t stsc = start_atom(m, "stsc");
        put_u32(m, 0); put_u32(m, frag ? 0 : 1); /* ver/flags (0), entry count */
        if (!frag) { put_u32(m, 1); put_u32(m, m->num_tracks == 1 ? tr->sample_count : 1); put_u32(m, 1); }
        end_atom(m, stsc);

        uint64_t stsz = start_atom(m, "stsz");
        put_u32(m, 0); put_u32(m, 0); put_u32(m, tr->sample_count);
        for (uint32_t i = 0; i < tr->sample_count; i++) {
            put_u32(m, tr->sample_sizes[i]);
        }
        end_atom(m, stsz);

        uint32_t chunks = frag ? 0 : m->num_tracks == 1 ? 1 : tr->sample_count;
        bool use64_stco = m->num_tracks == 1 && m->mdat_pos + m->mdat_size > UINT32_MAX;
        for (uint32_t i = 0; i < tr->sample_count; i++) {
            if ((tr->sample_offsets ? tr->sample_offsets[i] : m->mdat_pos) > 0xFFFFFFFFULL) {
                use64_stco = true;
                break;
            }
        }

        if (!use64_stco) {
            uint64_t stco = start_atom(m, "stco");
            put_u32(m, 0); put_u32(m, chunks);
            for (uint32_t i = 0; i < chunks; i++) {
                put_u32(m, (uint32_t)(tr->sample_offsets ? tr->sample_offsets[i] : m->mdat_pos));
            }
            end_atom(m, stco);
        } else {
            uint64_t co64 = start_atom(m, "co64");
            put_u32(m, 0); put_u32(m, chunks);
            for (uint32_t i = 0; i < chunks; i++) {
                put_u64(m, tr->sample_offsets ? tr->sample_offsets[i] : m->mdat_pos);
            }
            end_atom(m, co64);
        }

        end_atom(m, stbl);
        end_atom(m, minf);
        end_atom(m, mdia);
        end_atom(m, trak);
    }

#ifdef FAAM_MUXER_FRAGMENTED
    if (frag) {
        uint64_t mvex = start_atom(m, "mvex");
        uint64_t mehd = start_atom(m, "mehd");
        put_u32(m, 1U << 24);
        m->mehd_pos = mehd + 12;
        put_u64(m, 0);
        end_atom(m, mehd);
        for (uint32_t t = 0; t < m->num_tracks; t++) {
            uint64_t trex = start_atom(m, "trex");
            put_u32(m, 0); put_u32(m, m->tracks[t].cfg.track_id);
            put_u32(m, 1); put_u32(m, 0); put_u32(m, 0); put_u32(m, 0);
            end_atom(m, trex);
        }
        end_atom(m, mvex);
    }
#endif

    uint64_t udta = start_atom(m, "udta");

    if (m->cfg.chapters && m->cfg.num_chapters > 0) {
        uint64_t chpl = start_atom(m, "chpl");
        put_u32(m, 1U << 24);
        put_u32(m, 0);
        put_u8(m, (uint8_t)m->cfg.num_chapters);
        for (uint32_t c = 0; c < m->cfg.num_chapters; c++) {
            put_u64(m, m->cfg.chapters[c].start_ms * 10000ULL);
            size_t tlen = strlen(m->cfg.chapters[c].title);
            if (tlen > 255) tlen = 255;
            put_u8(m, (uint8_t)tlen);
            put_data(m, m->cfg.chapters[c].title, tlen);
        }
        end_atom(m, chpl);
    }

    uint64_t meta = start_atom(m, "meta");
    put_u32(m, 0);
    uint64_t hdlr2 = start_atom(m, "hdlr");
    put_u32(m, 0); put_u32(m, 0); put_data(m, "mdirappl", 8);
    put_u32(m, 0); put_u32(m, 0); put_u8(m, 0);
    end_atom(m, hdlr2);

    uint32_t ilst_size;
    if (!m->error)
        m->error = faam_write_ilst(m->cfg.metadata, m->gapless_present ? &m->cfg.gapless : NULL,
                                  NULL, 0, write_ilst_bytes, m, &ilst_size);
    end_atom(m, meta);
    end_atom(m, udta);
    end_atom(m, moov);
}

faam_status faam_muxer_finalize(faam_muxer *m)
{
    if (!m) return FAAM_ERR_INVALID_ARG;
    if (m->error) return m->error;
    if (m->finalized) return FAAM_OK;
    if (!m->io.seek || !m->io.tell) return m->error = FAAM_ERR_UNSUPPORTED;
    for (uint32_t t = 0; t < m->num_tracks; t++)
        if (m->tracks[t].cfg.track_type == FAAM_TRACK_VIDEO && !m->tracks[t].codec_data_len)
            return m->error = FAAM_ERR_INVALID_ARG; /* Annex-B stream never carried SPS and PPS */
    uint64_t pos = m->io.tell(m->io.user_data);
#ifdef FAAM_MUXER_FRAGMENTED
    if (m->fragmented) {
        close_fragment(m);
        if (m->error) return m->error;
        uint64_t movie_ticks = 0, end = m->io.tell(m->io.user_data);
        uint32_t movie_ts = movie_timescale_of(m);
        for (uint32_t t = 0; t < m->num_tracks; t++) {
            uint64_t dur = m->tracks[t].bitrate_window.samples * movie_ts / track_timescale(&m->tracks[t]);
            if (dur > movie_ticks) movie_ticks = dur;
        }
        uint8_t dur[8];
        write_u64_be(dur, movie_ticks);
        if (!m->io.seek(m->io.user_data, m->mehd_pos)) return m->error = FAAM_ERR_UNSUPPORTED;
        if (m->io.write(m->io.user_data, dur, 8) != 8) return m->error = FAAM_ERR_IO_WRITE;
        if (!m->io.seek(m->io.user_data, end)) return m->error = FAAM_ERR_UNSUPPORTED;
        if (m->io.flush && !m->io.flush(m->io.user_data)) return m->error = FAAM_ERR_IO_WRITE;
        m->file_bytes = end;
        m->finalized = true;
        return FAAM_OK;
    }
#endif
    bool large = m->mdat_size > UINT32_MAX - 8ULL;
    uint8_t header[16];
    write_u32_be(header, large ? 1 : (uint32_t)(m->mdat_size + 8));
    memcpy(header + 4, "mdat", 4);
    if (large) write_u64_be(header + 8, m->mdat_size + 16);
    if (!m->io.seek(m->io.user_data, m->mdat_pos - (large ? 16 : 8)))
        return m->error = FAAM_ERR_UNSUPPORTED;
    uint32_t bytes = large ? 16 : 4;
    if (m->io.write(m->io.user_data, header, bytes) != (int32_t)bytes)
        return m->error = FAAM_ERR_IO_WRITE;
    if (!m->io.seek(m->io.user_data, pos)) return m->error = FAAM_ERR_UNSUPPORTED;

    write_moov(m, false);

    flush_staging(m);
    if (!m->error && m->io.flush && !m->io.flush(m->io.user_data)) m->error = FAAM_ERR_IO_WRITE;
    if (!m->error) {
        m->file_bytes = m->io.tell(m->io.user_data);
        m->finalized = true;
    }
    return m->error;
}

void faam_muxer_close(faam_muxer **handle)
{
    if (!handle || !*handle) return;
    faam_muxer *m = *handle;
    *handle = NULL;
    for (uint32_t t = 0; t < m->num_tracks; t++) {
        FreeMemory(m->tracks[t].sample_sizes);
        FreeMemory(m->tracks[t].sample_offsets);
        if (m->tracks[t].stts_entries) FreeMemoryFast(m->tracks[t].stts_entries);
        if (m->tracks[t].stss_entries) FreeMemoryFast(m->tracks[t].stss_entries);
        FreeMemory(m->tracks[t].ctts_entries);
    }
    if (m->heap_owned) FreeMemory(m);
}

faam_status faam_muxer_open(const faam_muxer_config *cfg, const faam_io *io, faam_muxer **out_muxer) {
    if (!out_muxer) return FAAM_ERR_INVALID_ARG;
    *out_muxer = NULL;
    uint32_t size;
    faam_status st = faam_muxer_get_state_size(cfg, &size);
    if (st != FAAM_OK) return st;
    void *mem = AllocMemory(size);
    if (!mem) return FAAM_ERR_INSUFFICIENT_MEM;
    st = faam_muxer_init(mem, size, cfg, io, out_muxer);
    if (st != FAAM_OK) FreeMemory(mem);
    else (*out_muxer)->heap_owned = true;
    return st;
}

faam_status faam_muxer_get_info(const faam_muxer *m, uint32_t track_id, faam_muxer_info *out_info)
{
    if (!m || !out_info || out_info->struct_size < sizeof(*out_info)) return FAAM_ERR_INVALID_ARG;
    const faam_muxer_track *tr = NULL;
    for (uint32_t t = 0; t < m->num_tracks; t++)
        if (m->tracks[t].cfg.track_id == track_id) tr = &m->tracks[t];
    if (!tr) return FAAM_ERR_NO_TRACK;
    faam_muxer_track stats = *tr;
    update_bitrates(&stats);
    if (m->cfg.constant_rate) stats.max_bitrate = stats.avg_bitrate;
    out_info->struct_size = sizeof(*out_info);
    out_info->frame_count = tr->sample_count;
    out_info->duration_ticks = tr->bitrate_window.samples;
    out_info->file_bytes = m->finalized ? m->file_bytes : m->mdat_pos + m->mdat_size;
#ifdef FAAM_MUXER_FRAGMENTED
    if (m->fragmented && !m->finalized) out_info->file_bytes = m->io.tell(m->io.user_data);
#endif
    out_info->max_bitrate = stats.max_bitrate;
    out_info->avg_bitrate = stats.avg_bitrate;
    out_info->max_frame_size = tr->max_frame_size;
    return FAAM_OK;
}

#ifdef FAAM_EMBEDDED
faam_status faam_update_tags_stream(const faam_io *io, const faam_metadata *meta) {
    (void)io; (void)meta;
    return FAAM_ERR_UNSUPPORTED;
}

faam_status faam_update_chapters_stream(const faam_io *io, const faam_chapter *chapters, uint32_t count) {
    (void)io; (void)chapters; (void)count;
    return FAAM_ERR_UNSUPPORTED;
}
#endif
