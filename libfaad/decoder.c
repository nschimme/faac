/*
 * Main FAAD Decoder Library Entry Points
 */

#include "faad_internal.h"

FAADAPI faad_status faad_params_init(faad_params *p, uint32_t caller_size)
{
    if (!p || caller_size < sizeof(faad_params)) {
        return FAAD_ERR_INVALID_ARGUMENT;
    }
    memset(p, 0, sizeof(faad_params));
    p->struct_size = sizeof(faad_params);
    p->stream_format = FAAD_STREAM_ADTS;
    p->output_format = FAAD_OUTPUT_16BIT;
    p->downmix_stereo = false;
    return FAAD_OK;
}

FAADAPI faad_status faad_decoder_open(const faad_params *p,
                                       const uint8_t *asc_buf, uint32_t asc_len,
                                       faad_decoder **out)
{
    if (!out) return FAAD_ERR_INVALID_ARGUMENT;
    *out = NULL;

    faad_decoder *dec = (faad_decoder *)calloc(1, sizeof(faad_decoder));
    if (!dec) return FAAD_ERR_NO_MEMORY;

    if (p) {
        dec->params = *p;
    } else {
        faad_params_init(&dec->params, sizeof(faad_params));
    }

    dec->pns_seed = 0x12345678;

    if (asc_buf && asc_len > 0) {
        BitReader bs;
        bits_init(&bs, asc_buf, asc_len);
        faad_status st = asc_decode(&bs, &dec->asc);
        if (st != FAAD_OK) {
            free(dec);
            return st;
        }
        dec->num_channels = dec->asc.num_channels ? dec->asc.num_channels : 2;
        dec->sample_rate = dec->asc.is_sbr ? dec->asc.sbr_sample_rate : (dec->asc.sample_rate ? dec->asc.sample_rate : 44100);
        dec->frame_samples = dec->asc.is_sbr ? 2048 : 1024;
    } else {
        dec->num_channels = 2;
        dec->sample_rate = 44100;
        dec->frame_samples = 1024;
    }

    *out = dec;
    return FAAD_OK;
}

FAADAPI faad_status faad_decoder_close(faad_decoder **dec)
{
    if (!dec || !*dec) return FAAD_OK;
    free(*dec);
    *dec = NULL;
    return FAAD_OK;
}

FAADAPI faad_status faad_decoder_get_info(faad_decoder *dec, faad_decoder_info *out)
{
    if (!dec || !out || out->struct_size < sizeof(faad_decoder_info)) {
        return FAAD_ERR_INVALID_ARGUMENT;
    }
    out->sample_rate = dec->sample_rate;
    out->num_channels = dec->num_channels;
    out->frame_samples = dec->frame_samples;
    out->object_type = dec->asc.is_sbr ? FAAD_OBJ_HE_AAC_V1 : FAAD_OBJ_LOW;
    out->max_output_bytes = dec->frame_samples * dec->num_channels * 4;
    return FAAD_OK;
}

