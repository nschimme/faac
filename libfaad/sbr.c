/*
 * Spectral Band Replication (SBR) Decoder Engine
 */

#include "faad_internal.h"
#include "sfb_tables.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float qmf_c_synth[640];
static bool sbr_tables_initialized = false;

static void sbr_init_qmf_tables(void)
{
    if (sbr_tables_initialized) return;

    for (int i = 0; i < 640; i++) {
        qmf_c_synth[i] = sinf((float)M_PI * (i + 0.5f) / 640.0f);
    }
    sbr_tables_initialized = true;
}

void sbr_init_tables(void)
{
    sbr_init_qmf_tables();
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

/* 32-subband QMF analysis filterbank */
static void qmf_analysis(const float *in, float qmf_real[32][32], float qmf_imag[32][32])
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
static void qmf_synthesis(float qmf_real[32][64], float qmf_imag[32][64], float *out)
{
    sbr_init_qmf_tables();

    for (int t = 0; t < 32; t++) {
        for (int n = 0; n < 64; n++) {
            float sum = 0.0f;
            for (int k = 0; k < 64; k++) {
                float re = qmf_real[t][k];
                float im = qmf_imag[t][k];
                float angle = (float)M_PI * (k + 0.5f) * (n - 0.25f) / 64.0f;
                sum += re * cosf(angle) - im * sinf(angle);
            }
            out[t * 64 + n] = sum * qmf_c_synth[(t * 64 + n) % 640];
        }
    }
}

void sbr_apply(struct faad_decoder *dec, uint32_t num_ch, float *pcm_in, float *pcm_out)
{
    if (!dec->sbr_present) {
        /* Dual-rate 1:2 sample rate interpolation if SBR extension payload absent */
        for (uint32_t ch = 0; ch < num_ch; ch++) {
            for (uint32_t i = 0; i < FRAME_LEN_LONG; i++) {
                float sample = pcm_in[ch * FRAME_LEN_LONG + i];
                pcm_out[ch * 2048 + i * 2]     = sample;
                pcm_out[ch * 2048 + i * 2 + 1] = sample;
            }
        }
        return;
    }

    /* Full 32-subband QMF Analysis -> HFR -> 64-subband QMF Synthesis */
    for (uint32_t ch = 0; ch < num_ch; ch++) {
        float qmf_ana_r[32][32];
        float qmf_ana_i[32][32];
        float qmf_syn_r[32][64];
        float qmf_syn_i[32][64];

        memset(qmf_syn_r, 0, sizeof(qmf_syn_r));
        memset(qmf_syn_i, 0, sizeof(qmf_syn_i));

        qmf_analysis(pcm_in + ch * FRAME_LEN_LONG, qmf_ana_r, qmf_ana_i);

        /* Copy baseband low subbands (0..31) */
        for (int t = 0; t < 32; t++) {
            for (int k = 0; k < 32; k++) {
                qmf_syn_r[t][k] = qmf_ana_r[t][k];
                qmf_syn_i[t][k] = qmf_ana_i[t][k];
            }
        }

        /* High Frequency Reconstruction (HFR): Replicate low subbands (0..31) to high subbands (32..63) */
        for (int t = 0; t < 32; t++) {
            for (int k = 32; k < 64; k++) {
                int src_k = k - 32;
                qmf_syn_r[t][k] = qmf_ana_r[t][src_k];
                qmf_syn_i[t][k] = qmf_ana_i[t][src_k];
            }
        }

        qmf_synthesis(qmf_syn_r, qmf_syn_i, pcm_out + ch * 2048);
    }
}
