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

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "decode_engine.h"
#include "ftest.h"

static bool g_session_started = false;
static bool g_summary_called = false;
static int g_progress_count = 0;

static void test_session_start_cb(const decode_session_info_t *info, void *user_data)
{
    (void)user_data;
    g_session_started = true;
    FT_CHECK(strcmp(info->input_filename, "nonexistent.aac") == 0);
}

static void test_summary_cb(const decode_summary_t *summary, void *user_data)
{
    (void)user_data;
    g_summary_called = true;
    (void)summary;
}

static bool test_progress_cb(const progress_info_t *info, void *user_data)
{
    (void)user_data;
    (void)info;
    g_progress_count++;
    return true;
}

static void test_decode_engine_init(void)
{
    decode_options_t opts;
    init_decode_options(&opts);
    FT_CHECK_INT(opts.bit_depth, 16);
    FT_CHECK(opts.gapless);
    FT_CHECK_INT(opts.verbose, 1);
}

static void test_decode_engine_nonexistent_file(void)
{
    g_session_started = false;
    g_summary_called = false;
    g_progress_count = 0;

    decode_options_t opts;
    init_decode_options(&opts);
    opts.input_filename = "nonexistent.aac";
    opts.output_filename = "output.wav";

    decode_callbacks_t cbs = {
        .progress_cb = test_progress_cb,
        .session_start_cb = test_session_start_cb,
        .summary_cb = test_summary_cb,
        .user_data = NULL
    };

    int res = run_decoding_session_ext(&opts, &cbs);
    FT_CHECK_INT(res, DECODE_ERROR);
    FT_CHECK(!g_summary_called);
}

int main(void)
{
    test_decode_engine_init();
    test_decode_engine_nonexistent_file();
    return ft_done();
}
