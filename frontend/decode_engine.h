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

#ifndef DECODE_ENGINE_H
#define DECODE_ENGINE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <faad3.h>
#include "progress.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *input_filename;
    const char *output_filename;
    const char *adts_outfile;

    bool write_stdout;
    bool raw_format;
    uint32_t bit_depth; /* 16, 24, 32 */
    bool is_float;
    enum faad_downmix_mode downmix;
    uint8_t drc_cut;
    uint8_t drc_boost;
    uint8_t drc_target;
    bool gapless;
    uint32_t want_track;
    uint8_t verbose; /* 0: quiet, 1: progress, 2: detailed summary */
    bool strict_mode;
    bool overwrite;
    double jump_seconds;
    bool info_only;
    bool json_info;
} decode_options_t;

void init_decode_options(decode_options_t *opts);

#define DECODE_SUCCESS   0
#define DECODE_ERROR     1
#define DECODE_CANCELLED 2

typedef struct {
    const char *input_filename;
    const char *output_filename;
    uint32_t sample_rate;
    uint16_t num_channels;
    uint64_t total_track_samples;
    uint32_t total_track_frames;
    double duration_sec;
    double avg_bitrate_kbps;
    bool is_mp4;
    bool raw_format;
    uint32_t bit_depth;
    bool is_float;
    enum faad_object_type object_type;
    enum faad_downmix_mode downmix;
    const char *profile_name;
} decode_session_info_t;

typedef struct {
    uint64_t frame_count;
    uint64_t sample_count;
    uint64_t total_pcm_bytes;
    uint32_t max_bitrate_kbps;
    uint32_t max_frame_bytes;
    double duration_sec;
    double time_elapsed_sec;
    double speed_factor;
    double avg_bitrate_kbps;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t bit_depth;
    bool is_float;
    bool is_mp4;
} decode_summary_t;

typedef void (*decode_session_start_callback_t)(const decode_session_info_t *info, void *user_data);
typedef void (*decode_summary_callback_t)(const decode_summary_t *summary, void *user_data);
typedef void (*decode_log_message_callback_t)(int level, const char *message, void *user_data);

typedef struct {
    progress_callback_t progress_cb;
    decode_session_start_callback_t session_start_cb;
    decode_summary_callback_t summary_cb;
    decode_log_message_callback_t log_cb;
    void *user_data;
} decode_callbacks_t;

int run_decoding_session(const decode_options_t *opts,
                          progress_callback_t progress_cb,
                          void *user_data);

int run_decoding_session_ext(const decode_options_t *opts,
                              const decode_callbacks_t *callbacks);

#ifdef __cplusplus
}
#endif

#endif /* DECODE_ENGINE_H */
