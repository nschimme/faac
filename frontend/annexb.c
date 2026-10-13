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

#include <stdlib.h>
#include <string.h>

#include "annexb.h"
#include "endian.h"

/* Finds the next NAL unit at or after *pos. nal is its first byte (the header), without the
   start code and without the zero bytes that pad it before the next one. *pos moves to the
   next start code, so repeated calls walk the stream. Returns false when none is left. */
static bool next_nal(const uint8_t *d, size_t len, size_t *pos, size_t *nal, size_t *nal_len)
{
    size_t p = *pos;

    /* A byte above 1 at p+2 rules out a start code at p, p+1 and p+2. */
    while (p + 3 <= len) {
        if (d[p + 2] > 1) p += 3;
        else if (d[p] == 0 && d[p + 1] == 0 && d[p + 2] == 1) break;
        else p++;
    }
    if (p + 3 > len) {
        *pos = len;
        return false;
    }

    size_t start = p + 3, end = start;
    while (end + 3 <= len) {
        if (d[end + 2] > 1) end += 3;
        else if (d[end] == 0 && d[end + 1] == 0 && d[end + 2] == 1) break;
        else end++;
    }
    if (end + 3 > len) end = len;

    /* A NAL unit never ends in a zero byte; those belong to the longer start code or padding. */
    size_t last = end;
    while (last > start && d[last - 1] == 0) last--;
    *nal = start;
    *nal_len = last - start;
    *pos = end;
    return true;
}

/* Removes every emulation-prevention byte (0x03 after two zero bytes). Only for reading the
   bit fields of an SPS; the configuration record stores the NAL units as they are. */
static uint32_t rbsp_strip_epb(const uint8_t *nal, uint32_t nal_len, uint8_t *out, uint32_t out_cap)
{
    uint32_t o = 0;
    int zero_run = 0;
    for (uint32_t i = 0; i < nal_len && o < out_cap; i++) {
        if (zero_run >= 2 && nal[i] == 3) {
            zero_run = 0;
            continue;
        }
        out[o++] = nal[i];
        zero_run = (nal[i] == 0) ? zero_run + 1 : 0;
    }
    return o;
}

typedef struct {
    const uint8_t *data;
    size_t len;
} nal_ref;

static const char *build_avcc(const nal_ref *sps, const nal_ref *pps, uint8_t *out, uint32_t out_cap, uint32_t *out_len)
{
    if (!sps->data) return "no SPS in the stream";
    if (!pps->data) return "no PPS in the stream";
    if (sps->len < 4) return "the SPS is too short";
    if (sps->len > 0xFFFF || pps->len > 0xFFFF || 11 + sps->len + pps->len > out_cap)
        return "the SPS and PPS are too large for the codec configuration";

    uint32_t o = 0;
    out[o++] = 1;            /* configurationVersion */
    out[o++] = sps->data[1]; /* AVCProfileIndication */
    out[o++] = sps->data[2]; /* profile_compatibility */
    out[o++] = sps->data[3]; /* AVCLevelIndication */
    out[o++] = 0xFF;         /* lengthSizeMinusOne = 3 (4 bytes) */
    out[o++] = 0xE1;         /* numOfSequenceParameterSets = 1 */
    uint16_t be = htobe16((uint16_t)sps->len);
    memcpy(out + o, &be, 2); o += 2;
    memcpy(out + o, sps->data, sps->len); o += (uint32_t)sps->len;
    out[o++] = 1;            /* numOfPictureParameterSets = 1 */
    be = htobe16((uint16_t)pps->len);
    memcpy(out + o, &be, 2); o += 2;
    memcpy(out + o, pps->data, pps->len); o += (uint32_t)pps->len;
    *out_len = o;
    return NULL;
}

/* HEVCDecoderConfigurationRecord (ISO/IEC 14496-15) from the raw VPS, SPS and PPS NAL units.
 *
 * Only the SPS's general profile_tier_level() is read (ISO/IEC 23008-2, 7.3.2.2 and 7.3.3):
 * its first 12 bytes sit at a fixed RBSP offset behind the one-byte
 * sps_video_parameter_set_id / sps_max_sub_layers_minus1 / sps_temporal_id_nesting_flag
 * header, whatever the sub-layer count. Chroma format, bit depths, min_spatial_segmentation_idc
 * and parallelismType are not parsed; they get the neutral values (4:2:0, 8-bit, 0, unknown),
 * which decoders treat as advisory. */