FAADAPI faad_status faad_decoder_decode(faad_decoder *dec,
                                        const uint8_t *in, uint32_t in_bytes,
                                        uint32_t *bytes_consumed,
                                        void *out, uint32_t out_cap,
                                        uint32_t *bytes_written)
{
    if (!dec || !in || !bytes_consumed || !out || !bytes_written) {
        return FAAD_ERR_INVALID_ARGUMENT;
    }

    BitReader bs;
    bits_init(&bs, in, in_bytes);

    uint32_t frame_len = 0;
    if (dec->params.stream_format == FAAD_STREAM_ADTS) {
        faad_status st = adts_decode_header(&bs, &dec->asc, &frame_len);
        if (st != FAAD_OK) return st;
        dec->num_channels = dec->asc.num_channels ? dec->asc.num_channels : 2;
        dec->sample_rate = dec->asc.sample_rate ? dec->asc.sample_rate : 44100;
    }

    /* Core Syntactic Element Parsing */
    uint32_t syntax_id = bits_get(&bs, 3);

    CPEInfo cpe;
    ICSInfo ics;
    memset(&cpe, 0, sizeof(cpe));
    memset(&ics, 0, sizeof(ics));

    if (syntax_id == ID_SCE) {
        decode_sce(&bs, dec, &ics, 0);
        dequantize_spectrum(&ics, dec->spec[0]);
        apply_pns(&ics, dec->spec[0], &dec->pns_seed);
        apply_tns(&ics, dec->spec[0]);
        if (dec->num_channels == 0) dec->num_channels = 1;
    } else if (syntax_id == ID_CPE) {
        decode_cpe(&bs, dec, &cpe, 0);
        dequantize_spectrum(&cpe.ics[0], dec->spec[0]);
        dequantize_spectrum(&cpe.ics[1], dec->spec[1]);
        apply_pns(&cpe.ics[0], dec->spec[0], &dec->pns_seed);
        apply_pns(&cpe.ics[1], dec->spec[1], &dec->pns_seed);
        apply_is_stereo(&cpe, dec->spec[0], dec->spec[1]);
        apply_ms_stereo(&cpe, dec->spec[0], dec->spec[1]);
        apply_tns(&cpe.ics[0], dec->spec[0]);
        apply_tns(&cpe.ics[1], dec->spec[1]);
        if (dec->num_channels == 0) dec->num_channels = 2;
    }

    /* Check for SBR Extension Payload in FIL elements */
    while (bits_get_consumed(&bs) + 8 <= in_bytes * 8) {
        uint32_t elem_id = bits_get(&bs, 3);
        if (elem_id == ID_FIL) {
            uint32_t count = bits_get(&bs, 4);
            if (count == 15) count += bits_get(&bs, 8) - 1;
            uint32_t ext_type = bits_get(&bs, 4);
            if (ext_type == SBR_EXTENSION_DATA || ext_type == SBR_EXTENSION_DATA_CRC) {
                sbr_decode_extension(dec, &bs, 0, syntax_id);
            } else {
                bits_skip(&bs, (count - 1) * 8 + 4);
            }
        } else {
            break;
        }
    }

    /* Perform IMDCT and windowing for each channel */
    float pcm_float[MAX_CHANNELS * FRAME_LEN_LONG];
    for (uint32_t c = 0; c < dec->num_channels; c++) {
        ICSInfo *channel_ics = (syntax_id == ID_CPE) ? &cpe.ics[c] : &ics;
        imdct_and_window(dec, c, channel_ics, dec->spec[c], pcm_float + c * FRAME_LEN_LONG);
    }

    /* SBR Synthesis / HFR */
    float pcm_final[MAX_CHANNELS * 2048];
    if (dec->asc.is_sbr || dec->sbr_present) {
        dec->frame_samples = 2048;
        sbr_apply(dec, dec->num_channels, pcm_float, pcm_final);
    } else {
        dec->frame_samples = 1024;
        memcpy(pcm_final, pcm_float, dec->num_channels * 1024 * sizeof(float));
    }

    /* Write output PCM */
    uint32_t total_samples = dec->frame_samples * dec->num_channels;
    uint32_t required_bytes = total_samples * ((dec->params.output_format == FAAD_OUTPUT_16BIT) ? 2 : 4);

    if (out_cap < required_bytes) {
        return FAAD_ERR_OUTPUT_TOO_SMALL;
    }

    if (dec->params.output_format == FAAD_OUTPUT_16BIT) {
        int16_t *out_int16 = (int16_t *)out;
        for (uint32_t i = 0; i < dec->frame_samples; i++) {
            for (uint32_t c = 0; c < dec->num_channels; c++) {
                float val = pcm_final[c * dec->frame_samples + i];
                if (val > 32767.0f) val = 32767.0f;
                if (val < -32768.0f) val = -32768.0f;
                out_int16[i * dec->num_channels + c] = (int16_t)val;
            }
        }
    } else {
        float *out_f32 = (float *)out;
        for (uint32_t i = 0; i < dec->frame_samples; i++) {
            for (uint32_t c = 0; c < dec->num_channels; c++) {
                out_f32[i * dec->num_channels + c] = pcm_final[c * dec->frame_samples + i] / 32768.0f;
            }
        }
    }

    if (dec->params.stream_format == FAAD_STREAM_ADTS && frame_len > 0) {
        *bytes_consumed = frame_len;
    } else {
        *bytes_consumed = (bits_get_consumed(&bs) + 7) / 8;
    }
    *bytes_written = required_bytes;

    return FAAD_OK;
}

FAADAPI const char *faad_strerror(faad_status status)
{
    switch (status) {
        case FAAD_OK:                   return "Success";
        case FAAD_ERR_INVALID_ARGUMENT: return "Invalid argument";
        case FAAD_ERR_UNSUPPORTED:      return "Unsupported configuration";
        case FAAD_ERR_NO_MEMORY:        return "Memory allocation failed";
        case FAAD_ERR_OUTPUT_TOO_SMALL: return "Output buffer too small";
        case FAAD_ERR_NEED_MORE_DATA:   return "Need more input data";
        case FAAD_ERR_DECODE_FAILED:    return "Decoding failed";
        default:                        return "Unknown error";
    }
}
