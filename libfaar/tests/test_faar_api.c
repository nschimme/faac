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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <faar.h>

typedef struct {
    union { faar_config config; faar_library_info library; faar_resampler_info info; } v;
    unsigned char tail[64];
} padded;

static int tail_untouched(const padded *p) {
    for (size_t i = 0; i < sizeof(p->tail); ++i)
        if (p->tail[i] != 0xa5) return 0;
    return 1;
}

static void test_struct_size(void) {
    padded p;
    faar_config config;

    /* Sizes below the frozen baseline are rejected. */
    memset(&p, 0xa5, sizeof(p));
    assert(faar_config_init(&p.v.config, (uint32_t)(FAAR_CONFIG_BASELINE - 1))
           == FAAR_ERR_INVALID_ARGUMENT);
    assert(faar_config_init(NULL, sizeof(faar_config)) == FAAR_ERR_INVALID_ARGUMENT);

    /* A larger caller struct is accepted; only the known bytes are written. */
    assert(faar_config_init(&p.v.config, sizeof(faar_config) + sizeof(p.tail))
           == FAAR_OK);
    assert(p.v.config.struct_size == sizeof(faar_config));
    assert(tail_untouched(&p));

    /* Exactly the baseline is accepted, and the size is written back. */
    memset(&p, 0xa5, sizeof(p));
    assert(faar_config_init(&p.v.config, FAAR_CONFIG_BASELINE) == FAAR_OK);
    assert(p.v.config.struct_size == FAAR_CONFIG_BASELINE);
    assert(tail_untouched(&p));

    /* open() reads only the bytes the caller's struct_size covers. */
    memset(&p, 0xa5, sizeof(p));
    assert(faar_config_init(&p.v.config, sizeof(faar_config)) == FAAR_OK);
    p.v.config.in_rate = 48000;
    p.v.config.out_rate = 44100;
    p.v.config.channels = 1;
    p.v.config.struct_size = sizeof(faar_config) + sizeof(p.tail);
    faar_resampler *r = NULL;
    assert(faar_resampler_open(&p.v.config, &r) == FAAR_OK && r);
    assert(faar_resampler_close(&r) == FAAR_OK);
    p.v.config.struct_size = (uint32_t)(FAAR_CONFIG_BASELINE - 1);
    r = (faar_resampler *)&p;
    assert(faar_resampler_open(&p.v.config, &r) == FAAR_ERR_INVALID_ARGUMENT);
    assert(r == NULL);

    /* Query structures: baseline minimum, larger accepted, size written back. */
    memset(&p, 0xa5, sizeof(p));
    p.v.library.struct_size = (uint32_t)(FAAR_LIBRARY_INFO_BASELINE - 1);
    assert(faar_get_library_info(&p.v.library) == FAAR_ERR_INVALID_ARGUMENT);
    p.v.library.struct_size = sizeof(faar_library_info) + sizeof(p.tail);
    assert(faar_get_library_info(&p.v.library) == FAAR_OK);
    assert(p.v.library.struct_size == sizeof(faar_library_info));
    assert(tail_untouched(&p));

    assert(faar_config_init(&config, sizeof(config)) == FAAR_OK);
    config.in_rate = 48000;
    config.out_rate = 44100;
    config.channels = 1;
    assert(faar_resampler_open(&config, &r) == FAAR_OK);
    memset(&p, 0xa5, sizeof(p));
    p.v.info.struct_size = (uint32_t)(FAAR_RESAMPLER_INFO_BASELINE - 1);
    assert(faar_resampler_get_info(r, &p.v.info) == FAAR_ERR_INVALID_ARGUMENT);
    p.v.info.struct_size = sizeof(faar_resampler_info) + sizeof(p.tail);
    assert(faar_resampler_get_info(r, &p.v.info) == FAAR_OK);
    assert(p.v.info.struct_size == sizeof(faar_resampler_info));
    assert(tail_untouched(&p));
    assert(faar_resampler_close(&r) == FAAR_OK);
}

static void test_input_bound(void) {
    faar_config config;
    float input[64] = {0}, output[256];
    size_t made = 7;
    faar_resampler *r = NULL;
    assert(faar_config_init(&config, sizeof(config)) == FAAR_OK);
    config.in_rate = 48000;
    config.out_rate = 44100;
    config.channels = 1;
    config.max_input_frames = 32;
    assert(faar_resampler_open(&config, &r) == FAAR_OK && r);
    assert(faar_resampler_process(r, input, 33, output, 256, &made)
           == FAAR_ERR_INVALID_ARGUMENT);
    assert(made == 0);
    assert(faar_resampler_process(r, input, 32, output, 256, &made) == FAAR_OK);
    assert(faar_resampler_close(&r) == FAAR_OK);
}

