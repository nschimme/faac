/*
 * AudioSpecificConfig and ADTS Header Parsing
 */

#include "faad_internal.h"
#ifdef HAVE_LIBFAAM
#include "faam.h"
#endif

const uint32_t faad_sample_rates[16] = {
    96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350, 0, 0, 0
};

faad_status asc_decode(BitReader *bs, AudioSpecificConfig *asc)
{
    memset(asc, 0, sizeof(*asc));

#ifdef HAVE_LIBFAAM
    uint8_t raw_asc[16];
    uint32_t rem_bytes = bs->len - bs->byte_pos;
    if (rem_bytes > sizeof(raw_asc)) rem_bytes = sizeof(raw_asc);
    memcpy(raw_asc, bs->buffer + bs->byte_pos, rem_bytes);

    faam_asc_info info;
    if (faam_asc_parse(raw_asc, rem_bytes, &info) == FAAM_OK) {
        asc->object_type = (enum faad_object_type)info.object_type;
        asc->sample_rate = info.sample_rate;
        asc->num_channels = info.channels;
        asc->is_sbr = info.sbr_present;
        asc->sbr_sample_rate = info.sbr_present ? info.sample_rate * 2 : info.sample_rate;

        if (asc->object_type == FAAD_OBJ_NULL || asc->object_type == FAAD_OBJ_HE_AAC_V1) {
            asc->object_type = FAAD_OBJ_LC;
        }
        return FAAD_OK;
    }
#endif

    uint32_t aot = bits_get(bs, 5);
    if (aot == 31) {
        aot = 32 + bits_get(bs, 6);
    }
    asc->object_type = (enum faad_object_type)aot;

    uint32_t sr_idx = bits_get(bs, 4);
    if (sr_idx == 15) {
        asc->sample_rate = bits_get(bs, 24);
    } else {
        asc->sample_rate = faad_sample_rates[sr_idx];
    }

    asc->num_channels = bits_get(bs, 4);

    if (asc->object_type == FAAD_OBJ_HE_AAC_V1) {
        asc->is_sbr = true;
        uint32_t sbr_sr_idx = bits_get(bs, 4);
        if (sbr_sr_idx == 15) {
            asc->sbr_sample_rate = bits_get(bs, 24);
        } else {
            asc->sbr_sample_rate = faad_sample_rates[sbr_sr_idx];
        }
        uint32_t real_aot = bits_get(bs, 5);
        if (real_aot == 31) {
            real_aot = 32 + bits_get(bs, 6);
        }
        asc->object_type = (enum faad_object_type)real_aot;
    } else {
        asc->sbr_sample_rate = asc->sample_rate * 2;
    }

    if (asc->object_type == FAAD_OBJ_LC || asc->object_type == FAAD_OBJ_HE_AAC_V1 || asc->object_type == FAAD_OBJ_NULL) {
        asc->object_type = FAAD_OBJ_LC;
        return FAAD_OK;
    }

    return FAAD_OK;
}

faad_status adts_decode_header(BitReader *bs, AudioSpecificConfig *asc, uint32_t *frame_length)
{
    uint32_t sync = bits_get(bs, 12);
    if (sync != 0xFFF) {
        return FAAD_ERR_DECODE_FAILED;
    }
    bits_skip(bs, 1);
    bits_skip(bs, 2);
    uint32_t protection_absent = bits_get(bs, 1);

    uint32_t profile = bits_get(bs, 2);
    uint32_t sr_idx = bits_get(bs, 4);
    bits_skip(bs, 1);
    uint32_t channel_config = bits_get(bs, 3);
    bits_skip(bs, 4);

    uint32_t flen = bits_get(bs, 13);
    bits_skip(bs, 11);
    bits_skip(bs, 2);

    if (protection_absent == 0) {
        bits_skip(bs, 16);
    }

    if (asc) {
        memset(asc, 0, sizeof(*asc));
        asc->object_type = (enum faad_object_type)(profile + 1);
        asc->sample_rate = faad_sample_rates[sr_idx];
        asc->num_channels = channel_config;
    }

    if (frame_length) {
        *frame_length = flen;
    }

    return FAAD_OK;
}
