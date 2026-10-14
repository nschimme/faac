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
 * The public faac_* encoder API (see include/faac.h).
 *
 * This is a thin facade over the private encoder core in frame.c (declared in
 * faac_internal.h): it supplies the params-at-open entry point, uniform
 * faac_status error reporting, resolved-property queries, and library-owned
 * ASC. The opaque faac_encoder* is the core's faacEncStruct*.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <faac.h>
#include "frame.h"
#include "bitstream.h"
#include "sbr.h"
#include "resample.h"
#include "util.h"

/* The public enums are width-pinned to 32 bits by their FAAC_*_MAX sentinels;
 * verify the compiler honored that so the ABI matches the documented layout. */
_Static_assert(sizeof(enum faac_status)        == 4, "faac_status must be 32-bit");
_Static_assert(sizeof(enum faac_object_type)   == 4, "faac_object_type must be 32-bit");
_Static_assert(sizeof(enum faac_rate_control)  == 4, "faac_rate_control must be 32-bit");
_Static_assert(sizeof(enum faac_stereo_mode)   == 4, "faac_stereo_mode must be 32-bit");
_Static_assert(sizeof(enum faac_shortctl_mode) == 4, "faac_shortctl_mode must be 32-bit");
_Static_assert(sizeof(enum faac_stream_format) == 4, "faac_stream_format must be 32-bit");
_Static_assert(sizeof(enum faac_channel_layout) == 4, "faac_channel_layout must be 32-bit");
_Static_assert(sizeof(enum faac_input_format)  == 4, "faac_input_format must be 32-bit");

/* These enums retain the internal numeric values, unlike rate control. */
_Static_assert((int)FAAC_OBJ_AUTO == AUTO && (int)FAAC_OBJ_LC == LOW
               && (int)FAAC_OBJ_HE_AAC_V1 == HE_V1, "object type drift");
_Static_assert((int)FAAC_STEREO_LR == JOINT_NONE && (int)FAAC_STEREO_AUTO == JOINT_MIXED, "stereo mode drift");
_Static_assert((int)FAAC_SHORTCTL_NORMAL == SHORTCTL_NORMAL && (int)FAAC_SHORTCTL_NOLONG == SHORTCTL_NOLONG, "shortctl drift");
_Static_assert((int)FAAC_STREAM_RAW == RAW_STREAM && (int)FAAC_STREAM_ADTS == ADTS_STREAM, "stream format drift");
_Static_assert(sizeof(((faac_encoder_info *)0)->asc) >= sizeof(((faacEncStruct *)0)->asc),
               "info.asc must hold the handle's ASC");
_Static_assert((int)FAAC_INPUT_NULL  == INPUT_NULL  && (int)FAAC_INPUT_16BIT == INPUT_16BIT
            && (int)FAAC_INPUT_24BIT == INPUT_24BIT && (int)FAAC_INPUT_24IN32 == INPUT_32BIT
            && (int)FAAC_INPUT_FLOAT == INPUT_FLOAT, "input format drift");

/* The minimum parameter layout ends at rate_control so appended fields do
 * not raise the minimum caller size. */
#define PARAMS_BASELINE_SIZE \
    ((uint32_t)(offsetof(faac_params, rate_control) + sizeof(enum faac_rate_control)))

/* Query baselines end at their last field so appended fields remain optional. */
#define LIBRARY_INFO_BASELINE_SIZE \
    ((uint32_t)(offsetof(faac_library_info, sbr_decimation) + sizeof(uint32_t)))
#define ENCODER_INFO_BASELINE_SIZE \
    ((uint32_t)(offsetof(faac_encoder_info, rate_control) + sizeof(enum faac_rate_control)))

/* faac_encoder* and faacEncHandle are the same underlying object. */
static inline faacEncStruct *unwrap(faac_encoder *enc) { return (faacEncStruct *)enc; }

