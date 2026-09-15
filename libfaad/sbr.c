/*
 * Spectral Band Replication (SBR) Decoder Engine
 */

#include "faad_internal.h"
#include "sfb_tables.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

/* 32-subband QMF analysis filterbank */
static void qmf_analysis_320(const float *in, float qmf_real[32][32], float qmf_imag[32][32])
{
    for (int t = 0; t < 32; t++) {
        for (int k = 0; k < 32; k++) {
            float sum_r = 0.0f;
            float sum_i = 0.0f;
            for (int n = 0; n < 32; n++) {
                float sample = in[t * 32 + n];
                float angle = (float)M_PI * (k + 0.5f) * (n - 0.5f) / 32.0f;
                sum_r += sample * cosf(angle);
                sum_i += sample * sinf(angle);
            }
            qmf_real[t][k] = sum_r / 32.0f;
            qmf_imag[t][k] = sum_i / 32.0f;
        }
    }
}

/* 64-subband QMF synthesis filterbank */
static void qmf_synthesis_640(float qmf_real[32][64], float qmf_imag[32][64], float *out)
{
    for (int t = 0; t < 32; t++) {
        for (int n = 0; n < 64; n++) {
            float sum = 0.0f;
            for (int k = 0; k < 64; k++) {
                float re = qmf_real[t][k];
                float im = qmf_imag[t][k];
                float angle = (float)M_PI * (k + 0.5f) * (n - 0.25f) / 64.0f;
                sum += re * cosf(angle) - im * sinf(angle);
            }
            out[t * 64 + n] = sum / 32.0f;
        }
    }
}

void sbr_apply(struct faad_decoder *dec, uint32_t num_ch, float *pcm_in, float *pcm_out)
{
    if (!dec->sbr_present) {
        /* Smooth linear interpolation for dual-rate core upsampling */
        for (uint32_t ch = 0; ch < num_ch; ch++) {
            float prev = pcm_in[ch * FRAME_LEN_LONG];
            for (uint32_t i = 0; i < FRAME_LEN_LONG; i++) {
                float sample = pcm_in[ch * FRAME_LEN_LONG + i];
                pcm_out[ch * 2048 + i * 2]     = 0.5f * (prev + sample);
                pcm_out[ch * 2048 + i * 2 + 1] = sample;
                prev = sample;
            }
        }
        return;
    }

    /* Full 32-subband QMF Analysis -> SBR HFR -> 64-subband QMF Synthesis */
    for (uint32_t ch = 0; ch < num_ch; ch++) {
        float qmf_ana_r[32][32];
        float qmf_ana_i[32][32];
        float qmf_syn_r[32][64];
        float qmf_syn_i[32][64];

        memset(qmf_syn_r, 0, sizeof(qmf_syn_r));
        memset(qmf_syn_i, 0, sizeof(qmf_syn_i));

        qmf_analysis_320(pcm_in + ch * FRAME_LEN_LONG, qmf_ana_r, qmf_ana_i);

        /* Copy baseband low subbands (0..31) */
        for (int t = 0; t < 32; t++) {
            for (int k = 0; k < 32; k++) {
                qmf_syn_r[t][k] = qmf_ana_r[t][k];
                qmf_syn_i[t][k] = qmf_ana_i[t][k];
            }
        }

        /* High Frequency Reconstruction (HFR): Replicate low subbands to high subbands */
        for (int t = 0; t < 32; t++) {
            for (int k = 32; k < 64; k++) {
                int src_k = k - 32;
                qmf_syn_r[t][k] = qmf_ana_r[t][src_k];
                qmf_syn_i[t][k] = qmf_ana_i[t][src_k];
            }
        }

        qmf_synthesis_640(qmf_syn_r, qmf_syn_i, pcm_out + ch * 2048);
    }
}