static const char *build_hvcc(const nal_ref *vps, const nal_ref *sps, const nal_ref *pps,
                              uint8_t *out, uint32_t out_cap, uint32_t *out_len)
{
    if (!vps->data) return "no VPS in the stream";
    if (!sps->data) return "no SPS in the stream";
    if (!pps->data) return "no PPS in the stream";
    if (sps->len < 15) return "the SPS is too short";

    uint8_t rbsp[256];
    uint32_t strip_in = sps->len > sizeof(rbsp) ? (uint32_t)sizeof(rbsp) : (uint32_t)sps->len;
    uint32_t rbsp_len = rbsp_strip_epb(sps->data, strip_in, rbsp, sizeof(rbsp));
    if (rbsp_len < 15) return "the SPS is too short"; /* 2 (NAL header) + 1 + 12 (general PTL) */

    if (vps->len > 0xFFFF || sps->len > 0xFFFF || pps->len > 0xFFFF ||
        23 + 5 + vps->len + 5 + sps->len + 5 + pps->len > out_cap)
        return "the VPS, SPS and PPS are too large for the codec configuration";

    uint8_t general_profile_space = (rbsp[3] >> 6) & 0x03;
    uint8_t general_tier_flag = (rbsp[3] >> 5) & 0x01;
    uint8_t general_profile_idc = rbsp[3] & 0x1F;
    const uint8_t *compat_flags = rbsp + 4;     /* 4 bytes */
    const uint8_t *constraint_flags = rbsp + 8; /* 6 bytes */
    uint8_t general_level_idc = rbsp[14];

    uint8_t sps_max_sub_layers_minus1 = (rbsp[2] >> 1) & 0x07;
    uint8_t temporal_id_nesting = rbsp[2] & 0x01;
    uint8_t num_temporal_layers = sps_max_sub_layers_minus1 + 1;
    if (num_temporal_layers > 7) num_temporal_layers = 7; /* the field is 3 bits */

    uint32_t o = 0;
    out[o++] = 1; /* configurationVersion */
    out[o++] = (uint8_t)((general_profile_space << 6) | (general_tier_flag << 5) | general_profile_idc);
    memcpy(out + o, compat_flags, 4); o += 4;
    memcpy(out + o, constraint_flags, 6); o += 6;
    out[o++] = general_level_idc;
    out[o++] = 0xF0; /* reserved '1111' + min_spatial_segmentation_idc[11:8] = 0 */
    out[o++] = 0x00; /* min_spatial_segmentation_idc[7:0] */
    out[o++] = 0xFC; /* reserved '111111' + parallelismType = 0 (unknown) */
    out[o++] = 0xFD; /* reserved '111111' + chroma_format_idc = 1 (4:2:0) */
    out[o++] = 0xF8; /* reserved '11111' + bit_depth_luma_minus8 = 0 */
    out[o++] = 0xF8; /* reserved '11111' + bit_depth_chroma_minus8 = 0 */
    out[o++] = 0x00; out[o++] = 0x00; /* avgFrameRate = 0 (unspecified) */
    /* constantFrameRate = 0, numTemporalLayers, temporalIdNested, lengthSizeMinusOne = 3 */
    out[o++] = (uint8_t)((num_temporal_layers << 3) | (temporal_id_nesting << 2) | 0x03);
    out[o++] = 3; /* numOfArrays: VPS, SPS, PPS */

    const struct { uint8_t type; const nal_ref *nal; } arrays[3] = {
        { 32, vps }, { 33, sps }, { 34, pps }
    };
    for (int a = 0; a < 3; a++) {
        out[o++] = (uint8_t)(0x80 | arrays[a].type); /* array_completeness = 1, NAL_unit_type */
        uint16_t be = htobe16(1);
        memcpy(out + o, &be, 2); o += 2;
        be = htobe16((uint16_t)arrays[a].nal->len);
        memcpy(out + o, &be, 2); o += 2;
        memcpy(out + o, arrays[a].nal->data, arrays[a].nal->len); o += (uint32_t)arrays[a].nal->len;
    }
    *out_len = o;
    return NULL;
}

const char *annexb_build_config(annexb_codec codec, const uint8_t *data, size_t len,
                                uint8_t *out, uint32_t out_cap, uint32_t *out_len)
{
    nal_ref vps = { NULL, 0 }, sps = { NULL, 0 }, pps = { NULL, 0 };
    size_t pos = 0, nal, nal_len;

    *out_len = 0;
    while (next_nal(data, len, &pos, &nal, &nal_len)) {
        if (nal_len == 0) continue;
        nal_ref *slot = NULL;
        if (codec == ANNEXB_H265) {
            if (nal_len < 2) continue;
            switch ((data[nal] >> 1) & 0x3F) {
            case 32: slot = &vps; break;
            case 33: slot = &sps; break;
            case 34: slot = &pps; break;
            }
        } else {
            switch (data[nal] & 0x1F) {
            case 7: slot = &sps; break;
            case 8: slot = &pps; break;
            }
        }
        if (slot && !slot->data) {
            slot->data = data + nal;
            slot->len = nal_len;
        }
    }
    return codec == ANNEXB_H265 ? build_hvcc(&vps, &sps, &pps, out, out_cap, out_len)
                                : build_avcc(&sps, &pps, out, out_cap, out_len);
}