FAACAPI faac_status faac_get_library_info(faac_library_info *out)
{
    faac_library_info info;
    char *vid = NULL, *vcopy = NULL;
    uint32_t caller_size, n;

    if (!out)
        return FAAC_ERR_INVALID_ARGUMENT;
    caller_size = out->struct_size;
    if (caller_size < LIBRARY_INFO_BASELINE_SIZE)
        return FAAC_ERR_INVALID_ARGUMENT;

    faacEncGetVersion(&vid, &vcopy);

    memset(&info, 0, sizeof(info));
    info.version        = vid;
    info.copyright      = vcopy;
    info.max_channels   = (uint32_t)MAX_CHANNELS;
    info.sbr_decimation = (uint32_t)FAAC_SBR_DECIMATION;

    /* Write at most the caller's struct_size so a newer library cannot overrun
     * an older, smaller faac_library_info; report the byte count actually set. */
    n = caller_size < (uint32_t)sizeof(info) ? caller_size : (uint32_t)sizeof(info);
    info.struct_size = n;
    memcpy(out, &info, n);
    return FAAC_OK;
}

FAACAPI faac_status faac_params_init(faac_params *p, uint32_t caller_size)
{
    faac_params tmp;
    uint32_t n;

    if (!p)
        return FAAC_ERR_INVALID_ARGUMENT;
    if (caller_size < PARAMS_BASELINE_SIZE)
        return FAAC_ERR_INVALID_ARGUMENT;

    memset(&tmp, 0, sizeof(tmp));
    tmp.object_type   = FAAC_OBJ_LC;
    tmp.stereo_mode   = FAAC_STEREO_AUTO;
    tmp.flags         = FAAC_FLAG_TNS | FAAC_FLAG_PNS;
    tmp.bit_rate      = 0;
    tmp.bandwidth     = 0;              /* derive from bit_rate */
    tmp.quality       = 0;
    tmp.peak_bit_rate = 0;              /* no per-frame peak cap */
    tmp.rate_control  = FAAC_RC_ABR;
    tmp.stream_format = FAAC_STREAM_ADTS;
    tmp.input_format  = FAAC_INPUT_16BIT;
    tmp.short_control = FAAC_SHORTCTL_NORMAL;

    /* Write at most the caller's struct_size so a newer library cannot overrun
     * an older, smaller faac_params; report the byte count actually set. */
    n = caller_size < (uint32_t)sizeof(faac_params) ? caller_size : (uint32_t)sizeof(faac_params);
    tmp.struct_size = n;
    memcpy(p, &tmp, n);
    return FAAC_OK;
}

#define FAAC_MIN_SAMPLE_RATE 7350u
#define FAAC_MAX_SAMPLE_RATE 96000u

static uint64_t resolved_bit_rate(const faac_params *p)
{
    return p->rate_control == FAAC_RC_VBR ? 0 :
        (p->bit_rate ? p->bit_rate : (uint64_t)64000u * p->channels);
}

/* Validate the enumerated/range fields the caller supplied. Returns FAAC_OK,
 * FAAC_ERR_INVALID_ARGUMENT for out-of-range values, or FAAC_ERR_UNSUPPORTED
 * for values that are valid but not implemented in this build. */
