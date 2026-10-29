/*
 * FAAC - Freeware Advanced Audio Coder
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

/* Focused tests for encode-engine options and callback/session behavior. */
#include "encode_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #x); \
    return 1; \
} } while (0)

static void put16(FILE *f, unsigned value)
{
    fputc((int)(value & 255), f);
    fputc((int)((value >> 8) & 255), f);
}

static void put32(FILE *f, unsigned value)
{
    put16(f, value & 65535);
    put16(f, value >> 16);
}

static int make_wav(const char *path)
{
    enum { samples = 4096 };
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    put32(f, 0x46464952); put32(f, 36 + samples * 2); put32(f, 0x45564157);
    put32(f, 0x20746d66); put32(f, 16); put16(f, 1); put16(f, 1);
    put32(f, 44100); put32(f, 88200); put16(f, 2); put16(f, 16);
    put32(f, 0x61746164); put32(f, samples * 2);
    for (unsigned i = 0; i < samples; i++) {
        int sample = ((int)(i % 100) - 50) * 160;
        put16(f, (unsigned)(unsigned short)sample);
    }
    return fclose(f) == 0;
}

typedef struct {
    unsigned starts;
    unsigned progress_calls;
    unsigned finals;
    unsigned summaries;
    encode_session_info_t session;
    encode_summary_t summary;
} callbacks_state;

static void on_start(const encode_session_info_t *info, void *user_data)
{
    callbacks_state *state = user_data;
    state->starts++;
    state->session = *info;
}

static bool on_progress(const progress_info_t *info, void *user_data)
{
    callbacks_state *state = user_data;
    state->progress_calls++;
    if (info->is_final) state->finals++;
    return true;
}

static void on_summary(const encode_summary_t *summary, void *user_data)
{
    callbacks_state *state = user_data;
    state->summaries++;
    state->summary = *summary;
}

static int test_options(void)
{
    encode_options_t opts;
    init_encode_options(&opts);
    CHECK(opts.container_mp4);
    CHECK(opts.stream_format == FAAC_STREAM_ADTS);
    CHECK(opts.mpeg_version == 4);
    CHECK(opts.object_type == FAAC_OBJ_AUTO);
    CHECK(opts.quant_quality == 0 && opts.bit_rate == DEFAULT_ABR_KBPS * 1000);
    CHECK(opts.use_tns && opts.use_pns && opts.use_lfe == -1);

    parse_quality_or_bitrate("72", false, &opts);
    CHECK(opts.quant_quality == 72 && opts.bit_rate == 0);
    parse_quality_or_bitrate("", false, &opts);
    CHECK(opts.quant_quality == DEFAULT_QUANT_QUALITY && opts.bit_rate == 0);
    parse_quality_or_bitrate("999999", false, &opts);
    CHECK(opts.quant_quality == UINT16_MAX && opts.bit_rate == 0);
    parse_quality_or_bitrate("96", true, &opts);
    CHECK(opts.bit_rate == 96000 && opts.quant_quality == 0);
    parse_quality_or_bitrate("n/a", true, &opts);
    CHECK(opts.bit_rate == DEFAULT_ABR_KBPS * 1000 && opts.quant_quality == 0);

    CHECK(add_custom_tag_to_options(&opts, "key", "value"));
    CHECK(add_custom_tag_to_options(&opts, "second", "two"));
    CHECK(opts.custom_tag_count == 2 && opts.custom_tag_cap >= 2);
    CHECK(!strcmp(opts.custom_tags[0].name, "key"));
    CHECK(!strcmp(opts.custom_tags[0].value, "value"));
    CHECK(!add_custom_tag_to_options(NULL, "key", "value"));
    CHECK(!add_custom_tag_to_options(&opts, NULL, "value"));
    free_encode_options(&opts);
    CHECK(opts.custom_tags == NULL && opts.custom_tag_count == 0 && opts.custom_tag_cap == 0);
    free_encode_options(&opts);
    return 0;
}

static int test_session(const char *input, const char *output)
{
    encode_options_t opts;
    callbacks_state state = {0};
    encode_callbacks_t callbacks = {
        .progress_cb = on_progress,
        .session_start_cb = on_start,
        .summary_cb = on_summary,
        .user_data = &state
    };

    init_encode_options(&opts);
    opts.input_filename = input;
    opts.output_filename = output;
    opts.container_mp4 = false;
    opts.stream_format = FAAC_STREAM_ADTS;
    opts.quant_quality = 100;
    opts.bit_rate = 0;
    opts.verbose = 0;

    CHECK(run_encoding_session_ext(NULL, &callbacks) != ENCODE_SUCCESS);
    CHECK(run_encoding_session_ext(&opts, &callbacks) == ENCODE_SUCCESS);
    CHECK(state.starts == 1);
    CHECK(state.session.sample_rate == 44100 && state.session.num_channels == 1);
    CHECK(state.session.total_input_samples == 4096);
    CHECK(state.session.frame_size > 0);
    CHECK(state.progress_calls > 0 && state.finals == 1);
    CHECK(state.summaries == 1);
    CHECK(!state.summary.is_mp4 && state.summary.sample_count == 4096);
    CHECK(state.summary.frame_count > 0 && state.summary.max_frame_size > 0);

    FILE *f = fopen(output, "rb");
    CHECK(f != NULL);
    CHECK(fseek(f, 0, SEEK_END) == 0);
    CHECK(ftell(f) > 0);
    fclose(f);
    free_encode_options(&opts);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    if (test_options()) return 1;
    if (!make_wav(argv[1])) return 1;
    if (test_session(argv[1], argv[2])) return 1;
    remove(argv[1]);
    remove(argv[2]);
    return 0;
}
