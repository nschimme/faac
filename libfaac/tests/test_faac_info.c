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

/* The encoder info reports the resolved stream format and the ASC, the
 * parameter flags reject unknown bits, and the rate controls take a
 * whole-stream bitrate. */
#include <assert.h>
#include <stdlib.h>
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

/* Encode a few frames of 16-bit PCM and return the concatenated stream. */
static size_t encode_stream(uint32_t channels, enum faac_channel_layout layout,
                            const int16_t *pcm, uint32_t frames, uint8_t *out, size_t cap)
{
    faac_params p;
    faac_encoder *enc = NULL;
    faac_encoder_info info = { .struct_size = sizeof info };
    size_t total = 0;
    assert(faac_params_init(&p, sizeof p) == FAAC_OK);
    p.sample_rate = 48000;
    p.channels = channels;
    p.bit_rate = 64000 * channels;
    p.stream_format = FAAC_STREAM_ADTS;
    p.channel_layout = layout;
    assert(faac_encoder_open(&p, &enc) == FAAC_OK);
    assert(faac_encoder_get_info(enc, &info) == FAAC_OK);
    for (uint32_t done = 0; done <= frames; done += info.frame_samples) {
        uint32_t n = 0;
        uint32_t left = frames - done < info.frame_samples ? frames - done : info.frame_samples;
        assert(total + info.max_output_bytes <= cap);
        assert(faac_encoder_encode(enc, left ? pcm + (size_t)done * channels : NULL,
                                   left * channels, out + total, (uint32_t)(cap - total), &n) == FAAC_OK);
        total += n;
    }
    assert(faac_encoder_close(&enc) == FAAC_OK);
    return total;
}

