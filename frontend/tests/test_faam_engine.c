/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "../faam_engine.h"

static int32_t file_read(void *user, void *out, uint32_t bytes)
{
    return (int32_t)fread(out, 1, bytes, (FILE *)user);
}

static int32_t file_write(void *user, const void *data, uint32_t bytes)
{
    return (int32_t)fwrite(data, 1, bytes, (FILE *)user);
}

static bool file_seek(void *user, uint64_t offset)
{
    return offset <= LONG_MAX && fseek((FILE *)user, (long)offset, SEEK_SET) == 0;
}

static uint64_t file_tell(void *user)
{
    long pos = ftell((FILE *)user);
    return pos < 0 ? UINT64_MAX : (uint64_t)pos;
}

typedef struct {
    unsigned calls;
    unsigned frames;
    bool final;
} progress_state;

static bool progress_cb(const progress_info_t *info, void *user)
{
    progress_state *state = (progress_state *)user;
    state->calls++;
    state->frames = info->current_frame;
    state->final = info->is_final;
    return true;
}

typedef struct {
    uint8_t payloads[4];
    unsigned count;
    uint32_t track_id;
} sample_state;

static bool sample_cb(void *user, const faam_frame_loc *loc,
                      const uint8_t *frame, uint32_t frame_bytes)
{
    sample_state *state = (sample_state *)user;
    assert(loc->track_id == state->track_id);
    assert(frame_bytes == 1 && state->count < sizeof(state->payloads));
    state->payloads[state->count++] = frame[0];
    return true;
}

static size_t append_frame(uint8_t *out, size_t pos, adts_params params, uint8_t payload)
{
    uint8_t header[ADTS_HEADER_SIZE];
    adts_write_header(&params, 1, header);
    memcpy(out + pos, header, sizeof(header));
    out[pos + sizeof(header)] = payload;
    return pos + sizeof(header) + 1;
}

static void test_mux_and_demux_engine(void)
{
    static const uint8_t asc[] = { 0x12, 0x10 };
    adts_params lc = { .profile = 1, .sr_idx = 4, .channel_cfg = 2 };
    adts_params other_profile = { .profile = 0, .sr_idx = 4, .channel_cfg = 2 };
    uint8_t input_data[64];
    size_t input_len = 0;
    input_data[input_len++] = 0x12; /* Junk before the first sync word is skipped. */
    input_len = append_frame(input_data, input_len, lc, 0xA1);
    input_len = append_frame(input_data, input_len, other_profile, 0xB2); /* False sync/config. */
    input_len = append_frame(input_data, input_len, lc, 0xC3);

    FILE *input = tmpfile();
    FILE *output = tmpfile();
    assert(input && output);
    assert(fwrite(input_data, 1, input_len, input) == input_len);
    rewind(input);

    faam_track_config tracks[2] = {{
        .struct_size = sizeof(faam_track_config), .track_type = FAAM_TRACK_AUDIO,
        .codec_id = FAAM_CODEC_AAC, .timescale = 44100, .sample_rate = 44100,
        .channels = 2, .codec_data = asc, .codec_data_len = sizeof(asc)
    }, {
        .struct_size = sizeof(faam_track_config), .track_type = FAAM_TRACK_AUDIO,
        .codec_id = FAAM_CODEC_AAC, .timescale = 44100, .sample_rate = 44100,
        .channels = 2, .codec_data = asc, .codec_data_len = sizeof(asc)
    }};
    faam_muxer_config config;
    assert(faam_muxer_config_init(&config, sizeof(config)) == FAAM_OK);
    config.num_tracks = 2;
    config.tracks = tracks;

    faam_io output_io = { .struct_size = sizeof(output_io), .user_data = output,
                          .read = file_read, .write = file_write,
                          .seek = file_seek, .tell = file_tell };
    faam_muxer *muxer = NULL;
    assert(faam_muxer_open(&config, &output_io, &muxer) == FAAM_OK);
    uint32_t mux_track_id = 0;
    assert(faam_muxer_get_track_id(muxer, 0, &mux_track_id) == FAAM_OK);
    uint32_t other_track_id = 0;
    assert(faam_muxer_get_track_id(muxer, 1, &other_track_id) == FAAM_OK);

    uint32_t frames = 0;
    bool multi_block = false;
    progress_state mux_progress = {0};
    assert(faam_engine_mux_adts_frames(muxer, mux_track_id, input, &lc, NULL, 0,
                                       &frames, &multi_block, progress_cb,
                                       &mux_progress) == FAAM_OK);
    assert(frames == 2 && !multi_block);
    assert(mux_progress.calls == 3 && mux_progress.frames == 2 && mux_progress.final);
    const uint8_t other_payload = 0xE4;
    assert(faam_muxer_write_frame(muxer, other_track_id, &other_payload, 1,
                                  1024, 0, FAAM_FRAME_KEYFRAME) == FAAM_OK);
    assert(faam_muxer_finalize(muxer) == FAAM_OK);
    assert(faam_muxer_close(&muxer) == FAAM_OK);
    fclose(input);

    rewind(output);
    faam_io input_io = { .struct_size = sizeof(input_io), .user_data = output,
                         .read = file_read, .seek = file_seek, .tell = file_tell };
    faam_demuxer *demuxer = NULL;
    assert(faam_demuxer_open(NULL, &input_io, &demuxer) == FAAM_OK);
    faam_track_info info = { .struct_size = sizeof(info) };
    assert(faam_demuxer_get_track_info(demuxer, 0, &info) == FAAM_OK);
    assert(info.total_frames == 2 && info.timescale == 44100);
    sample_state samples = { .track_id = info.track_id };
    progress_state demux_progress = {0};
    assert(faam_engine_demux_track(demuxer, &info, sample_cb, &samples,
                                   progress_cb, &demux_progress) == FAAM_OK);
    assert(samples.count == 2 && samples.payloads[0] == 0xA1 && samples.payloads[1] == 0xC3);
    assert(demux_progress.calls == 3 && demux_progress.frames == 2 && demux_progress.final);
    assert(faam_demuxer_close(&demuxer) == FAAM_OK);
    fclose(output);
}