int main(void) {
    test_struct_size();
    test_input_bound();
    faar_config config;
    assert(faar_config_init(&config, sizeof(config)) == FAAR_OK);
    assert(config.quality == FAAR_QUALITY_GOOD);

    faar_library_info library = {.struct_size = sizeof(library)};
    assert(faar_get_library_info(&library) == FAAR_OK);
    assert(library.max_channels >= 1 && library.max_channels <= 8);
    /* Only the precision policy is reported; every preset is always built. */
    assert((library.flags & ~(uint32_t)FAAR_LIB_FLOAT_ONLY) == 0);
    assert(strcmp(library.version, FAAR_VERSION_STRING) == 0);

    config.in_rate = 48000;
    config.out_rate = 44100;
    config.channels = 1;
    faar_resampler *resampler = NULL;
    assert(faar_resampler_open(&config, &resampler) == FAAR_OK);
    assert(resampler != NULL);

    faar_resampler_info info = {.struct_size = sizeof(info)};
    assert(faar_resampler_get_info(resampler, &info) == FAAR_OK);
    assert(info.decimate_factor >= 1);
    {
        faar_config c2 = config;
        c2.in_rate = 96000;
        c2.out_rate = 44100;
        c2.decimate_factor = 2;
        faar_resampler *r2 = NULL;
        faar_resampler_info i2 = {.struct_size = sizeof(i2)};
        assert(faar_resampler_open(&c2, &r2) == FAAR_OK);
        assert(faar_resampler_get_info(r2, &i2) == FAAR_OK);
        assert(i2.decimate_factor == 2);
        assert(faar_resampler_close(&r2) == FAAR_OK);
    }
    assert(info.delay_frames > 0);

    float input[480] = {0};
    float output[512] = {0};
    size_t produced = 0;
    assert(faar_resampler_process(resampler, input, 480, output,
                                  512, &produced) == FAAR_OK);
    size_t total = produced;
    size_t flushed = 0;
    do {
        assert(faar_resampler_finalize(resampler, output + total,
                                    512 - total, &flushed) == FAAR_OK);
        total += flushed;
    } while (flushed != 0);
    assert(total == 441);

    assert(faar_resampler_process(resampler, input, 1, output, 1,
                                  &produced) == FAAR_ERR_INVALID_ARGUMENT);
    assert(produced == 0);
    assert(faar_resampler_close(&resampler) == FAAR_OK);
    assert(resampler == NULL);
    assert(faar_resampler_close(&resampler) == FAAR_OK);

    config.channels = library.max_channels + 1;
    assert(faar_resampler_open(&config, &resampler)
           == FAAR_ERR_INVALID_ARGUMENT);
    assert(resampler == NULL);

    /* Dither flags: none, or exactly one mode plus one depth. */
    config.channels = 1;
    static const uint32_t valid_flags[] = {
        0,
        FAAR_DITHER_TPDF | FAAR_DITHER_16,
        FAAR_DITHER_TPDF | FAAR_DITHER_24,
        FAAR_DITHER_SHAPED | FAAR_DITHER_16,
        FAAR_DITHER_SHAPED | FAAR_DITHER_24,
    };
    for (size_t i = 0; i < sizeof(valid_flags) / sizeof(valid_flags[0]); ++i) {
        config.flags = valid_flags[i];
        assert(faar_resampler_open(&config, &resampler) == FAAR_OK);
        assert(faar_resampler_close(&resampler) == FAAR_OK);
    }
    static const uint32_t invalid_flags[] = {
        FAAR_DITHER_TPDF,
        FAAR_DITHER_16,
        FAAR_DITHER_TPDF | FAAR_DITHER_SHAPED | FAAR_DITHER_16,
        FAAR_DITHER_TPDF | FAAR_DITHER_16 | FAAR_DITHER_24,
        FAAR_DITHER_16 | FAAR_DITHER_24,
        0x10,
        FAAR_DITHER_SHAPED | FAAR_DITHER_16 | 0x10,
    };
    for (size_t i = 0; i < sizeof(invalid_flags) / sizeof(invalid_flags[0]); ++i) {
        config.flags = invalid_flags[i];
        assert(faar_resampler_open(&config, &resampler)
               == FAAR_ERR_INVALID_ARGUMENT);
        assert(resampler == NULL);
        faar_design *design = NULL;
        assert(faar_design_open(&config, &design)
               == FAAR_ERR_INVALID_ARGUMENT);
        assert(design == NULL);
    }
    config.flags = 0;
    config.quality = FAAR_QUALITY_HIGH;
    assert(faar_resampler_open(&config, &resampler) == FAAR_OK);
    assert(resampler != NULL);
    assert(faar_resampler_close(&resampler) == FAAR_OK);
    assert(resampler == NULL);
    assert(strcmp(faar_strerror(FAAR_ERR_NO_MEMORY), "out of memory") == 0);
    return 0;
}
