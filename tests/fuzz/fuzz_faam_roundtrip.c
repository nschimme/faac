/*
 * libFuzzer harness for faam roundtrip:
 * fuzz-derived track config/frames/metadata/gapless -> faam muxer -> in-memory file -> demuxer;
 * assert frame count, sizes, bytes, gapless and metadata survive. A failed assertion must abort().
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "faam.h"

typedef struct {
    uint8_t *buf;
    size_t size;
    size_t cap;
    size_t pos;
} rw_mem_stream;

static int32_t rw_read(void *user_data, void *buf, uint32_t bytes_to_read)
{
    rw_mem_stream *s = (rw_mem_stream *)user_data;
    if (s->pos >= s->size) return 0;
    size_t rem = s->size - s->pos;
    uint32_t to_copy = (uint32_t)(rem < bytes_to_read ? rem : bytes_to_read);
    memcpy(buf, s->buf + s->pos, to_copy);
    s->pos += to_copy;
    return (int32_t)to_copy;
}

static int32_t rw_write(void *user_data, const void *buf, uint32_t bytes_to_write)
{
    rw_mem_stream *s = (rw_mem_stream *)user_data;
    if (s->pos + bytes_to_write > s->cap) {
        size_t new_cap = (s->pos + bytes_to_write) * 2 + 4096;
        if (new_cap > 10 * 1024 * 1024) return -1;
        uint8_t *p = realloc(s->buf, new_cap);
        if (!p) return -1;
        s->buf = p;
        s->cap = new_cap;
    }
    memcpy(s->buf + s->pos, buf, bytes_to_write);
    s->pos += bytes_to_write;
    if (s->pos > s->size) s->size = s->pos;
    return (int32_t)bytes_to_write;
}

static bool rw_seek(void *user_data, uint64_t offset)
{
    rw_mem_stream *s = (rw_mem_stream *)user_data;
    if (offset > 10 * 1024 * 1024) return false;
    s->pos = (size_t)offset;
    return true;
}

static uint64_t rw_tell(void *user_data)
{
    rw_mem_stream *s = (rw_mem_stream *)user_data;
    return (uint64_t)s->pos;
}

static bool rw_flush(void *user_data)
{
    (void)user_data;
    return true;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 16) return 0;

    uint8_t mode = data[0];
    uint8_t frame_count_target = (data[1] % 8) + 1;
    const uint8_t *payload = data + 2;
    size_t payload_len = size - 2;

    faam_muxer_config cfg;
    faam_muxer_config_init(&cfg, sizeof(cfg));

    cfg.fragment_ms = (mode & 1) ? 1000 : 0;
    cfg.flags = (mode & 2) ? FAAM_MUXER_M4B : 0;

    faam_gapless_info gap; gap.struct_size = sizeof(gap);
    gap.encoder_delay = 1024;
    gap.end_padding = 640;
    gap.total_samples = 240000;
    cfg.gapless = &gap;

    char title_str[16];
    snprintf(title_str, sizeof(title_str), "Title_%u", mode);
    faam_metadata meta;
    memset(&meta, 0, sizeof(meta)); meta.struct_size = sizeof(meta);
    meta.title = title_str;
    cfg.metadata = &meta;

    static const uint8_t asc_sample[2] = { 0x11, 0x90 }; /* 48kHz stereo LC */

    faam_track_config trk;
    memset(&trk, 0, sizeof(trk));
    trk.struct_size = sizeof(trk);
    trk.track_type = FAAM_TRACK_AUDIO;
    trk.codec_id = FAAM_CODEC_AAC;
    trk.timescale = 48000;
    trk.sample_rate = 48000;
    trk.channels = 2;
    trk.codec_data = asc_sample;
    trk.codec_data_len = sizeof(asc_sample);

    uint32_t track_id = 0;
    cfg.tracks = &trk;
    cfg.num_tracks = 1;

    rw_mem_stream ms;
    ms.cap = 16384;
    ms.buf = malloc(ms.cap);
    if (!ms.buf) return 0;
    ms.size = 0;
    ms.pos = 0;

    faam_io io = { sizeof(faam_io), &ms, rw_read, rw_write, rw_seek, rw_tell, rw_flush };

    faam_muxer *m = NULL;
    if (faam_muxer_open(&cfg, &io, &m) != FAAM_OK || !m) {
        free(ms.buf);
        return 0;
    }

    if (faam_muxer_get_track_id(m, 0, &track_id) != FAAM_OK) abort();

    uint32_t written_frames = 0;
    uint32_t frame_sizes[16];
    size_t frame_offsets_in_payload[16];

    size_t p_pos = 0;
    for (uint32_t i = 0; i < frame_count_target && p_pos < payload_len; i++) {
        uint32_t f_size = (payload[p_pos] % 128) + 1;
        p_pos++;
        if (p_pos + f_size > payload_len) f_size = (uint32_t)(payload_len - p_pos);
        if (f_size == 0) break;

        frame_sizes[i] = f_size;
        frame_offsets_in_payload[i] = p_pos;

        faam_status st = faam_muxer_write_frame(m, track_id, payload + p_pos, f_size, 1024, 0, FAAM_FRAME_KEYFRAME);
        if (st != FAAM_OK) break;

        p_pos += f_size;
        written_frames++;
    }

    if (written_frames == 0) {
        faam_muxer_close(&m);
        free(ms.buf);
        return 0;
    }

    if (faam_muxer_finalize(m) != FAAM_OK) {
        faam_muxer_close(&m);
        free(ms.buf);
        return 0;
    }

    faam_muxer_close(&m);

    /* Assert demuxing retrieves the frame count, sizes, bytes, gapless and metadata */
    ms.pos = 0;
    faam_demuxer *d = NULL;
    if (faam_demuxer_open(NULL, &io, &d) != FAAM_OK || !d) {
        abort();
    }

    faam_gapless_info demux_gap; demux_gap.struct_size = sizeof(demux_gap);
    if (faam_demuxer_get_track_gapless(d, 0, &demux_gap) == FAAM_OK) {
        if (demux_gap.encoder_delay != gap.encoder_delay || demux_gap.end_padding != gap.end_padding) {
            abort();
        }
    }

    faam_metadata demux_meta; demux_meta.struct_size = sizeof(demux_meta);
    if (faam_demuxer_get_metadata(d, &demux_meta) == FAAM_OK) {
        if (!demux_meta.title || strcmp(demux_meta.title, title_str) != 0) {
            abort();
        }
    }

    uint32_t demuxed_count = 0;
    faam_frame_loc loc; loc.struct_size = sizeof(loc);
    uint8_t read_buf[256];

    while (faam_demuxer_next_frame_loc(d, &loc) == FAAM_OK) {
        if (demuxed_count >= written_frames) {
            abort();
        }

        if (loc.frame_bytes != frame_sizes[demuxed_count]) {
            abort();
        }

        uint32_t read_bytes = 0;
        if (faam_demuxer_read_frame(d, read_buf, sizeof(read_buf), &read_bytes) != FAAM_OK) {
            abort();
        }

        if (read_bytes != loc.frame_bytes) {
            abort();
        }

        const uint8_t *orig = payload + frame_offsets_in_payload[demuxed_count];
        if (memcmp(read_buf, orig, read_bytes) != 0) {
            abort();
        }

        demuxed_count++;
    }

    if (demuxed_count != written_frames) {
        abort();
    }

    faam_demuxer_close(&d);
    free(ms.buf);
    return 0;
}