typedef struct {
    bool vcl;
    bool first_slice; /* the slice that opens a picture */
    bool key;
    bool au_start;    /* a non-VCL unit that opens an access unit when it follows a slice */
} nal_class;

/* ISO/IEC 14496-10 7.4.1.2.3 and ISO/IEC 23008-2 7.4.2.4.4: where an access unit may begin.
   Returns false for a NAL unit too short to classify. */
static bool classify(annexb_codec codec, const uint8_t *nal, size_t len, nal_class *c)
{
    memset(c, 0, sizeof(*c));
    if (codec == ANNEXB_H265) {
        if (len < 2) return false;
        unsigned type = (nal[0] >> 1) & 0x3F;
        c->vcl = type <= 31;
        if (c->vcl) {
            /* first_slice_segment_in_pic_flag is the first bit behind the 2-byte header. */
            if (len < 3) return false;
            c->first_slice = (nal[2] & 0x80) != 0;
        }
        c->key = type >= 16 && type <= 23; /* IRAP pictures */
        c->au_start = (type >= 32 && type <= 35) || type == 39 || (type >= 41 && type <= 44) ||
                      (type >= 48 && type <= 55);
        return true;
    }
    unsigned type = nal[0] & 0x1F;
    c->vcl = type >= 1 && type <= 5;
    if (c->vcl) {
        /* first_mb_in_slice is ue(v): the value 0 is the single bit 1, first behind the header. */
        if (len < 2) return false;
        /* Partitions B and C (types 3, 4) open with slice_id and never start a picture. */
        c->first_slice = (type == 1 || type == 2 || type == 5) && (nal[1] & 0x80) != 0;
    }
    c->key = type == 5;
    c->au_start = (type >= 6 && type <= 9) || (type >= 14 && type <= 18);
    return true;
}

typedef struct {
    uint8_t *buf;
    size_t len;
    size_t cap;
} sample_buf;

static bool sample_reserve(sample_buf *s, size_t extra)
{
    if (extra > UINT32_MAX - s->len) return false; /* the muxer takes 32-bit sample sizes */
    size_t need = s->len + extra;
    if (need <= s->cap) return true;
    size_t cap = s->cap ? s->cap : 65536;
    while (cap < need) cap = cap > SIZE_MAX / 2 ? need : cap * 2;
    uint8_t *grown = (uint8_t *)realloc(s->buf, cap);
    if (!grown) return false;
    s->buf = grown;
    s->cap = cap;
    return true;
}

annexb_status annexb_split(annexb_codec codec, const uint8_t *data, size_t len,
                           annexb_sample_fn fn, void *user)
{
    sample_buf s = { NULL, 0, 0 };
    annexb_status status = ANNEXB_OK;
    bool has_slice = false, key = false;
    size_t pos = 0, nal, nal_len;

    while (next_nal(data, len, &pos, &nal, &nal_len)) {
        nal_class c;
        if (nal_len == 0 || !classify(codec, data + nal, nal_len, &c)) continue;

        /* The next picture starts at its first slice, or earlier at the AUD, parameter sets or
           SEI that introduce it; the rest of a picture's slices and trailing SEI stay with it. */
        if (has_slice && ((c.vcl && c.first_slice) || (!c.vcl && c.au_start))) {
            if (!fn(user, s.buf, (uint32_t)s.len, key)) { status = ANNEXB_STOPPED; goto done; }
            s.len = 0;
            has_slice = key = false;
        }
        if (nal_len > UINT32_MAX - 4 || !sample_reserve(&s, 4 + nal_len)) { status = ANNEXB_NO_MEMORY; goto done; }
        uint32_t be = htobe32((uint32_t)nal_len);
        memcpy(s.buf + s.len, &be, 4);
        memcpy(s.buf + s.len + 4, data + nal, nal_len);
        s.len += 4 + nal_len;
        if (c.vcl) has_slice = true;
        if (c.key) key = true;
    }
    if (has_slice && !fn(user, s.buf, (uint32_t)s.len, key)) status = ANNEXB_STOPPED;

done:
    free(s.buf);
    return status;
}