static void test_late_multi_block_frame(void)
{
    adts_params lc = { .profile = 1, .sr_idx = 4, .channel_cfg = 2 };
    uint8_t data[32];
    size_t len = append_frame(data, 0, lc, 0x11);
    size_t second_frame = len;
    len = append_frame(data, len, lc, 0x22);
    data[second_frame + 6] |= 1; /* number_of_raw_data_blocks_in_frame */
    FILE *input = tmpfile();
    FILE *output = tmpfile();
    assert(input && output && fwrite(data, 1, len, input) == len);
    rewind(input);

    /* The engine stops when the unsupported frame appears, preserving the valid prefix. */
    static const uint8_t asc[] = { 0x12, 0x10 };
    faam_track_config track = {
        .struct_size = sizeof(track), .track_type = FAAM_TRACK_AUDIO,
        .codec_id = FAAM_CODEC_AAC, .timescale = 44100, .sample_rate = 44100,
        .channels = 2, .codec_data = asc, .codec_data_len = sizeof(asc)
    };
    faam_muxer_config config;
    assert(faam_muxer_config_init(&config, sizeof(config)) == FAAM_OK);
    config.num_tracks = 1;
    config.tracks = &track;
    faam_io io = { .struct_size = sizeof(io), .user_data = output,
                   .read = file_read, .write = file_write,
                   .seek = file_seek, .tell = file_tell };
    faam_muxer *muxer = NULL;
    assert(faam_muxer_open(&config, &io, &muxer) == FAAM_OK);
    uint32_t frames = 0;
    bool multi_block = false;
    progress_state progress = {0};
    assert(faam_engine_mux_adts_frames(muxer, 1, input, &lc, NULL, 0,
                                       &frames, &multi_block, progress_cb,
                                       &progress) == FAAM_OK);
    assert(frames == 1 && multi_block);
    assert(progress.calls == 1 && progress.frames == 1 && !progress.final);
    assert(faam_muxer_close(&muxer) == FAAM_OK);
    fclose(input);
    fclose(output);
}

int main(void)
{
    test_mux_and_demux_engine();
    test_late_multi_block_frame();
    return 0;
}
