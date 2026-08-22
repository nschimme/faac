/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2001 Menno Bakker
 * Copyright (C) 2002-2026 FAAC Team
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

#ifndef ENCODE_ENGINE_H
#define ENCODE_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include <faac.h>
#include "progress.h"
#include "mp4write.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *input_filename;
    const char *output_filename;

    bool container_mp4;
    bool raw_pcm_input;
    int raw_channels;
    int raw_bits;
    int raw_rate;
    int raw_endian;

    int center_channel;
    int lfe_channel;

    enum faac_mpeg_version mpeg_version;
    enum faac_object_type object_type;
    enum faac_joint_mode joint_mode;
    enum faac_stream_format stream_format;
    enum faac_shortctl_mode shortctl;

    bool use_tns;
    int use_lfe; /* -1 for auto (ch >= 6), 0 = false, 1 = true */
    int pns_level; /* -1 to leave default */

    uint32_t quant_quality;
    uint32_t bit_rate; /* total bitrate in bps (whole stream) */
    uint32_t max_bit_rate; /* bps whole stream cap */
    uint32_t bandwidth; /* cutoff frequency in Hz */

    bool ignore_wav_length;
    int overwrite;

    mp4_metadata_t metadata;
    const char *creation_time_str;
    const uint8_t *art_data;
    uint64_t art_size;

    int verbose;
} encode_options_t;

/* Canonical defaults, shared by both the CLI and GUI frontends. */
#define DEFAULT_QUANT_QUALITY 100
#define DEFAULT_ABR_KBPS      128

void init_encode_options(encode_options_t *opts);

/* Parse a quality-or-bitrate text field (as typed into the CLI's -q/-b or
   the GUI's rate edit box) into opts->quant_quality/opts->bit_rate,
   falling back to DEFAULT_QUANT_QUALITY/DEFAULT_ABR_KBPS on invalid/empty
   input rather than silently producing 0. is_bitrate_mode selects which
   field is being set and whether the value is in kbps (bitrate) or a raw
   quality percentage. */
void parse_quality_or_bitrate(const char *text, bool is_bitrate_mode,
                               encode_options_t *opts);

#define ENCODE_SUCCESS   0
#define ENCODE_ERROR     1
#define ENCODE_CANCELLED 2

typedef struct {
    const char *input_filename;
    const char *output_filename;
    unsigned int sample_rate;
    unsigned int num_channels;
    uint64_t total_input_samples;
    unsigned int frame_size;

    bool container_mp4;
    enum faac_stream_format stream_format;
    enum faac_mpeg_version mpeg_version;
    enum faac_object_type object_type;
    enum faac_joint_mode joint_mode;
    bool use_tns;
    int pns_level;
    unsigned int bandwidth;
    unsigned long quant_quality;
    int bit_rate; /* bps per channel */

    bool remapping_channels;
    int center_channel;
    int lfe_channel;
    enum faac_shortctl_mode shortctl;
} encode_session_info_t;

typedef struct {
    uint32_t frame_count;
    uint32_t sample_count;
    uint32_t max_bitrate;
    uint32_t avg_bitrate;
    uint32_t max_frame_size;
    bool is_mp4;
} encode_summary_t;

typedef encode_summary_t encode_mp4_summary_t;

typedef void (*session_start_callback_t)(const encode_session_info_t *info, void *user_data);
typedef void (*summary_callback_t)(const encode_summary_t *summary, void *user_data);
typedef summary_callback_t mp4_summary_callback_t;
typedef void (*log_message_callback_t)(int level, const char *message, void *user_data);

typedef struct {
    progress_callback_t progress_cb;
    session_start_callback_t session_start_cb;
    summary_callback_t summary_cb;
    mp4_summary_callback_t mp4_summary_cb; /* alias for backward compatibility */
    log_message_callback_t log_cb;
    void *user_data;
} encode_callbacks_t;

int run_encoding_session(const encode_options_t *opts,
                          progress_callback_t progress_cb,
                          void *user_data);

int run_encoding_session_ext(const encode_options_t *opts,
                              const encode_callbacks_t *callbacks);

#ifdef __cplusplus
}
#endif

#endif /* ENCODE_ENGINE_H */
