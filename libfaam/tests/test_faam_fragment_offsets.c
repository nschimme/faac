/*
 * FAAM - Freeware Advanced Audio Muxer
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

/*
 * Growing moov must move the absolute positions a fragmented file keeps behind it. The input is
 * built here in the layout FFmpeg writes: tfhd base_data_offset naming the moof by absolute position,
 * and an mfra index of absolute moof offsets ending in mfro.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "faam.h"
#include "mem_io.h"
#include "../../tests/faam_test_helpers.h"

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

#include "ffmpeg_fragmented.h"

int main(void)
{
    mem_file f;
    char title[300];
    memset(title, 'T', sizeof(title) - 1);
    title[sizeof(title) - 1] = 0;

    /* Growing moov moves the moof, so the base_data_offset and the mfra entry must follow it. */
    build(&f, 1);
    check_layout(&f);
    size_t before_moof = find_box(&f, "moof", 0, f.len);
    faam_io io = mem_file_io(&f);
    CHECK(test_set_tags(&io, title, "Artist", 0) == FAAM_OK);
    CHECK(find_box(&f, "moof", 0, f.len) > before_moof);
    check_layout(&f);

    /* A second edit moves them again. */
    f.pos = 0;
    CHECK(test_set_tags(&io, "short", NULL, 0) == FAAM_OK);
    check_layout(&f);
    free(f.buf);

    /* An index that cannot be followed is refused before any byte is written. */
    build(&f, 1000);
    mem_file ref = f;
    ref.buf = (uint8_t *)malloc(f.len);
    memcpy(ref.buf, f.buf, f.len);
    io = mem_file_io(&f);
    CHECK(test_set_tags(&io, title, NULL, 0) == FAAM_ERR_BAD_CONTAINER);
    CHECK(f.len == ref.len && !memcmp(f.buf, ref.buf, f.len));
    free(ref.buf);
    free(f.buf);
    return 0;
}