static faac_status validate_params(const faac_params *p, uint64_t *bit_rate_per_channel)
{
    switch (p->object_type) {
        case FAAC_OBJ_AUTO: case FAAC_OBJ_LC: case FAAC_OBJ_HE_AAC_V1:
            break;
        case FAAC_OBJ_HE_AAC_V2:
            return FAAC_ERR_UNSUPPORTED;   /* parametric stereo not implemented */
        default:
            return FAAC_ERR_INVALID_ARGUMENT;
    }
    switch (p->stereo_mode) {
        case FAAC_STEREO_LR: case FAAC_STEREO_MS:
        case FAAC_STEREO_IS: case FAAC_STEREO_AUTO: break;
        default: return FAAC_ERR_INVALID_ARGUMENT;
    }
    switch (p->short_control) {
        case FAAC_SHORTCTL_NORMAL: case FAAC_SHORTCTL_NOSHORT:
        case FAAC_SHORTCTL_NOLONG: break;
        default: return FAAC_ERR_INVALID_ARGUMENT;
    }
    switch (p->stream_format) {
        case FAAC_STREAM_RAW: case FAAC_STREAM_ADTS: case FAAC_STREAM_ADTS_MPEG2: break;
        default: return FAAC_ERR_INVALID_ARGUMENT;
    }
    switch (p->input_format) {
        case FAAC_INPUT_16BIT: case FAAC_INPUT_24BIT: case FAAC_INPUT_24IN32: case FAAC_INPUT_FLOAT:
            break;
        default:
            return FAAC_ERR_INVALID_ARGUMENT;
    }
    /* The ADTS/ASC rate table spans 7350..96000 Hz. Outside it the rate index
     * would lie, and the rate-derived tables and bit budgets are only sized for
     * it. */
    if (p->sample_rate < FAAC_MIN_SAMPLE_RATE || p->sample_rate > FAAC_MAX_SAMPLE_RATE)
        return FAAC_ERR_INVALID_ARGUMENT;
    if (GetChannelConfig((int)p->channels) == 0)
        return FAAC_ERR_INVALID_ARGUMENT;
    if (p->channel_layout != FAAC_LAYOUT_AAC && p->channel_layout != FAAC_LAYOUT_WAV)
        return FAAC_ERR_INVALID_ARGUMENT;
    uint64_t bit_rate = resolved_bit_rate(p);
    *bit_rate_per_channel = bit_rate / p->channels;
    switch (p->rate_control) {
    case FAAC_RC_VBR:
        if (p->bit_rate || p->quality > 5000)
            return FAAC_ERR_INVALID_ARGUMENT;
        break;
    case FAAC_RC_ABR:
    case FAAC_RC_CBR:
        if (p->quality || !*bit_rate_per_channel)
            return FAAC_ERR_INVALID_ARGUMENT;
        if (*bit_rate_per_channel > MaxBitrate(p->sample_rate))
            return FAAC_ERR_INVALID_ARGUMENT;
        break;
    default:
        return FAAC_ERR_INVALID_ARGUMENT;
    }
    if (p->peak_bit_rate) {
        /* Bounded because it is converted into a per-frame bit budget; an
         * absurd value is a caller error, not a request for an unlimited
         * frame. */
        if (p->peak_bit_rate > FAAC_MAX_PEAK_BIT_RATE)
            return FAAC_ERR_INVALID_ARGUMENT;
        /* A peak below the average is unsatisfiable by definition, and the two
         * controllers actively fight: rate control raises quality to reach
         * bit_rate while the limiter cuts it to stay under the cap, so frames
         * end up larger the tighter the cap gets. Reject it rather than emit a
         * stream that quietly busts the ceiling it was asked to respect. */
        if (p->peak_bit_rate < bit_rate)
            return FAAC_ERR_INVALID_ARGUMENT;
    }
    if (p->flags & ~(FAAC_FLAG_TNS | FAAC_FLAG_PNS))
        return FAAC_ERR_INVALID_ARGUMENT;
    return FAAC_OK;
}

