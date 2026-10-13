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

#include "adts.h"

bool adts_header_ok(const uint8_t *h)
{
    return h[0] == 0xFF && (h[1] & 0xF6) == 0xF0 && ((h[2] >> 2) & 0x0F) <= 12;
}

uint32_t adts_frame_length(const uint8_t *h)
{
    return ((uint32_t)(h[3] & 0x03) << 11) | ((uint32_t)h[4] << 3) | ((uint32_t)(h[5] & 0xE0) >> 5);
}

uint32_t adts_header_length(const uint8_t *h)
{
    return (h[1] & 0x01) ? 7 : 9; /* protection_absent == 0 adds a CRC */
}

void adts_parse_header(const uint8_t *h, adts_params *out)
{
    out->profile = (uint8_t)((h[2] & 0xC0) >> 6);
    out->sr_idx = (uint8_t)((h[2] & 0x3C) >> 2);
    out->channel_cfg = (uint8_t)(((h[2] & 0x01) << 2) | ((h[3] & 0xC0) >> 6));
    out->raw_blocks = h[6] & 0x03;
}

const char *adts_params_from_asc(const AscInfo *asc, bool lenient, adts_params *out)
{
    int sr_idx = asc_codec_sr_idx(asc->sample_rate);
    if (sr_idx < 0)
        return "its core sample rate has no ADTS sampling frequency index";

    if (lenient) {
        out->profile = (asc->object_type >= 1 && asc->object_type <= 4) ? (uint8_t)(asc->object_type - 1) : 1;
        out->channel_cfg = asc->num_channels & 7;
    } else {
        /* The 2-bit profile names Main, LC, SSR and LTP; any other object type has no ADTS form. */
        if (asc->object_type < 1 || asc->object_type > 4)
            return "its AAC object type has no ADTS profile";
        /* 0 would put the layout in a program config element inside the frames; 8 and up are reserved. */
        if (asc->num_channels < 1 || asc->num_channels > 7)
            return "its channel configuration cannot be written in an ADTS header";
        out->profile = (uint8_t)(asc->object_type - 1);
        out->channel_cfg = asc->num_channels;
    }
    out->sr_idx = (uint8_t)sr_idx;
    out->raw_blocks = 0;
    return NULL;
}

void adts_write_header(const adts_params *p, uint32_t payload_bytes, uint8_t hdr[ADTS_HEADER_SIZE])
{
    uint32_t len = payload_bytes + ADTS_HEADER_SIZE;
    hdr[0] = 0xFF;
    hdr[1] = 0xF1; /* MPEG-4, layer 0, no CRC */
    hdr[2] = (uint8_t)((p->profile << 6) | (p->sr_idx << 2) | ((p->channel_cfg >> 2) & 1));
    hdr[3] = (uint8_t)(((p->channel_cfg & 3) << 6) | ((len >> 11) & 0x03));
    hdr[4] = (uint8_t)((len >> 3) & 0xFF);
    hdr[5] = (uint8_t)(((len & 7) << 5) | 0x1F); /* buffer fullness: VBR */
    hdr[6] = (uint8_t)(0xFC | p->raw_blocks);
}
