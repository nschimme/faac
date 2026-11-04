/*
 * FAAR - Freeware Advanced Audio Resampler
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

/* libFuzzer harness for the public FAAR streaming API. */
#include <faar.h>

#include <stdint.h>
#include <string.h>

#ifndef MAX_CHANNELS
#define MAX_CHANNELS 8
#endif

#define MAX_INPUT_FRAMES 128
#define MAX_OUTPUT_FRAMES 4096

static const uint32_t rates[] = {
    8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000,
    88200, 96000, 176400, 192000, 352800, 384000, 705600, 768000
};

static uint32_t choose_rate(uint8_t value) {
    return rates[value % (sizeof(rates) / sizeof(rates[0]))];
}

static void drain_pending(faar_resampler *handle, float *output,
                          size_t output_capacity) {
    for (unsigned guard = 0; guard < 64; ++guard) {
        size_t produced = 0;
        if (faar_resampler_process(handle, NULL, 0, output, output_capacity,
                                   &produced) != FAAR_OK || produced == 0) break;
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 6) return 0;

    faar_library_info lib;
    memset(&lib, 0, sizeof(lib));
    lib.struct_size = sizeof(lib);
    if (faar_get_library_info(&lib) != FAAR_OK || lib.struct_size < sizeof(lib))
        __builtin_trap();
    if (lib.flags & ~FAAR_LIB_FLOAT_ONLY)
        __builtin_trap();

    uint16_t channel_limit = MAX_CHANNELS > 0 ? MAX_CHANNELS : 1;
    if (channel_limit > 8) channel_limit = 8;
    if (lib.max_channels && lib.max_channels < channel_limit)
        channel_limit = (uint16_t)lib.max_channels;
    uint16_t channels = 1 + data[2] % channel_limit;
    uint32_t input_rate = choose_rate(data[0]);
    uint32_t output_rate = choose_rate(data[1]);
    faar_quality quality = (faar_quality)(data[3] % 3);
    unsigned cascade_selector = data[4] % 3;
    uint32_t cascade = cascade_selector == 0 ? 0 : (cascade_selector == 1 ? 2 : 4);

    size_t available = (size - 6) / (sizeof(float) * channels);
    size_t frames = available < MAX_INPUT_FRAMES ? available : MAX_INPUT_FRAMES;
    if (!frames) return 0;

    float input[MAX_INPUT_FRAMES * 8];
    float output[MAX_OUTPUT_FRAMES * 8];
    memcpy(input, data + 6, frames * channels * sizeof(float));

    faar_config config;
    if (faar_config_init(&config, sizeof(config)) != FAAR_OK) return 0;
    config.in_rate = input_rate;
    config.out_rate = output_rate;
    config.channels = channels;
    config.quality = quality;
    config.decimate_factor = cascade;
    config.flags = (uint32_t)(data[5] & 0x0f);
    if (data[4] & 0x80)
        config.max_input_frames = 1 + (data[5] >> 4) * 4;

    faar_design *design = NULL;
    faar_resampler *handle = NULL;
    if (data[4] & 0x40) {
        if (faar_design_open(&config, &design) != FAAR_OK) return 0;
        if (faar_resampler_open_from_design(design, &handle) != FAAR_OK) {
            faar_design_close(&design);
            return 0;
        }
        /* Streams retain the design, so the caller may drop its reference now. */
        faar_design_close(&design);
    } else if (faar_resampler_open(&config, &handle) != FAAR_OK) {
        return 0;
    }

    faar_resampler_info info;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    if (faar_resampler_get_info(handle, &info) != FAAR_OK ||
        info.struct_size < sizeof(info))
        __builtin_trap();

    /* A chunk above the reserved bound is rejected before consuming input. */
    if (config.max_input_frames && frames >= (size_t)config.max_input_frames + 1) {
        size_t produced = 123;
        if (faar_resampler_process(handle, input, (size_t)config.max_input_frames + 1,
                output, MAX_OUTPUT_FRAMES, &produced) != FAAR_ERR_INVALID_ARGUMENT ||
            produced != 0)
            __builtin_trap();
    }

    size_t offset = 0;
    while (offset < frames) {
        size_t block = 1 + data[(offset + 6) % size] % 32;
        if (block > frames - offset) block = frames - offset;
        size_t produced = 0;
        if (faar_resampler_process(handle, input + offset * channels, block,
                output, MAX_OUTPUT_FRAMES, &produced) != FAAR_OK) break;
        if (produced > MAX_OUTPUT_FRAMES) __builtin_trap();
        drain_pending(handle, output, MAX_OUTPUT_FRAMES);
        offset += block;
    }
    for (unsigned guard = 0; guard < 64; ++guard) {
        size_t produced = 0;
        if (faar_resampler_finalize(handle, output, MAX_OUTPUT_FRAMES,
                &produced) != FAAR_OK || produced == 0) break;
        if (produced > MAX_OUTPUT_FRAMES) __builtin_trap();
    }
    faar_resampler_close(&handle);
    return 0;
}
