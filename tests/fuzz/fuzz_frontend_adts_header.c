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

/*
 * libFuzzer harness for frontend/adts.c ADTS header validation and adts_params_from_asc().
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "adts.h"
#include "asc_codec.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 2)
        return 0;

    /* 1. ADTS header parsing and validation */
    if (size >= ADTS_HEADER_SIZE) {
        if (adts_header_ok(data)) {
            uint32_t flen = adts_frame_length(data);
            uint32_t hlen = adts_header_length(data);
            (void)flen;
            (void)hlen;

            adts_params p;
            adts_parse_header(data, &p);

            uint8_t out_hdr[ADTS_HEADER_SIZE];
            adts_write_header(&p, 100, out_hdr);
        }
    }

    /* 2. AudioSpecificConfig parsing and adts_params_from_asc */
    AscInfo asc;
    asc_codec_parse(data, (uint32_t)size, &asc);

    adts_params p_strict, p_lenient;
    const char *err_strict = adts_params_from_asc(&asc, false, &p_strict);
    const char *err_lenient = adts_params_from_asc(&asc, true, &p_lenient);

    if (!err_strict) {
        uint8_t hdr[ADTS_HEADER_SIZE];
        adts_write_header(&p_strict, 200, hdr);
    }
    if (!err_lenient) {
        uint8_t hdr[ADTS_HEADER_SIZE];
        adts_write_header(&p_lenient, 200, hdr);
    }

    return 0;
}