FAACAPI faac_status faac_encoder_open(const faac_params *p, faac_encoder **out)
{
    faacEncStruct *h;
    faacEncConfiguration *cfg;
    unsigned long inSamples = 0, maxOut = 0;
    faac_status st;
    int applied;
    uint64_t bit_rate_per_channel;

    if (!out)
        return FAAC_ERR_INVALID_ARGUMENT;
    *out = NULL;
    if (!p)
        return FAAC_ERR_INVALID_ARGUMENT;

    /* Not sizeof(faac_params): that grows, which would reject older callers. */
    if (p->struct_size < PARAMS_BASELINE_SIZE)
        return FAAC_ERR_INVALID_ARGUMENT;

    st = validate_params(p, &bit_rate_per_channel);
    if (st != FAAC_OK)
        return st;

    h = (faacEncStruct *)faacEncOpen(p->sample_rate, p->channels,
                                     &inSamples, &maxOut);
    if (!h)
        return FAAC_ERR_NO_MEMORY;

    /* Install validated parameters before the core's one-time initialization. */
    cfg = &h->config;
    cfg->mpegVersion   = p->stream_format == FAAC_STREAM_ADTS_MPEG2 ? MPEG2 : MPEG4;
    cfg->aacObjectType = (unsigned int)p->object_type;  /* LOW / HE_V1 / AUTO */
    cfg->jointmode     = (unsigned int)p->stereo_mode;
    cfg->useLfe        = (p->channels == 6 || p->channels == 8);
    cfg->useTns        = (p->flags & FAAC_FLAG_TNS) ? 1 : 0;
    cfg->bitRate       = (unsigned long)bit_rate_per_channel;
    cfg->bandWidth     = p->bandwidth;
    cfg->quantqual     = p->rate_control == FAAC_RC_VBR ? p->quality : 0;
    cfg->outputFormat  = p->stream_format == FAAC_STREAM_RAW ? RAW_STREAM : ADTS_STREAM;
    cfg->inputFormat   = (unsigned int)p->input_format;
    cfg->shortctl      = (int)p->short_control;
    cfg->usePns        = (p->flags & FAAC_FLAG_PNS) ? 1 : 0;
    cfg->maxBitRate    = p->peak_bit_rate;
    switch (p->rate_control) {
    case FAAC_RC_ABR: cfg->rateControl = RATE_ABR; break;
    case FAAC_RC_VBR: cfg->rateControl = RATE_VBR; break;
    case FAAC_RC_CBR: cfg->rateControl = RATE_CBR; break;
    default: break; /* validate_params rejected other modes */
    }
    /* WAVE order must become AAC element order. */
    if (p->channel_layout == FAAC_LAYOUT_WAV && p->channels >= 3) {
        static const uint8_t wav_to_aac[6][8] = {
            {2, 0, 1},
            {2, 0, 1, 3},
            {2, 0, 1, 3, 4},
            {2, 0, 1, 4, 5, 3},
            {0},
            {2, 0, 1, 6, 7, 4, 5, 3}
        };
        for (uint32_t i = 0; i < p->channels; ++i)
            cfg->channel_map[i] = wav_to_aac[p->channels - 3][i];
    }
    /* AAC input retains the identity map installed by faacEncOpen. */

    applied = faacEncApplyConfig(h);
    if (applied != 1) {
        faacEncClose((faacEncHandle)h);
        return (applied < 0) ? FAAC_ERR_NO_MEMORY : FAAC_ERR_INVALID_ARGUMENT;
    }

    /* ASC depends only on the resolved configuration. MPEG-2 has none. */
    if (cfg->mpegVersion != MPEG2)
        h->ascLen = (uint32_t)faacEncGetDecoderSpecificInfo((faacEncHandle)h, h->asc);

    *out = (faac_encoder *)h;
    return FAAC_OK;
}

FAACAPI faac_status faac_encoder_close(faac_encoder **enc)
{
    if (!enc)
        return FAAC_ERR_INVALID_ARGUMENT;
    if (*enc) {
        faacEncClose((faacEncHandle)unwrap(*enc));
        *enc = NULL;
    }
    return FAAC_OK;
}

/* LC: one frame of 50% MDCT overlap. HE-AAC: that same core delay at full
 * rate, plus the half-band downsampler's FIR group delay, plus the one sample
 * the input FIFO is primed with to make the count even. The SBR decoder's QMF
 * delay is excluded: decoders add their own when trimming. */
static uint32_t faacEncoderDelay(const faacEncStruct *h)
{
    switch (h->config.aacObjectType) {
        case LOW:   return FRAME_LEN;
        case HE_V1: return 2 * FRAME_LEN + RESAMPLE_FILTER_LEN / 2 + 1;
    }
    assert(0 && "faacEncoderDelay: unhandled aacObjectType");
    return FRAME_LEN;
}