/* WAVE order must encode exactly like the same samples laid out in AAC element order. */
static void test_layouts(void)
{
    /* For each channel count, the WAVE channel that feeds each AAC slot. */
    static const uint8_t aac_from_wav[][8] = {
        {2, 0, 1}, {2, 0, 1, 3}, {2, 0, 1, 3, 4}, {2, 0, 1, 4, 5, 3},
        {0}, {2, 0, 1, 6, 7, 4, 5, 3},
    };
    faac_library_info lib = { .struct_size = sizeof lib };
    assert(faac_get_library_info(&lib) == FAAC_OK);
    assert(lib.version && lib.version[0]);
    for (uint32_t ch = 3; ch <= 8 && ch <= lib.max_channels; ch++) {
        if (ch == 7) continue;
        const uint32_t frames = 4096;
        int16_t *wav = malloc((size_t)frames * ch * sizeof *wav);
        int16_t *aac = malloc((size_t)frames * ch * sizeof *aac);
        uint8_t *a = malloc(1 << 20), *b = malloc(1 << 20);
        assert(wav && aac && a && b);
        for (uint32_t i = 0; i < frames; i++)
            for (uint32_t c = 0; c < ch; c++)
                wav[(size_t)i * ch + c] = (int16_t)(((i * (37 + 11 * c)) % 20011) - 10005 + 300 * (int)c);
        for (uint32_t i = 0; i < frames; i++)
            for (uint32_t c = 0; c < ch; c++)
                aac[(size_t)i * ch + c] = wav[(size_t)i * ch + aac_from_wav[ch - 3][c]];
        size_t na = encode_stream(ch, FAAC_LAYOUT_WAV, wav, frames, a, 1 << 20);
        size_t nb = encode_stream(ch, FAAC_LAYOUT_AAC, aac, frames, b, 1 << 20);
        assert(na > 0 && na == nb && memcmp(a, b, na) == 0);
        /* A different order must not encode the same: the test would be vacuous. */
        size_t nc = encode_stream(ch, FAAC_LAYOUT_AAC, wav, frames, a, 1 << 20);
        assert(nc != nb || memcmp(a, b, nb) != 0);
        free(wav); free(aac); free(a); free(b);
    }
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
    test_layouts();

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
#if FAAC_ENCODER_SBR
    assert(open_with(&enc, FAAC_OBJ_HE_AAC_V1, FAAC_STREAM_ADTS_MPEG2, 0) == FAAC_OK);
    info.struct_size = sizeof info;
    assert(faac_encoder_get_info(enc, &info) == FAAC_OK);
    assert(info.stream_format == FAAC_STREAM_ADTS && info.asc_size > 0);
    assert(faac_encoder_close(&enc) == FAAC_OK);
#else
    /* An LC-only build refuses HE-AAC instead of opening something else. */
    assert(open_with(&enc, FAAC_OBJ_HE_AAC_V1, FAAC_STREAM_ADTS, 0) == FAAC_ERR_UNSUPPORTED && !enc);
#endif

    /* Unknown flag bits and layouts are refused. */
    assert(open_with(&enc, FAAC_OBJ_LC, FAAC_STREAM_ADTS, 4) == FAAC_ERR_INVALID_ARGUMENT && !enc);
    {
        faac_params p;
        assert(faac_params_init(&p, sizeof p) == FAAC_OK);
        p.sample_rate = 44100;
        p.channels = 2;
        p.channel_layout = (enum faac_channel_layout)2;
        assert(faac_encoder_open(&p, &enc) == FAAC_ERR_INVALID_ARGUMENT && !enc);
    }

    /* ABR and CBR take a whole-stream rate and floor it per channel; zero means
     * 64 kbit/s for every channel. */
    {
        faac_library_info lib = { .struct_size = sizeof lib };
        assert(faac_get_library_info(&lib) == FAAC_OK);
        const uint32_t ch = lib.max_channels >= 3 ? 3 : lib.max_channels;
        faac_params p;
        assert(faac_params_init(&p, sizeof p) == FAAC_OK);
        p.sample_rate = 48000;
        p.channels = ch;
        p.stream_format = FAAC_STREAM_ADTS;
        assert(p.rate_control == FAAC_RC_ABR && p.bit_rate == 0 && p.quality == 0);
        assert(faac_encoder_open(&p, &enc) == FAAC_OK);
        info.struct_size = sizeof info;
        assert(faac_encoder_get_info(enc, &info) == FAAC_OK);
        assert(info.rate_control == FAAC_RC_ABR && info.bit_rate == ch * 64000);
        assert(faac_encoder_close(&enc) == FAAC_OK);

        p.bit_rate = 97000; /* floored per channel: 96999 in all for three */
        p.rate_control = FAAC_RC_CBR;
        assert(faac_encoder_open(&p, &enc) == FAAC_OK);
        assert(faac_encoder_get_info(enc, &info) == FAAC_OK);
        assert(info.rate_control == FAAC_RC_CBR && info.bit_rate == 97000 / ch * ch);
        assert(faac_encoder_close(&enc) == FAAC_OK);

        /* A quality only means something to VBR, and VBR takes no bitrate. */
        p.quality = 100;
        assert(faac_encoder_open(&p, &enc) == FAAC_ERR_INVALID_ARGUMENT && !enc);
        p.rate_control = FAAC_RC_ABR;
        assert(faac_encoder_open(&p, &enc) == FAAC_ERR_INVALID_ARGUMENT && !enc);
        p.rate_control = FAAC_RC_VBR;
        assert(faac_encoder_open(&p, &enc) == FAAC_ERR_INVALID_ARGUMENT && !enc); /* bit_rate set */
        p.bit_rate = 0;
        assert(faac_encoder_open(&p, &enc) == FAAC_OK);
        assert(faac_encoder_get_info(enc, &info) == FAAC_OK);
        assert(info.rate_control == FAAC_RC_VBR && info.bit_rate == 0 && info.quality == 100);
        assert(faac_encoder_close(&enc) == FAAC_OK);
        p.quality = 5001;
        assert(faac_encoder_open(&p, &enc) == FAAC_ERR_INVALID_ARGUMENT && !enc);

        /* The peak cap is a whole-stream rate too and cannot undercut the average. */
        p.rate_control = FAAC_RC_ABR;
        p.quality = 0;
        p.bit_rate = 192000;
        p.peak_bit_rate = 191999;
        assert(faac_encoder_open(&p, &enc) == FAAC_ERR_INVALID_ARGUMENT && !enc);
        p.peak_bit_rate = 192000;
        assert(faac_encoder_open(&p, &enc) == FAAC_OK);
        assert(faac_encoder_close(&enc) == FAAC_OK);
    }
    return 0;
}
