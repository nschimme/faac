/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

#include "faam_engine.h"

static double progress_time(void)
{
#ifdef _WIN32
    LARGE_INTEGER count, frequency;
    if (QueryPerformanceFrequency(&frequency) && QueryPerformanceCounter(&count))
        return (double)count.QuadPart / (double)frequency.QuadPart;
#else
#ifdef CLOCK_MONOTONIC
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == 0)
        return (double)tv.tv_sec + (double)tv.tv_usec * 1e-6;
#endif
    return (double)clock() / CLOCKS_PER_SEC;
}

static bool report_progress(progress_callback_t progress, void *user, uint32_t frames,
                            double media_seconds, double start, bool final)
{
    if (!progress) return true;
    double elapsed = progress_time() - start;
    progress_info_t info = {0};
    info.current_frame = frames;
    info.time_elapsed_sec = elapsed;
    info.speed_factor = elapsed > 0.0 ? media_seconds / elapsed : 0.0;
    info.is_final = final;
    return progress(&info, user);
}

faam_status faam_engine_mux_adts_frames(faam_muxer *m, uint32_t track_id, FILE *f,
                                        const adts_params *first, const uint8_t *prefix,
                                        size_t prefix_len, uint32_t *frames, bool *multi_block,
                                        progress_callback_t progress, void *progress_user)
{
    static const uint32_t sample_rates[] = { 96000, 88200, 64000, 48000, 44100, 32000,
                                              24000, 22050, 16000, 12000, 11025, 8000, 7350 };
    uint32_t sample_rate = first->sr_idx < sizeof(sample_rates) / sizeof(sample_rates[0]) ?
                           sample_rates[first->sr_idx] : 0;
    double start = progress ? progress_time() : 0.0;
    uint8_t buf[65536];
    size_t buf_len = prefix_len;
    if (buf_len) memcpy(buf, prefix, buf_len);

    faam_status result = FAAM_OK;
    for (;;) {
        /* Read only enough for the next header or complete frame. A large fread
         * on a pipe waits for the whole request, delaying completed fragments
         * until the input closes or the buffer fills. */
        size_t read_target = 7;
        if (buf_len >= 7 && adts_header_ok(buf)) {
            adts_params p;
            adts_parse_header(buf, &p);
            uint32_t len = adts_frame_length(buf);
            uint32_t hlen = adts_header_length(buf);
            if (p.profile == first->profile && p.sr_idx == first->sr_idx &&
                p.channel_cfg == first->channel_cfg && p.raw_blocks == 0 && len >= hlen)
                read_target = len;
        }
        if (read_target > sizeof(buf)) { result = FAAM_ERR_INVALID_ARGUMENT; break; }
        bool attempted_read = read_target > buf_len;
        size_t n = attempted_read ? fread(buf + buf_len, 1, read_target - buf_len, f) : 0;
        buf_len += n;

        size_t off = 0;
        while (off + 7 <= buf_len) {
            const uint8_t *h = buf + off;
            adts_params p;
            if (!adts_header_ok(h)) { off++; continue; }
            adts_parse_header(h, &p);
            if (p.profile != first->profile || p.sr_idx != first->sr_idx || p.channel_cfg != first->channel_cfg) {
                off++;
                continue;
            }
            uint32_t len = adts_frame_length(h);
            uint32_t hlen = adts_header_length(h);
            if (len < hlen) { off++; continue; }
            if (p.raw_blocks != 0) { *multi_block = true; goto finished; }
            if (off + len > buf_len) break;
            result = faam_muxer_write_frame(m, track_id, h + hlen, len - hlen, 1024, 0, FAAM_FRAME_KEYFRAME);
            if (result != FAAM_OK) goto finished;
            (*frames)++;
            if (progress && !report_progress(progress, progress_user, *frames,
                                 sample_rate ? (double)*frames * 1024.0 / sample_rate : 0.0,
                                 start, false)) { result = (faam_status)FAAM_ENGINE_CANCELLED; goto finished; }
            off += len;
        }

        memmove(buf, buf + off, buf_len - off);
        buf_len -= off;
        if (ferror(f)) { result = FAAM_ERR_IO_READ; break; }
        if (n == 0 && attempted_read) break;
    }

finished:
    if (progress && result == FAAM_OK && !*multi_block)
        report_progress(progress, progress_user, *frames,
                        sample_rate ? (double)*frames * 1024.0 / sample_rate : 0.0,
                        start, true);
    return result;
}

/* One video access unit; durations follow the frame rate num/den exactly by
 * rounding the running timestamp, not each interval. */
static faam_status mux_video_frame(faam_muxer *m, uint32_t track_id, const uint8_t *data,
                                   uint32_t len, bool key, uint32_t *frames,
                                   uint32_t fps_num, uint32_t fps_den)
{
    uint64_t k = *frames;
    uint64_t from = (k * FAAM_VIDEO_TIMESCALE * fps_den + fps_num / 2) / fps_num;
    uint64_t to = ((k + 1) * FAAM_VIDEO_TIMESCALE * fps_den + fps_num / 2) / fps_num;
    faam_status st = faam_muxer_write_frame(m, track_id, data, len, (uint32_t)(to - from), 0,
                                            key ? FAAM_FRAME_KEYFRAME : 0);
    if (st == FAAM_OK) (*frames)++;
    return st;
}

