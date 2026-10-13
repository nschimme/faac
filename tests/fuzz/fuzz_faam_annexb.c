/*
 * libFuzzer harness for frontend/annexb.c (annexb_build_config, annexb_split)
 * and FAAM_TRACK_ANNEXB path (when FAAM_MUXER_VIDEO is enabled).
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "annexb.h"
#include "faam.h"

typedef struct {
    uint32_t sample_count;
    uint32_t total_bytes;
} split_ctx;

static bool sample_cb(void *user, const uint8_t *sample, uint32_t len, bool key)
{
    (void)sample;
    (void)key;
    split_ctx *ctx = (split_ctx *)user;
    ctx->sample_count++;
    ctx->total_bytes += len;
    return ctx->sample_count < 256;
}

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
    if (size < 2) return 0;

    annexb_codec codec = (data[0] & 1) ? ANNEXB_H265 : ANNEXB_H264;
    const uint8_t *payload = data + 1;
    size_t payload_len = size - 1;

    /* 1. Test annexb_build_config */
    uint8_t cfg_buf[ANNEXB_CONFIG_MAX];
    uint32_t cfg_len = 0;
    (void)annexb_build_config(codec, payload, payload_len, cfg_buf, sizeof(cfg_buf), &cfg_len);

    /* 2. Test annexb_split */
    split_ctx ctx = { 0, 0 };
    (void)annexb_split(codec, payload, payload_len, sample_cb, &ctx);

#ifdef FAAM_MUXER_VIDEO
    /* 3. Test libfaam FAAM_TRACK_ANNEXB path */
    faam_muxer_config cfg;
    faam_muxer_config_init(&cfg, sizeof(cfg));

    faam_track_config trk;
    memset(&trk, 0, sizeof(trk));
    trk.struct_size = sizeof(trk);
    trk.track_type = FAAM_TRACK_VIDEO;
    trk.codec_id = codec == ANNEXB_H265 ? FAAM_CODEC_H265 : FAAM_CODEC_H264;
    trk.timescale = 90000;
    trk.width = 320;
    trk.height = 240;
    trk.flags = FAAM_TRACK_ANNEXB;

    if (codec == ANNEXB_H265) {
        if (cfg_len == 0) return 0;
        trk.codec_data = cfg_buf;
        trk.codec_data_len = cfg_len;
    } else {
        if (cfg_len > 0) {
            trk.codec_data = cfg_buf;
            trk.codec_data_len = cfg_len;
        }
    }

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
    if (faam_muxer_open(&cfg, &io, &m) == FAAM_OK && m) {
        if (faam_muxer_get_track_id(m, 0, &track_id) != FAAM_OK) abort();
        (void)faam_muxer_write_frame(m, track_id, payload, (uint32_t)(payload_len < 65536 ? payload_len : 65536), 3000, 0, FAAM_FRAME_KEYFRAME);
        (void)faam_muxer_finalize(m);
        faam_muxer_close(&m);
    }

    free(ms.buf);
#endif

    return 0;
}
