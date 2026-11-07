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

/* The speaker layout a PCE in an AudioSpecificConfig describes, as the decoder
 * reports it before any frame is decoded. The shape check once failed only in
 * an optimized build with link-time optimization, so this runs through the
 * library the way it is built. */
#include <stdint.h>
#include <string.h>
#include "faad3.h"
#include "../../tests/ftest.h"

typedef struct {
    uint8_t data[32];
    uint32_t bits;
} bitwriter;

static void put(bitwriter *w, uint32_t value, uint32_t n)
{
    while (n--) {
        if (w->bits % 8 == 0) w->data[w->bits / 8] = 0;
        w->data[w->bits / 8] |= (uint8_t)(((value >> n) & 1u) << (7 - w->bits % 8));
        w->bits++;
    }
}

/* One element of a PCE list: is_cpe and its tag. */
typedef struct { uint8_t pair, tag; } element;

typedef struct {
    const char *name;
    uint8_t front, side, back, lfe;
    element elements[4];
    uint8_t lfe_tag;
    uint32_t channels, mask;
} shape;

static const shape shapes[] = {
    { "5.0 back",  2, 0, 1, 0, { {0,0}, {1,0}, {1,1} },          0, 5, 0x037 },
    { "5.1 back",  2, 0, 1, 1, { {0,0}, {1,0}, {1,1} },          0, 6, 0x03f },
    { "5.0 side",  2, 1, 0, 0, { {0,0}, {1,0}, {1,1} },          0, 5, 0x607 },
    { "5.1 side",  2, 1, 0, 1, { {0,0}, {1,0}, {1,1} },          0, 6, 0x60f },
    { "7.0 side and back", 2, 1, 1, 0, { {0,0}, {1,0}, {1,1}, {1,2} }, 0, 7, 0x637 },
    { "7.0 two back",      2, 0, 2, 0, { {0,0}, {1,0}, {1,2}, {1,1} }, 0, 7, 0x637 },
    { "7.1 side and back", 2, 1, 1, 1, { {0,0}, {1,0}, {1,1}, {1,2} }, 0, 8, 0x63f },
    { "7.1 two back",      2, 0, 2, 1, { {0,0}, {1,0}, {1,1}, {1,2} }, 0, 8, 0x63f },
    { "6.1 back",  2, 0, 2, 1, { {0,0}, {1,0}, {1,1}, {0,1} },   0, 7, 0x13f },
    { "6.1 without LFE",  2, 0, 2, 0, { {0,0}, {1,0}, {1,1}, {0,1} }, 0, 6, 0x137 },
    /* Two front pairs are the inner FLC/FRC and the outer FL/FR (7.1 wide without its LFE). */
    { "7.0 wide", 3, 0, 1, 0, { {0,0}, {1,0}, {1,1}, {1,2} }, 0, 7, 0x0f7 },
    /* Shapes without a WAVE layout keep mask 0. */
    { "duplicate pair tags", 2, 0, 2, 1, { {0,0}, {1,0}, {1,1}, {1,1} }, 0, 8, 0 },
    { "three front pairs", 3, 0, 0, 0, { {1,0}, {1,1}, {1,2} }, 0, 6, 0 },
};

static void check_shape(const shape *s)
{
    bitwriter w;
    memset(&w, 0, sizeof(w));
    put(&w, 2, 5);                    /* AAC LC */
    put(&w, 3, 4);                    /* 48 kHz */
    put(&w, 0, 4);                    /* channelConfiguration 0: a PCE follows */
    put(&w, 0, 3);                    /* GASpecificConfig */
    put(&w, 0, 4);                    /* element_instance_tag */
    put(&w, 1, 2);                    /* profile: LC */
    put(&w, 3, 4);                    /* 48 kHz */
    put(&w, s->front, 4);
    put(&w, s->side, 4);
    put(&w, s->back, 4);
    put(&w, s->lfe, 2);
    put(&w, 0, 3);                    /* assoc data */
    put(&w, 0, 4);                    /* coupling channels */
    put(&w, 0, 3);                    /* no mixdown */
    for (uint32_t i = 0; i < (uint32_t)s->front + s->side + s->back; i++) {
        put(&w, s->elements[i].pair, 1);
        put(&w, s->elements[i].tag, 4);
    }
    for (uint32_t i = 0; i < s->lfe; i++) put(&w, s->lfe_tag, 4);
    while (w.bits % 8) put(&w, 0, 1);
    put(&w, 0, 8);                    /* no comment */

    faad_config cfg;
    faad_decoder *dec = NULL;
    faad_stream_info info = { .struct_size = sizeof(info) };
    FT_REQUIRE(faad_config_init(&cfg, sizeof(cfg)) == FAAD_OK);
    cfg.stream_format = FAAD_STREAM_RAW;
    if (faad_decoder_open(&cfg, w.data, w.bits / 8, &dec) != FAAD_OK) {
        FT_CHECK(s->channels > 8 || !s->mask);
        return;
    }
    FT_REQUIRE(faad_decoder_get_info(dec, &info) == FAAD_OK);
    FT_CHECK_INT(info.channels, s->channels);
    if (info.channel_mask != s->mask)
        fprintf(stderr, "%s: mask 0x%x, want 0x%x\n", s->name, info.channel_mask, s->mask);
    FT_CHECK_INT(info.channel_mask, s->mask);
    faad_decoder_close(&dec);
}

int main(void)
{
    faad_library_info lib = { .struct_size = sizeof(lib) };
    FT_REQUIRE(faad_get_library_info(&lib) == FAAD_OK);
    if (lib.max_channels < 8) return 77;
    for (unsigned i = 0; i < sizeof(shapes) / sizeof(shapes[0]); i++)
        check_shape(&shapes[i]);
    return ft_done();
}
