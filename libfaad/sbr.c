/*
 * Spectral Band Replication (SBR) & Parametric Stereo (PS) Decoder Engine
 */

#include "faad_internal.h"
#include "sfb_tables.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const float ps_iid_scale_lut[15] = {
    0.000f, 0.125f, 0.250f, 0.375f, 0.500f, 0.625f, 0.750f, 0.875f,
    1.000f, 1.125f, 1.250f, 1.375f, 1.500f, 1.750f, 2.000f
};

static void ps_decode_payload(struct faad_decoder *dec, BitReader *bs)
{
    PSState *ps = &dec->ps;
    dec->ps_present = true;

    ps->enable_iid = bits_get(bs, 1);
    if (ps->enable_iid) {
        bool iid_mode = bits_get(bs, 1); /* 0 = 10 bands, 1 = 20 bands */
        int bands = iid_mode ? 20 : 10;
        for (int b = 0; b < bands; b++) {
            int val = bits_get(bs, 4);
            ps->iid_idx[b] = (int8_t)(val - 7);
        }
    }

    ps->enable_icc = bits_get(bs, 1);
    if (ps->enable_icc) {
        bool icc_mode = bits_get(bs, 1);
        int bands = icc_mode ? 20 : 10;
        for (int b = 0; b < bands; b++) {
            int val = bits_get(bs, 3);
            ps->icc_idx[b] = (int8_t)val;
        }
    }

    /* Compute PS mixing gains H11, H22, H12, H21 */
    for (int b = 0; b < SBR_PS_BANDS; b++) {
        int iid = ps->iid_idx[b] + 7;
        if (iid < 0) iid = 0;
        if (iid > 14) iid = 14;

        float c = ps_iid_scale_lut[iid];
        ps->h11[b] = sqrtf(2.0f / (1.0f + c * c));
        ps->h22[b] = c * ps->h11[b];
        ps->h12[b] = 0.0f;
        ps->h21[b] = 0.0f;
    }
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
        bits_skip(bs, 2); /* reserved */
    }

    /* SBR Frame Grid Decoding */
    sbr->bs_frame_class = bits_get(bs, 2);
    sbr->bs_num_env = bits_get(bs, 2) + 1;

    /* SBR Inverse Filtering Mode */
    for (int i = 0; i < 4; i++) {
        bits_skip(bs, 2);
    }

    /* SBR Envelope Data */
    for (int env = 0; env < sbr->bs_num_env; env++) {
        for (int band = 0; band < 48; band++) {
            bits_skip(bs, 6);
        }
    }

    /* SBR Noise Floor Data */
    sbr->bs_num_noise = (sbr->bs_num_env > 1) ? 2 : 1;
    for (int n = 0; n < sbr->bs_num_noise; n++) {
        for (int band = 0; band < 5; band++) {
            bits_skip(bs, 5);
        }
    }

    /* SBR Synthetics / Harmonics */
    bool bs_add_harmonic_flag = bits_get(bs, 1);
    if (bs_add_harmonic_flag) {
        for (int band = 0; band < 48; band++) {
            sbr->bs_add_harmonic[band] = bits_get(bs, 1);
        }
    }

    /* Check for Parametric Stereo (PS) extension payload */
    if (bits_get_consumed(bs) + 12 <= bs->len * 8) {
        bool ps_extended = bits_get(bs, 1);
        if (ps_extended) {
            uint32_t sync_ext = bits_get(bs, 11);
            if (sync_ext == 0x548) { /* Parametric Stereo Sync Header */
                ps_decode_payload(dec, bs);
            }
        }
    }

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

    /* Parametric Stereo (HE-AAC v2): Synthesize stereo L/R channels from Mono baseband */
    if (dec->ps_present && num_ch == 1) {
        dec->num_channels = 2;
        float qmf_ana_r[32][32];
        float qmf_ana_i[32][32];
        float qmf_left_r[32][64], qmf_left_i[32][64];
        float qmf_right_r[32][64], qmf_right_i[32][64];

        memset(qmf_left_r, 0, sizeof(qmf_left_r));
        memset(qmf_left_i, 0, sizeof(qmf_left_i));
        memset(qmf_right_r, 0, sizeof(qmf_right_r));
        memset(qmf_right_i, 0, sizeof(qmf_right_i));

        qmf_analysis_320(pcm_in, qmf_ana_r, qmf_ana_i);

        /* Apply Parametric Stereo IID pan gains across subbands */
        for (int t = 0; t < 32; t++) {
            for (int k = 0; k < 64; k++) {
                int band = (k * SBR_PS_BANDS) / 64;
                float src_r = (k < 32) ? qmf_ana_r[t][k] : qmf_ana_r[t][k - 32];
                float src_i = (k < 32) ? qmf_ana_i[t][k] : qmf_ana_i[t][k - 32];

                qmf_left_r[t][k]  = src_r * dec->ps.h11[band];
                qmf_left_i[t][k]  = src_i * dec->ps.h11[band];
                qmf_right_r[t][k] = src_r * dec->ps.h22[band];
                qmf_right_i[t][k] = src_i * dec->ps.h22[band];
            }
        }

        qmf_synthesis_640(qmf_left_r, qmf_left_i, pcm_out);
        qmf_synthesis_640(qmf_right_r, qmf_right_i, pcm_out + 2048);
        return;
    }

    /* Standard HE-AAC v1 SBR Synthesis */
    for (uint32_t ch = 0; ch < num_ch; ch++) {
        float qmf_ana_r[32][32];
        float qmf_ana_i[32][32];
        float qmf_syn_r[32][64];
        float qmf_syn_i[32][64];

        memset(qmf_syn_r, 0, sizeof(qmf_syn_r));
        memset(qmf_syn_i, 0, sizeof(qmf_syn_i));

        qmf_analysis_320(pcm_in + ch * FRAME_LEN_LONG, qmf_ana_r, qmf_ana_i);

        for (int t = 0; t < 32; t++) {
            for (int k = 0; k < 32; k++) {
                qmf_syn_r[t][k] = qmf_ana_r[t][k];
                qmf_syn_i[t][k] = qmf_ana_i[t][k];
            }
        }

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
