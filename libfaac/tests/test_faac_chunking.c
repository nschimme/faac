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

/* The encoder buffers input internally, so the bytes it emits must not depend on how
 * the caller slices the PCM across faac_encoder_encode() calls (documented in faac.h:
 * any count from 0 to frame_samples*channels). The same signal fed in different call
 * sizes, then flushed, must produce a byte-identical stream. */
#include <stdio.h>
#include "faac.h"
#include "test_faac_pcm.h"

typedef struct { const char *name; enum faac_object_type object; uint32_t rate, channels, bit_rate; } config;

/* Returns the stream length; *out is heap-allocated. */
static size_t encode_chunked(const config *cfg, const int16_t *pcm, uint32_t frames, uint32_t chunk, uint8_t **out)
{
    faac_params p;
    faac_encoder *enc = NULL;
    faac_encoder_info info = { .struct_size = sizeof(info) };
    FT_REQUIRE(faac_params_init(&p, sizeof(p)) == FAAC_OK);
    p.sample_rate = cfg->rate;
    p.channels = cfg->channels;
    p.object_type = cfg->object;
    p.bit_rate = cfg->bit_rate;
    p.stream_format = FAAC_STREAM_ADTS;
    p.input_format = FAAC_INPUT_16BIT;
    FT_REQUIRE(faac_encoder_open(&p, &enc) == FAAC_OK);
    FT_REQUIRE(faac_encoder_get_info(enc, &info) == FAAC_OK);

    /* encode() refuses an output buffer below one maximal ADTS frame, so keep that much spare. */
    size_t cap = (size_t)info.max_output_bytes * (frames / info.frame_samples + 8) + 8192, len = 0;
    uint8_t *stream = (uint8_t *)malloc(cap);
    FT_REQUIRE(stream);
    if (chunk > info.frame_samples) chunk = info.frame_samples;

    for (uint32_t done = 0; done < frames;) {
        uint32_t n = frames - done < chunk ? frames - done : chunk;
        uint32_t got = 0;
        FT_REQUIRE(faac_encoder_encode(enc, pcm + (size_t)done * cfg->channels, n * cfg->channels,
                                       stream + len, (uint32_t)(cap - len), &got) == FAAC_OK);
        len += got;
        done += n;
    }
    for (uint32_t got = 1; got;) {
        FT_REQUIRE(faac_encoder_encode(enc, NULL, 0, stream + len, (uint32_t)(cap - len), &got) == FAAC_OK);
        len += got;
    }
    FT_REQUIRE(faac_encoder_close(&enc) == FAAC_OK);
    *out = stream;
    return len;
}

int main(void)
{
    faac_library_info lib = { .struct_size = sizeof(lib) };
    FT_REQUIRE(faac_get_library_info(&lib) == FAAC_OK);
    static const config configs[] = {
        { "LC stereo 44.1k", FAAC_OBJ_LC, 44100, 2, 64000 },
        { "LC mono 48k", FAAC_OBJ_LC, 48000, 1, 48000 },
        { "HE stereo 48k", FAAC_OBJ_HE_AAC_V1, 48000, 2, 32000 },
    };
    static const uint32_t chunks[] = { 1, 17, 333, 1024, 2048 };

    for (size_t c = 0; c < sizeof(configs) / sizeof(configs[0]); c++) {
        const config *cfg = &configs[c];
        if (cfg->channels > lib.max_channels) continue; /* build has fewer channels */
        uint32_t frames = cfg->rate * 3 / 2 + 5; /* not a multiple of any chunk size */
        int16_t *pcm = pcm_make(cfg->rate, cfg->channels, frames);
        uint8_t *ref = NULL;
        size_t ref_len = encode_chunked(cfg, pcm, frames, 1024, &ref);
        FT_CHECK(ref_len > 0);
        for (size_t k = 0; k < sizeof(chunks) / sizeof(chunks[0]); k++) {
            uint8_t *got = NULL;
            size_t len = encode_chunked(cfg, pcm, frames, chunks[k], &got);
            if (len != ref_len) fprintf(stderr, "%s, chunk %u: ", cfg->name, chunks[k]);
            FT_CHECK_INT(len, ref_len);
            if (len == ref_len) {
                if (memcmp(got, ref, len)) fprintf(stderr, "%s, chunk %u: ", cfg->name, chunks[k]);
                FT_CHECK_MEM(got, ref, len);
            }
            free(got);
        }
        free(ref);
        free(pcm);
    }
    return ft_done();
}
