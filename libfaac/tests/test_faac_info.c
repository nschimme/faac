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

/* The encoder info reports the resolved stream format and the ASC, and the
 * parameter flags reject unknown bits. */
#include <assert.h>
#include <string.h>
#include "faac.h"

static faac_status open_with(faac_encoder **enc, enum faac_object_type object,
                             enum faac_stream_format format, uint32_t flags)
{
    faac_params p;
    assert(faac_params_init(&p, sizeof p) == FAAC_OK);
    p.sample_rate = 44100;
    p.channels = 2;
    p.object_type = object;
    p.stream_format = format;
    p.flags = flags;
    p.bit_rate = 48000;
    return faac_encoder_open(&p, enc);
}

static int stereo_available(void)
{
    faac_library_info lib = { .struct_size = sizeof lib };
    return faac_get_library_info(&lib) == FAAC_OK && lib.max_channels >= 2;
}

int main(void)
{
    if (!stereo_available())
        return 0; /* every case below encodes stereo */
    faac_encoder *enc = NULL;
    faac_encoder_info info;
    const uint8_t *asc;
    uint32_t len;

    /* The info ASC is the one faac_encoder_asc() returns; the rest is zero. */
    assert(open_with(&enc, FAAC_OBJ_LC, FAAC_STREAM_RAW, FAAC_FLAG_TNS | FAAC_FLAG_PNS) == FAAC_OK);
    memset(&info, 0xA5, sizeof info);
    info.struct_size = sizeof info;
    assert(faac_encoder_get_info(enc, &info) == FAAC_OK);
    assert(faac_encoder_asc(enc, &asc, &len) == FAAC_OK);
    assert(info.stream_format == FAAC_STREAM_RAW);
    assert(info.asc_size == len && len >= 2 && len <= sizeof info.asc);
    assert(memcmp(info.asc, asc, len) == 0);
    for (uint32_t i = len; i < sizeof info.asc; i++)
        assert(info.asc[i] == 0);
    assert(faac_encoder_close(&enc) == FAAC_OK);

    /* A baseline-sized info is filled up to its end and not past it. */
    assert(open_with(&enc, FAAC_OBJ_LC, FAAC_STREAM_ADTS, 0) == FAAC_OK);
    memset(&info, 0xA5, sizeof info);
    info.struct_size = FAAC_ENCODER_INFO_BASELINE;
    assert(faac_encoder_get_info(enc, &info) == FAAC_OK);
    assert(info.struct_size == FAAC_ENCODER_INFO_BASELINE);
    assert(info.stream_format == FAAC_STREAM_ADTS);
    assert(((const uint8_t *)&info)[FAAC_ENCODER_INFO_BASELINE] == 0xA5);
    info.struct_size = FAAC_ENCODER_INFO_BASELINE - 1;
    assert(faac_encoder_get_info(enc, &info) == FAAC_ERR_INVALID_ARGUMENT);
    assert(faac_encoder_close(&enc) == FAAC_OK);

    /* MPEG-2 ADTS has no ASC; HE-AAC overrides it with MPEG-4. */
    assert(open_with(&enc, FAAC_OBJ_LC, FAAC_STREAM_ADTS_MPEG2, 0) == FAAC_OK);
    info.struct_size = sizeof info;
    assert(faac_encoder_get_info(enc, &info) == FAAC_OK);
    assert(info.stream_format == FAAC_STREAM_ADTS_MPEG2 && info.asc_size == 0);
    assert(faac_encoder_asc(enc, &asc, &len) == FAAC_ERR_UNSUPPORTED);
    assert(faac_encoder_close(&enc) == FAAC_OK);
    assert(open_with(&enc, FAAC_OBJ_HE_AAC_V1, FAAC_STREAM_ADTS_MPEG2, 0) == FAAC_OK);
    info.struct_size = sizeof info;
    assert(faac_encoder_get_info(enc, &info) == FAAC_OK);
    assert(info.stream_format == FAAC_STREAM_ADTS && info.asc_size > 0);
    assert(faac_encoder_close(&enc) == FAAC_OK);

    /* Unknown flag bits and nonzero reserved bytes are refused. */
    assert(open_with(&enc, FAAC_OBJ_LC, FAAC_STREAM_ADTS, 4) == FAAC_ERR_INVALID_ARGUMENT && !enc);
    {
        faac_params p;
        assert(faac_params_init(&p, sizeof p) == FAAC_OK);
        p.sample_rate = 44100;
        p.channels = 2;
        p.reserved[2] = 1;
        assert(faac_encoder_open(&p, &enc) == FAAC_ERR_INVALID_ARGUMENT && !enc);
    }
    return 0;
}