typedef struct {
    faam_muxer *m;
    uint32_t track_id;
    uint32_t *frames;
    uint32_t fps_num;
    uint32_t fps_den;
    faam_status st;
    progress_callback_t progress;
    void *progress_user;
    double start;
} video_sink;

static bool video_sample(void *user, const uint8_t *sample, uint32_t len, bool key)
{
    video_sink *v = (video_sink *)user;
    v->st = mux_video_frame(v->m, v->track_id, sample, len, key, v->frames, v->fps_num, v->fps_den);
    if (v->st == FAAM_OK) {
        if (v->progress) {
            double media_seconds = v->fps_num ? (double)*v->frames * v->fps_den / v->fps_num : 0.0;
            if (!report_progress(v->progress, v->progress_user, *v->frames,
                                 media_seconds, v->start, false)) v->st = (faam_status)FAAM_ENGINE_CANCELLED;
        }
    }
    return v->st == FAAM_OK;
}

faam_status faam_engine_mux_video_frames(faam_muxer *m, uint32_t track_id, annexb_codec codec,
                                         const uint8_t *data, size_t len, uint32_t fps_num,
                                         uint32_t fps_den, uint32_t *frames,
                                         progress_callback_t progress, void *progress_user)
{
    double start = progress ? progress_time() : 0.0;
    video_sink sink = { m, track_id, frames, fps_num, fps_den, FAAM_OK, progress, progress_user,
                        start };
    annexb_status as = annexb_split(codec, data, len, video_sample, &sink);
    if (as == ANNEXB_NO_MEMORY) sink.st = FAAM_ERR_NO_MEMORY;
    if (as == ANNEXB_OK && sink.st == FAAM_OK)
        report_progress(progress, progress_user, *frames,
                        fps_num ? (double)*frames * fps_den / fps_num : 0.0,
                        start, true);
    return sink.st;
}

faam_status faam_engine_demux_track(faam_demuxer *d, const faam_track_info *track,
                                    faam_engine_demux_sample_fn sample, void *sample_user,
                                    progress_callback_t progress, void *progress_user)
{
    uint8_t *frame = (uint8_t *)malloc(65536);
    uint32_t frame_cap = 65536;
    uint32_t frame_bytes = 0;
    uint32_t frames = 0;
    uint64_t media_ticks = 0;
    double start = progress ? progress_time() : 0.0;
    if (!frame) return FAAM_ERR_NO_MEMORY;

    faam_status st = FAAM_OK;
    faam_frame_loc loc = { .struct_size = sizeof(loc) };
    while ((st = faam_demuxer_next_frame_loc(d, &loc)) == FAAM_OK) {
        if (loc.track_id != track->track_id) {
            /* Reading to NULL advances the demuxer without allocating for other tracks. */
            faam_demuxer_read_frame(d, NULL, 0, &frame_bytes);
            continue;
        }

        st = faam_demuxer_read_frame(d, frame, frame_cap, &frame_bytes);
        if (st == FAAM_ERR_OUTPUT_TOO_SMALL) {
            uint8_t *bigger = (uint8_t *)realloc(frame, frame_bytes);
            if (!bigger) { st = FAAM_ERR_NO_MEMORY; break; }
            frame = bigger;
            frame_cap = frame_bytes;
            st = faam_demuxer_read_frame(d, frame, frame_cap, &frame_bytes);
        }
        if (st != FAAM_OK) break;
        if (frame_bytes == 0) continue;
        if (!sample(sample_user, &loc, frame, frame_bytes)) {
            st = (faam_status)FAAM_ENGINE_CANCELLED;
            break;
        }

        frames++;
        media_ticks += loc.duration_ticks;
        if (progress) {
            double elapsed = progress_time() - start;
            double media_seconds = track->timescale ? (double)media_ticks / track->timescale : 0.0;
            progress_info_t info = {0};
            info.current_frame = frames;
            info.total_frames = track->total_frames;
            info.time_elapsed_sec = elapsed;
            info.speed_factor = elapsed > 0.0 ? media_seconds / elapsed : 0.0;
            if (track->track_type == FAAM_TRACK_AUDIO) {
                info.current_input_samples = media_ticks;
                info.total_input_samples = track->total_duration;
                info.sample_rate = track->timescale;
                info.num_channels = (uint16_t)track->channels;
            }
            if (!progress(&info, progress_user)) {
                st = (faam_status)FAAM_ENGINE_CANCELLED;
                break;
            }
        }
    }

    if (st == FAAM_END_OF_STREAM) {
        st = FAAM_OK;
        if (progress) {
            double elapsed = progress_time() - start;
            double media_seconds = track->timescale ? (double)media_ticks / track->timescale : 0.0;
            progress_info_t info = {0};
            info.current_frame = frames;
            info.total_frames = track->total_frames;
            info.time_elapsed_sec = elapsed;
            info.speed_factor = elapsed > 0.0 ? media_seconds / elapsed : 0.0;
            info.is_final = true;
            if (track->track_type == FAAM_TRACK_AUDIO) {
                info.current_input_samples = media_ticks;
                info.total_input_samples = track->total_duration;
                info.sample_rate = track->timescale;
                info.num_channels = (uint16_t)track->channels;
            }
            progress(&info, progress_user);
        }
    }

    free(frame);
    return st;
}
