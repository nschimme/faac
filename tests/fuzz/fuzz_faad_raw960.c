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

/*
 * libFuzzer harness for libfaad's RAW 960-sample (DAB+) decoder API.
 * Input: selector, then little-endian u16 length-prefixed access units.
 * Selector bit 7: instead read a u8 ASC length, ASC bytes, then access units.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "faad3.h"

/* ISO/IEC 14496-3 AudioSpecificConfig: audioObjectType(5),
 * samplingFrequencyIndex(4), channelConfiguration(4). AOT 5/29 additionally
 * has extensionSamplingFrequencyIndex(4), core audioObjectType(5).
 * GASpecificConfig starts with frameLengthFlag=1, dependsOnCoreCoder=0,
 * extensionFlag=0 (100); unused trailing bits are zero.
 * HE uses a 24 kHz core and 48 kHz output; PS uses a mono core.
 */
static const uint8_t asc[][11] = {
    { 0x12, 0x8c },                         /* LC, 32 kHz, mono */
    { 0x11, 0x94 },                         /* LC, 48 kHz, stereo */
    { 0x2b, 0x11, 0x8a, 0x00 },             /* SBR, 24 -> 48 kHz, stereo */
    { 0xeb, 0x09, 0x8a, 0x00 },             /* PS, 24 -> 48 kHz, mono core */
    { 0x11, 0x80, 0x04, 0xc8, 0x09, 0x00,   /* LC, 48 kHz, 7.1 signalled by a PCE */
      0x01, 0x08, 0xc8, 0x00, 0x00 },
};
static const uint32_t asc_len[] = { 2, 2, 4, 4, 11 };

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 2)
        return 0;

    faad_library_info lib;
    memset(&lib, 0, sizeof(lib));
    lib.struct_size = sizeof(lib);
    if (faad_get_library_info(&lib) != FAAD_OK || lib.struct_size < sizeof(lib))
        __builtin_trap();
    if (lib.flags & ~(FAAD_LIB_SBR | FAAD_LIB_PS))
        __builtin_trap();

    faad_config cfg;
    if (faad_config_init(&cfg, sizeof(cfg)) != FAAD_OK)
        __builtin_trap();
    cfg.stream_format = FAAD_STREAM_RAW;
    cfg.output_format = (enum faad_output_format)(1 + ((data[0] >> 2) & 3));
    cfg.downmix_mode = (enum faad_downmix_mode)((data[0] >> 4) & 3);

    uint8_t mode = data[0];
    uint32_t asc_index = mode % (sizeof(asc) / sizeof(asc[0]));
    const uint8_t *config = asc[asc_index];
    uint32_t config_len = asc_len[asc_index];
    size_t pos = 1;

    if (mode & 0x80) {
        config_len = data[pos++];
        if (config_len > size - pos)
            return 0;
        config = data + pos;
        pos += config_len;
    }

    if (mode & 0x40) {
        if (size - pos < 3)
            return 0;
        cfg.drc_cut = data[pos++] & 0x7f;
        cfg.drc_boost = data[pos++] & 0x7f;
        cfg.drc_target_level = data[pos++];
    }

    faad_decoder *dec = NULL;
    if (faad_decoder_open(&cfg, config, config_len, &dec) != FAAD_OK || !dec)
        return 0;

    faad_stream_info info;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    if (faad_decoder_get_info(dec, &info) != FAAD_OK || info.struct_size < sizeof(info))
        __builtin_trap();

    uint32_t pcm_capacity = info.max_output_bytes;
    if (pcm_capacity < 16384)
        pcm_capacity = 16384;
    uint8_t *pcm = malloc(pcm_capacity);
    if (!pcm) {
        faad_decoder_close(&dec);
        return 0;
    }
    int frames = 0;
    int decoded = 0;

    while (size - pos >= 2 && frames < 128) {
        uint32_t len = (uint32_t)data[pos] | ((uint32_t)data[pos + 1] << 8);
        pos += 2;
        if (len > size - pos)
            break;

        uint32_t consumed = 0;
        uint32_t written = 0;
        uint32_t flags = 0;
        faad_status st = faad_decode_frame(dec, data + pos, len,
                               &consumed, pcm, pcm_capacity, &written, &flags);

        if (written > pcm_capacity)
            __builtin_trap();
        if (st == FAAD_OK)
            decoded++;
        if (flags & FAAD_FRAME_FORMAT_CHANGED) {
            info.struct_size = sizeof(info);
            (void)faad_decoder_get_info(dec, &info);
        }

        if (st != FAAD_OK && st != FAAD_ERR_NEED_MORE_DATA && st != FAAD_ERR_SYNC_LOST && st != FAAD_ERR_DECODE_FAILED)
            break;

        pos += len;
        frames++;
        if (frames % 32 == 0)
            (void)faad_decoder_flush(dec);
    }

    info.struct_size = sizeof(info);
    if (faad_decoder_get_info(dec, &info) != FAAD_OK)
        __builtin_trap();
    if (info.flags & ~(FAAD_STREAM_FORMAT_KNOWN | FAAD_STREAM_PROG_REF_LEVEL))
        __builtin_trap();
    if (decoded && !(info.flags & FAAD_STREAM_FORMAT_KNOWN))
        __builtin_trap();
    if (info.channels > lib.max_channels || info.channels > 8)
        __builtin_trap();

    free(pcm);
    faad_decoder_close(&dec);
    return 0;
}