FAACAPI faac_status faac_encoder_get_info(const faac_encoder *enc, faac_encoder_info *out)
{
    faac_encoder_info info;
    const faacEncStruct *h;
    uint32_t caller_size, n;

    if (!enc || !out)
        return FAAC_ERR_INVALID_ARGUMENT;
    caller_size = out->struct_size;
    if (caller_size < ENCODER_INFO_BASELINE_SIZE)
        return FAAC_ERR_INVALID_ARGUMENT;
    h = (const faacEncStruct *)enc;

    memset(&info, 0, sizeof(info));
    info.frame_samples    = faacFrameSamples(h);
    info.max_output_bytes = (uint32_t)ADTS_FRAMESIZE;
    /* The handle holds the halved core rate for HE-AAC; report the rate the
     * decoder reconstructs. */
    info.sample_rate      = (uint32_t)SbrContextGetFullRate(h->sbrContext, h->sampleRate);
    info.object_type      = (enum faac_object_type)h->config.aacObjectType;
    info.bit_rate         = (uint32_t)((uint64_t)h->config.bitRate * h->numChannels);
    info.bandwidth        = (uint32_t)h->config.bandWidth;
    info.quality          = (uint32_t)h->config.quantqual;
    info.stream_format    = h->config.outputFormat == RAW_STREAM ? FAAC_STREAM_RAW :
        h->config.mpegVersion == MPEG2 ? FAAC_STREAM_ADTS_MPEG2 : FAAC_STREAM_ADTS;
    info.peak_bit_rate     = (uint32_t)h->config.maxBitRate;
    info.encoder_delay    = faacEncoderDelay(h);
    info.asc_size         = h->ascLen;
    memcpy(info.asc, h->asc, h->ascLen);
    switch (h->config.rateControl) {
    case RATE_ABR: info.rate_control = FAAC_RC_ABR; break;
    case RATE_VBR: info.rate_control = FAAC_RC_VBR; break;
    case RATE_CBR: info.rate_control = FAAC_RC_CBR; break;
    default: return FAAC_ERR_INVALID_ARGUMENT;
    }

    /* Write at most the caller's struct_size so a newer library cannot overrun
     * an older, smaller faac_encoder_info; report the byte count actually set. */
    n = caller_size < (uint32_t)sizeof(info) ? caller_size : (uint32_t)sizeof(info);
    info.struct_size = n;
    memcpy(out, &info, n);
    return FAAC_OK;
}

FAACAPI faac_status faac_encoder_asc(const faac_encoder *enc,
                                     const uint8_t **buf, uint32_t *len)
{
    const faacEncStruct *h;

    if (!enc || !buf || !len)
        return FAAC_ERR_INVALID_ARGUMENT;
    h = (const faacEncStruct *)enc;

    if (h->config.mpegVersion == MPEG2) {
        *buf = NULL;
        *len = 0;
        return FAAC_ERR_UNSUPPORTED;
    }

    *buf = h->asc;
    *len = h->ascLen;
    return FAAC_OK;
}

FAACAPI faac_status faac_encoder_encode(faac_encoder *enc,
                                        const void *in, uint32_t in_samples,
                                        uint8_t *out, uint32_t out_cap,
                                        uint32_t *bytes_written)
{
    faacEncStruct *h;
    int rc;

    if (!enc || !out || !bytes_written)
        return FAAC_ERR_INVALID_ARGUMENT;
    *bytes_written = 0;
    if (in_samples && !in)
        return FAAC_ERR_INVALID_ARGUMENT;
    h = unwrap(enc);

    if (out_cap < (uint32_t)ADTS_FRAMESIZE)
        return FAAC_ERR_OUTPUT_TOO_SMALL;
    if (in_samples > faacFrameSamples(h) * h->numChannels)
        return FAAC_ERR_INPUT_OVERFLOW;

    /* faacEncEncode reinterprets the input bytes per the configured
     * input_format; the int32_t* parameter is historical and does not imply an
     * int32 layout. */
    rc = faacEncEncode((faacEncHandle)h, (int32_t *)(uintptr_t)in,
                       in_samples, out, out_cap);
    /* A too-small output buffer was already rejected above, so a negative
     * return here is an unexpected core fault, not a buffer-size problem --
     * don't disguise it as one or the caller will grow the buffer and retry. */
    if (rc < 0)
        return FAAC_ERR_INTERNAL;

    *bytes_written = (uint32_t)rc;
    return FAAC_OK;
}

FAACAPI const char *faac_strerror(faac_status status)
{
    switch (status) {
        case FAAC_OK:                   return "success";
        case FAAC_ERR_INVALID_ARGUMENT: return "invalid argument";
        case FAAC_ERR_UNSUPPORTED:      return "unsupported configuration";
        case FAAC_ERR_NO_MEMORY:        return "out of memory";
        case FAAC_ERR_OUTPUT_TOO_SMALL: return "output buffer too small";
        case FAAC_ERR_INPUT_OVERFLOW:   return "input sample count too large";
        case FAAC_ERR_INTERNAL:         return "internal encoder error";
        case FAAC_STATUS_MAX:           break;
    }
    return "unknown error";
}
