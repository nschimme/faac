/*
 * Spectral Band Replication (SBR) Decoder Engine
 */

#include "faad_internal.h"

void sbr_init_tables(void)
{
}

faad_status sbr_decode_extension(struct faad_decoder *dec, BitReader *bs, uint32_t ch, uint32_t syntax_id)
{
    (void)syntax_id;
    if (ch >= MAX_CHANNELS) return FAAD_ERR_INVALID_ARGUMENT;

    SBRState *sbr = &dec->sbr[ch];
    dec->sbr_present = true;

    bool bs_header_extra_1 = bits_get(bs, 1);
    if (bs_header_extra_1) {
        sbr->header_present = true;
        sbr->bs_start_freq = bits_get(bs, 4);
        sbr->bs_stop_freq = bits_get(bs, 4);
        sbr->bs_xover_band = bits_get(bs, 3);
        bits_skip(bs, 2);
    }

    sbr->bs_frame_class = bits_get(bs, 2);
    sbr->bs_num_env = bits_get(bs, 2) + 1;

    return FAAD_OK;
}

void sbr_apply(struct faad_decoder *dec, uint32_t num_ch, float *pcm_in, float *pcm_out)
{
    if (!dec->sbr_present) {
        for (uint32_t ch = 0; ch < num_ch; ch++) {
            for (uint32_t i = 0; i < FRAME_LEN_LONG; i++) {
                float sample = pcm_in[ch * FRAME_LEN_LONG + i];
                pcm_out[ch * 2048 + i * 2]     = sample;
                pcm_out[ch * 2048 + i * 2 + 1] = sample;
            }
        }
        return;
    }

    for (uint32_t ch = 0; ch < num_ch; ch++) {
        for (uint32_t i = 0; i < FRAME_LEN_LONG; i++) {
            float sample = pcm_in[ch * FRAME_LEN_LONG + i];
            pcm_out[ch * 2048 + i * 2]     = sample;
            pcm_out[ch * 2048 + i * 2 + 1] = sample;
        }
    }
}
