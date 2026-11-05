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

enum { SAMPLES = 4 };

static uint8_t sample_byte(unsigned i, unsigned k) { return (uint8_t)(0x40 + i * 16 + k); }
static unsigned sample_size(unsigned i) { return 10 + i; }

/* tfra_entries is written as the entry count; the entries themselves are always one. */
static void build(mem_file *f, uint32_t tfra_entries)
{
    memset(f, 0, sizeof(*f));
    size_t b = mem_box_begin(f, "ftyp");
    mem_put(f, "M4A \0\0\0\0M4A isom", 16);
    mem_box_end(f, b);

    b = mem_box_begin(f, "moov");
    size_t mv = mem_box_begin(f, "mvhd");
    uint8_t zeros[100] = { 0 };
    mem_put32(f, 0);          /* version, flags */
    mem_put(f, zeros, 8);     /* creation, modification */
    mem_put32(f, 1000);       /* timescale */
    mem_put(f, zeros, 4);     /* duration */
    mem_put32(f, 0x00010000); /* rate */
    mem_put(f, zeros, 70);    /* volume .. next_track_ID */
    mem_box_end(f, mv);
    size_t mx = mem_box_begin(f, "mvex");
    size_t tx = mem_box_begin(f, "trex");
    mem_put32(f, 0);
    mem_put32(f, 1);
    mem_put(f, zeros, 16);
    mem_box_end(f, tx);
    mem_box_end(f, mx);
    mem_box_end(f, b);

    size_t moof = mem_box_begin(f, "moof");
    b = mem_box_begin(f, "mfhd");
    mem_put32(f, 0);
    mem_put32(f, 1);
    mem_box_end(f, b);
    size_t traf = mem_box_begin(f, "traf");
    b = mem_box_begin(f, "tfhd");
    mem_put32(f, 0x01);       /* base-data-offset-present */
    mem_put32(f, 1);          /* track_ID */
    mem_put64(f, moof);       /* absolute */
    mem_box_end(f, b);
    b = mem_box_begin(f, "trun");
    mem_put32(f, 0x201);      /* data-offset and sample-size present */
    mem_put32(f, SAMPLES);
    size_t data_off_at = f->len;
    mem_put32(f, 0);          /* patched below */
    for (unsigned i = 0; i < SAMPLES; i++) mem_put32(f, sample_size(i));
    mem_box_end(f, b);
    mem_box_end(f, traf);
    mem_box_end(f, moof);

    size_t mdat = mem_box_begin(f, "mdat");
    for (unsigned i = 0; i < SAMPLES; i++)
        for (unsigned k = 0; k < sample_size(i); k++) { uint8_t v = sample_byte(i, k); mem_put(f, &v, 1); }
    mem_box_end(f, mdat);
    uint32_t rel = (uint32_t)(mdat + 8 - moof);
    f->buf[data_off_at] = (uint8_t)(rel >> 24); f->buf[data_off_at + 1] = (uint8_t)(rel >> 16);
    f->buf[data_off_at + 2] = (uint8_t)(rel >> 8); f->buf[data_off_at + 3] = (uint8_t)rel;

    size_t mfra = mem_box_begin(f, "mfra");
    b = mem_box_begin(f, "tfra");
    mem_put32(f, 0);
    mem_put32(f, 1);
    mem_put32(f, 0);          /* one-byte traf, trun and sample numbers */
    mem_put32(f, tfra_entries);
    mem_put32(f, 0);          /* time */
    mem_put32(f, (uint32_t)moof);
    mem_put(f, "\1\1\1", 3);
    mem_box_end(f, b);
    b = mem_box_begin(f, "mfro");
    mem_put32(f, 0);
    size_t size_at = f->len;
    mem_put32(f, 0);
    mem_box_end(f, b);
    mem_box_end(f, mfra);
    uint32_t total = (uint32_t)(f->len - mfra);
    f->buf[size_at] = (uint8_t)(total >> 24); f->buf[size_at + 1] = (uint8_t)(total >> 16);
    f->buf[size_at + 2] = (uint8_t)(total >> 8); f->buf[size_at + 3] = (uint8_t)total;
}

static size_t find_box(const mem_file *f, const char *type, size_t from, size_t to)
{
    for (size_t p = from; p + 8 <= to;) {
        uint32_t sz = mem_be32(f->buf + p);
        CHECK(sz >= 8 && sz <= to - p);
        if (!memcmp(f->buf + p + 4, type, 4)) return p;
        p += sz;
    }
    return (size_t)-1;
}

/* Every sample still reads back where the file's own pointers say it is, and the index still
 * names the moof and ends the file. */
static void check_layout(const mem_file *f)
{
    size_t moof = find_box(f, "moof", 0, f->len);
    CHECK(moof != (size_t)-1);
    size_t traf = find_box(f, "traf", moof + 8, moof + mem_be32(f->buf + moof));
    size_t tfhd = find_box(f, "tfhd", traf + 8, traf + mem_be32(f->buf + traf));
    size_t trun = find_box(f, "trun", traf + 8, traf + mem_be32(f->buf + traf));
    CHECK(tfhd != (size_t)-1 && trun != (size_t)-1);
    uint64_t base = mem_be64(f->buf + tfhd + 8 + 8);
    CHECK(base == moof);
    size_t at = (size_t)(base + (int32_t)mem_be32(f->buf + trun + 16));
    for (unsigned i = 0; i < SAMPLES; i++) {
        CHECK(mem_be32(f->buf + trun + 20 + 4 * i) == sample_size(i));
        CHECK(at + sample_size(i) <= f->len);
        for (unsigned k = 0; k < sample_size(i); k++) CHECK(f->buf[at + k] == sample_byte(i, k));
        at += sample_size(i);
    }

    size_t mfra = find_box(f, "mfra", 0, f->len);
    CHECK(mfra != (size_t)-1 && mfra + mem_be32(f->buf + mfra) == f->len);
    size_t tfra = find_box(f, "tfra", mfra + 8, f->len);
    CHECK(tfra != (size_t)-1);
    size_t entry = mem_be32(f->buf + tfra + 28);
    CHECK(entry == moof);
    CHECK(!memcmp(f->buf + entry + 4, "moof", 4));
    size_t mfro = find_box(f, "mfro", mfra + 8, f->len);
    CHECK(mem_be32(f->buf + mfro + 12) == mem_be32(f->buf + mfra));
    CHECK(mfro + 16 == f->len);
}

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
