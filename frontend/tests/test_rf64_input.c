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

/* Exercise RF64 chunk traversal directly, without invoking the encoder CLI. */
#include "input.h"
#include "cli_io.h"
#include <string.h>

static bool write_u16(FILE *f, uint16_t value)
{
    uint8_t bytes[2] = { (uint8_t)value, (uint8_t)(value >> 8) };
    return fwrite(bytes, 1, sizeof(bytes), f) == sizeof(bytes);
}

static bool write_u32(FILE *f, uint32_t value)
{
    uint8_t bytes[4] = { (uint8_t)value, (uint8_t)(value >> 8),
                         (uint8_t)(value >> 16), (uint8_t)(value >> 24) };
    return fwrite(bytes, 1, sizeof(bytes), f) == sizeof(bytes);
}

static bool write_u64(FILE *f, uint64_t value)
{
    return write_u32(f, (uint32_t)value) && write_u32(f, (uint32_t)(value >> 32));
}

static bool write_tag(FILE *f, const char tag[4])
{
    return fwrite(tag, 1, 4, f) == 4;
}

static bool write_fixture(const char *path, uint64_t data_size,
                          uint64_t junk_size, uint64_t *data_offset)
{
    FILE *f = cli_fopen(path, "wb+");
    if (!f) return false;

    uint32_t ds64_size = junk_size ? 40 : 28;
    uint64_t riff_size = 12 + 8 + ds64_size + 8 + 16 + 8 + data_size + (data_size & 1);
    if (junk_size) riff_size += 8 + junk_size + (junk_size & 1);
    riff_size -= 8;

    bool ok = write_tag(f, "RF64") && write_u32(f, UINT32_MAX) && write_tag(f, "WAVE") &&
              write_tag(f, "ds64") && write_u32(f, ds64_size) &&
              write_u64(f, riff_size) && write_u64(f, data_size) &&
              write_u64(f, data_size / 4) && write_u32(f, junk_size ? 1 : 0);
    if (ok && junk_size)
        ok = write_tag(f, "JUNK") && write_u64(f, junk_size);

    if (ok && junk_size)
        ok = write_tag(f, "JUNK") && write_u32(f, UINT32_MAX) &&
             cli_fseek(f, cli_ftell(f) + junk_size + (junk_size & 1));

    if (ok) {
        ok = write_tag(f, "fmt ") && write_u32(f, 16) &&
             write_u16(f, 1) && write_u16(f, 2) && write_u32(f, 48000) &&
             write_u32(f, 192000) && write_u16(f, 4) && write_u16(f, 16) &&
             write_tag(f, "data") && write_u32(f, UINT32_MAX);
    }
    if (ok) {
        *data_offset = cli_ftell(f);
        const uint8_t samples[4] = { 1, 2, 3, 4 };
        ok = fwrite(samples, 1, sizeof(samples), f) == sizeof(samples);
    }
    if (ok && data_size > 4)
        ok = cli_fseek(f, *data_offset + data_size - 1) && fputc(0, f) != EOF;
    if (fclose(f) != 0) ok = false;
    return ok;
}

static bool check_fixture(const char *path, uint64_t data_size, uint64_t junk_size)
{
    uint64_t expected_offset;
    if (!write_fixture(path, data_size, junk_size, &expected_offset)) {
        fprintf(stderr, "RF64 fixture write failed: data=%llu junk=%llu\n",
                (unsigned long long)data_size, (unsigned long long)junk_size);
        return false;
    }

    pcmfile_t *f = wav_open_read(path, false);
    if (!f) {
        fprintf(stderr, "RF64 fixture open failed: data=%llu junk=%llu\n",
                (unsigned long long)data_size, (unsigned long long)junk_size);
        return false;
    }
    uint8_t samples[4];
    bool ok = f->channels == 2 && f->samplebytes == 2 && f->samplerate == 48000 &&
              f->samples == (int64_t)(data_size / 4) && cli_ftell(f->f) == expected_offset &&
              wav_read_native(f, samples, 2) == 2 && !memcmp(samples, "\1\2\3\4", 4);
    wav_close(f);
    if (!ok) fprintf(stderr, "RF64 fixture mismatch: data=%llu junk=%llu\n",
                     (unsigned long long)data_size, (unsigned long long)junk_size);
    return ok;
}

static bool check_float_endian(void)
{
    const uint8_t samples[2][8] = {
        { 0x00, 0x00, 0x80, 0x3e, 0x00, 0x00, 0x00, 0xbf },
        { 0x3e, 0x80, 0x00, 0x00, 0xbf, 0x00, 0x00, 0x00 }
    };
    for (unsigned be = 0; be <= 1; be++) {
        FILE *file = tmpfile();
        if (!file) return false;
        pcmfile_t f = { .f = file, .channels = 2, .samplebytes = 4,
                       .isfloat = true, .bigendian = be };
        float pcm[2];
        bool ok = fwrite(samples[be], 1, 8, file) == 8 && cli_fseek(file, 0) &&
                  wav_read_float32(&f, pcm, 2, NULL) == 2 &&
                  pcm[0] == 0.25f && pcm[1] == -0.5f;
        fclose(file);
        if (!ok) return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    bool ok = check_float_endian();
    if (!ok) fprintf(stderr, "float endian check failed\n");
    if (ok) ok = check_fixture(argv[1], 4, 0);
#ifndef _WIN32
    const uint64_t large = (UINT64_C(1) << 32) + 32768;
    const uint64_t large_junk = (UINT64_C(1) << 32) + 2;
    if (ok) ok = check_fixture(argv[1], large, 0) && check_fixture(argv[1], 4, large_junk);
#endif
    remove(argv[1]);
    return ok ? 0 : 1;
}
