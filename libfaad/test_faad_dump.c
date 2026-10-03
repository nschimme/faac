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


/* Exercise dump lifecycle in one process: output must be readable immediately
 * after close, append across streams, and restart frame numbering per stream. */
#include "faad.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void decode_to_dump(const char *path)
{
#ifdef _WIN32
    assert(_putenv_s("FAAD_DUMP", path) == 0);
#else
    assert(setenv("FAAD_DUMP", path, 1) == 0);
#endif
    faad_config cfg;
    assert(faad_config_init(&cfg, sizeof(cfg)) == FAAD_OK);
    cfg.stream_format = FAAD_STREAM_RAW;
    const uint8_t asc[] = { 0x12, 0x08 }; /* AAC-LC, 44.1 kHz, mono */
    const uint8_t frame[] = { 0xe0 }; /* END-only frame, concealed */
    faad_decoder *dec = NULL;
    assert(faad_decoder_open(&cfg, asc, sizeof(asc), &dec) == FAAD_OK);
    int16_t pcm[2048];
    for (int i = 0; i < 2; i++) {
        uint32_t used = 0, written = 0;
        assert(faad_decode_frame(dec, frame, sizeof(frame), &used,
                                pcm, sizeof(pcm), &written, NULL) == FAAD_OK);
        assert(used == sizeof(frame));
    }
    assert(faad_decoder_close(&dec) == FAAD_OK);
    assert(dec == NULL);
    assert(faad_decoder_close(&dec) == FAAD_OK);
}

static void check_dump(const char *path, int streams)
{
    FILE *f = fopen(path, "r");
    assert(f != NULL);
    for (int s = 0; s < streams; s++) {
        for (int frame = 1; frame <= 2; frame++) {
            char actual[128], expected[128];
            snprintf(expected, sizeof(expected), "B %d 3 0 0 0 0 0 0 0 0\n", frame);
            assert(fgets(actual, sizeof(actual), f) != NULL);
            assert(strcmp(actual, expected) == 0);
        }
    }
    assert(fgetc(f) == EOF);
    assert(!ferror(f));
    assert(fclose(f) == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    decode_to_dump(argv[1]);
    check_dump(argv[1], 1);
    decode_to_dump(argv[1]);
    check_dump(argv[1], 2);
    decode_to_dump(argv[2]);
    check_dump(argv[2], 1);
    check_dump(argv[1], 2);
    puts("FAAD dump close, append, frame reset, and path change tests passed.");
    return 0;
}
