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
 * libFuzzer harness for the public encoder API.
 *
 * The input is a small header that picks the faac_params (including values the
 * validator must reject), then a stream of PCM chunks whose sizes and contents
 * come from the remaining bytes. Float input carries whatever bit patterns the
 * fuzzer produces, NaN/Inf included.
 *
 * Build and run with tests/fuzz/run.sh.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "faac.h"
#include "asc_codec.h"

#define MAX_FRAME_SAMPLES (2048 * 8)
#define MAX_CALLS 64

static const uint32_t rates[] = {
    8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000, 64000, 88200, 96000
};

typedef struct {
    const uint8_t *p;
    size_t n;
} cursor;

static uint32_t take(cursor *c, unsigned bytes)
{
    uint32_t v = 0;
    while (bytes--) {
        v = (v << 8) | (c->n ? *c->p : 0);
        if (c->n) { c->p++; c->n--; }
    }
    return v;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    cursor c = { data, size };
    faac_params p;
    faac_encoder *enc = NULL;
    faac_encoder_info info;
    faac_library_info lib;
    uint8_t *out;
    static uint8_t pcm[MAX_FRAME_SAMPLES * sizeof(int32_t)];
    const uint8_t *asc;
    uint32_t asc_len, w, flags, sel, i;

    if (size < 24)
        return 0;

    memset(&lib, 0, sizeof(lib));
    lib.struct_size = sizeof(lib);
    if (faac_get_library_info(&lib) != FAAC_OK || lib.struct_size < sizeof(lib))
        __builtin_trap();
    if (lib.flags & ~(FAAC_LIB_SBR | FAAC_LIB_PS))
        __builtin_trap();

    /* A truncated struct cannot describe the baseline layout and must be rejected. */
    if (data[0] & 0x80) {
        faac_params bad;
        memset(&bad, 0xa5, sizeof(bad));
        if (faac_params_init(&bad, 0) != FAAC_ERR_INVALID_ARGUMENT)
            __builtin_trap();
    }

    if (faac_params_init(&p, sizeof(p)) != FAAC_OK)
        __builtin_trap();
    flags = take(&c, 4);
    sel = take(&c, 1);
    p.sample_rate  = (sel & 0x80) ? take(&c, 4) : rates[sel % (sizeof(rates) / sizeof(rates[0]))];
    {
        /* Stay inside the build's channel ceiling so most inputs reach encode();
         * one flag bit asks for one channel over it to exercise rejection. */
        uint32_t max_ch = lib.max_channels ? lib.max_channels : 8;
        p.channels = 1 + take(&c, 1) % max_ch;
        if ((flags >> 29) & 1)
            p.channels = max_ch + 1;
    }
    {
        uint32_t ot = flags >> 2 & 3;
        bool sbr = (lib.flags & FAAC_LIB_SBR) != 0;
        bool ps = (lib.flags & FAAC_LIB_PS) != 0;
        p.object_type = ot == 0 ? FAAC_OBJ_AUTO :
                        ot == 1 ? FAAC_OBJ_LC :
                        ot == 2 ? (sbr ? FAAC_OBJ_HE_AAC_V1 : FAAC_OBJ_LC) :
                                  (ps ? FAAC_OBJ_HE_AAC_V2 : FAAC_OBJ_LC);
        /* Keep the unsupported-profile rejection path reachable in either build. */
        if (((flags >> 30) & 1) && !(ot == 3 ? ps : (ot == 2 ? sbr : false)))
            p.object_type = ot == 3 ? FAAC_OBJ_HE_AAC_V2 : FAAC_OBJ_HE_AAC_V1;
    }
    p.stereo_mode   = (enum faac_stereo_mode)(flags >> 4 & 7);
    p.flags         = ((flags >> 8 & 1) ? FAAC_FLAG_TNS : 0)
                    | ((flags >> 9 & 1) ? FAAC_FLAG_PNS : 0);
    if ((flags >> 24 & 0xf) == 15)
        p.flags |= 0x80000000u;            /* unknown bit: open() must reject */

    /* Bits 10-11 pick the new stream format directly; the old mpeg_version
     * bit upgrades ADTS to FAAC_STREAM_ADTS_MPEG2. */
    p.stream_format = (enum faac_stream_format)(flags >> 10 & 3);
    if (p.stream_format == FAAC_STREAM_ADTS && (flags & 1))
        p.stream_format = FAAC_STREAM_ADTS_MPEG2;
    p.input_format  = (enum faac_input_format)(flags >> 12 & 7);
    p.short_control = (enum faac_shortctl_mode)(flags >> 15 & 3);

    p.bit_rate      = (flags >> 20 & 1) ? take(&c, 4) : take(&c, 2) * 4;
    p.bandwidth     = (flags >> 21 & 1) ? take(&c, 4) : take(&c, 2);
    p.quality       = (flags >> 22 & 1) ? take(&c, 4) : take(&c, 2) % 6000;
    p.peak_bit_rate = (flags >> 23 & 1) ? take(&c, 4) : 0;

    p.rate_control  = (enum faac_rate_control)(flags >> 17 & 7);

    /* SONAME 2 use_lfe and channel_map folded into channel_layout. The old
     * flag bits select AAC, WAV, or an invalid layout for rejection coverage;
     * LFE itself is automatic for six and eight channels. */
    p.channel_layout = (enum faac_channel_layout)(((flags >> 7) & 1) | ((flags >> 28 & 1) << 1));

    if (faac_encoder_open(&p, &enc) != FAAC_OK)
        return 0;

    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    if (faac_encoder_get_info(enc, &info) != FAAC_OK || !info.frame_samples) {
        __builtin_trap();
    }
    if (info.struct_size < sizeof(info) || info.max_output_bytes < 64) {
        __builtin_trap();
    }

    /* The ASC is built at open; the getter must agree with the info snapshot. */
    if (faac_encoder_asc(enc, &asc, &asc_len) == FAAC_OK) {
        if (asc_len != info.asc_size || (asc_len && memcmp(asc, info.asc, asc_len) != 0))
            __builtin_trap();
        /* AAC-LC ASCs carry the channel count directly, or in a PCE for 8 ch. */
        if (info.object_type == FAAC_OBJ_LC && info.asc_size) {
            AscInfo ai;
            asc_codec_parse(info.asc, info.asc_size, &ai);
            if (ai.num_channels != p.channels)
                __builtin_trap();
            if (p.channels == 8) {
                if (ai.channel_config != 0 || !ai.pce.wave71 ||
                    ai.pce.channels != 8 || info.asc_size > sizeof(info.asc))
                    __builtin_trap();
            } else if (ai.channel_config == 0) {
                __builtin_trap();
            }
        }
    } else if (info.stream_format != FAAC_STREAM_ADTS_MPEG2 || info.asc_size != 0) {
        __builtin_trap();
    }

    out = malloc(info.max_output_bytes);
    if (!out) {
        faac_encoder_close(&enc);
        return 0;
    }

    {
        unsigned bps = p.input_format == FAAC_INPUT_16BIT ? 2 :
                       p.input_format == FAAC_INPUT_24BIT ? 3 : 4;
        uint32_t cap = info.frame_samples * p.channels;
        uint32_t out_cap = info.max_output_bytes;
        int calls;

        for (calls = 0; calls < MAX_CALLS && c.n; calls++) {
            uint32_t n = take(&c, 2);
            size_t bytes;
            /* mostly full frames, sometimes ragged, sometimes over the limit */
            n = (n & 0xc000) == 0xc000 ? n & 0x3fff : (n % (cap + 1));
            if (n > cap + 8)
                n = cap + 8;
            bytes = (size_t)n * bps;
            if (bytes > sizeof(pcm))
                bytes = sizeof(pcm), n = (uint32_t)(bytes / bps);
            memset(pcm, 0, bytes);
            if (c.n) {
                size_t k = c.n < bytes ? c.n : bytes;
                /* repeat the available seed bytes so short inputs still fill frames */
                for (i = 0; i < bytes; i++)
                    pcm[i] = c.p[i % k];
                c.p += k < c.n ? k : c.n;
                c.n -= k < c.n ? k : c.n;
            }
            /* Every other call deliberately undersizes the buffer. */
            out_cap = ((calls & 1) && info.max_output_bytes > 1)
                          ? info.max_output_bytes / 2 : info.max_output_bytes;
            (void)faac_encoder_encode(enc, pcm, n, out, out_cap, &w);
            if (w > out_cap)
                __builtin_trap();
        }
        for (i = 0; i < 8; i++) {
            if (faac_encoder_encode(enc, NULL, 0, out, info.max_output_bytes, &w) != FAAC_OK || !w)
                break;
            if (w > info.max_output_bytes)
                __builtin_trap();
        }
    }
    free(out);
    faac_encoder_close(&enc);
    return 0;
}
