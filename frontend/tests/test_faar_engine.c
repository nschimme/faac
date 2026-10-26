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

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "faar_engine.h"

static const int16_t input_samples[] = {0, 1000, -1000, 32767};

static int check(int ok, const char *message) {
    if (!ok)
        fprintf(stderr, "FAIL: %s\n", message);
    return ok;
}

static int run_case(faar_pcm_format_t format, int write_wav,
                    const uint8_t *expected, size_t expected_size) {
    FILE *input = tmpfile();
    FILE *output = tmpfile();
    if (!check(input != NULL && output != NULL, "create temporary streams"))
        goto failed;
    if (!check(fwrite(input_samples, sizeof(input_samples), 1, input) == 1,
               "write raw input"))
        goto failed;
    rewind(input);

    pcmfile_t pcm = {
        .f = input,
        .channels = 1,
        .samplebytes = 2,
        .samplerate = 48000,
        .samples = 4,
        .data_remaining = sizeof(input_samples),
        .bounded_data = true,
        .bigendian = false,
        .isfloat = false,
    };
    faar_engine_options_t options = {
        .output_rate = 48000,
        .quality = FAAR_QUALITY_GOOD,
        .format = format,
        .dither = 0,
        .verbosity = 0,
        .write_wav = write_wav != 0,
    };
    faar_engine_stats_t stats;
    if (!check(faar_engine_run(&pcm, output, &options, &stats) == 0,
               "run resampler engine"))
        goto failed;
    if (!check(stats.input_frames == 4 && stats.output_frames == 4,
               "engine frame counts"))
        goto failed;
    if (!check(fflush(output) == 0 && fseek(output, 0, SEEK_SET) == 0,
               "rewind output"))
        goto failed;

    if (write_wav) {
        uint8_t header[80];
        if (!check(fread(header, sizeof(header), 1, output) == 1,
                   "read WAV header"))
            goto failed;
        if (!check(memcmp(header, "RIFF", 4) == 0 &&
                   memcmp(header + 8, "WAVEJUNK", 8) == 0 &&
                   memcmp(header + 48, "fmt ", 4) == 0 &&
                   memcmp(header + 72, "data", 4) == 0,
                   "WAV chunk layout"))
            goto failed;
        if (!check(header[76] == expected_size && header[77] == 0,
                   "WAV data length"))
            goto failed;
    }

    uint8_t actual[32];
    if (!check(expected_size <= sizeof(actual) &&
               fread(actual, 1, expected_size, output) == expected_size,
               "read output PCM"))
        goto failed;
    if (!check(memcmp(actual, expected, expected_size) == 0,
               "equal-rate PCM encoding"))
        goto failed;
    if (!check(fgetc(output) == EOF, "no extra output bytes"))
        goto failed;
    fclose(input);
    fclose(output);
    return 1;

failed:
    if (input) fclose(input);
    if (output) fclose(output);
    return 0;
}

int main(void) {
    static const uint8_t expected_s16[] = {
        0x00, 0x00, 0xe8, 0x03, 0x18, 0xfc, 0xff, 0x7f
    };
    static const uint8_t expected_s24[] = {
        0x00, 0x00, 0x00, 0x00, 0xe8, 0x03,
        0x00, 0x18, 0xfc, 0x00, 0xff, 0x7f
    };
    static const uint8_t expected_f32[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfa, 0x3c,
        0x00, 0x00, 0xfa, 0xbc, 0x00, 0xfe, 0x7f, 0x3f
    };

    int ok = 1;
    ok &= run_case(FAAR_PCM_S16LE, 1, expected_s16, sizeof(expected_s16));
    ok &= run_case(FAAR_PCM_S24LE, 0, expected_s24, sizeof(expected_s24));
    ok &= run_case(FAAR_PCM_F32LE, 0, expected_f32, sizeof(expected_f32));
    return ok ? 0 : 1;
}
