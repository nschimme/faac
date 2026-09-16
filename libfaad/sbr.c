/*
 * Spectral Band Replication (SBR) & Parametric Stereo (PS) Decoder Engine
 */

#include "faad_internal.h"
#include "sfb_tables.h"
#include "sbr_tables.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


static int sbr_clamp_int(int val, int min_val, int max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

static int sbr_compute_num_bands(uint32_t sample_rate, uint32_t start_freq, uint32_t stop_freq) {
    if (sample_rate == 0) sample_rate = 44100;
    int sr_row = (sample_rate <= 16000) ? 0 : (sample_rate <= 22050) ? 1 : (sample_rate <= 24000) ? 2 : (sample_rate <= 32000) ? 3 : (sample_rate <= 64000) ? 4 : 5;
    int temp = (sample_rate < 32000) ? 3000 : (sample_rate < 64000) ? 4000 : 5000;
    int start_min = ((temp << 7) + (int)(sample_rate >> 1)) / (int)sample_rate;
    int kx = sbr_clamp_int(start_min + sbr_offset[sr_row][start_freq & 15], 1, 63);
    int k2 = 64;
    if (stop_freq < 14) {
        int stop_min = ((temp << 8) + (int)(sample_rate >> 1)) / (int)sample_rate;
        k2 = sbr_clamp_int(stop_min + sbr_offset[sr_row][stop_freq & 15], kx + 1, 64);
    }
    int num_bands = k2 - kx;
    if (num_bands < 1) num_bands = 1;
    if (num_bands > 48) num_bands = 48;
    return num_bands;
}


static float qmf_syn_cos_lut[64][64];
static float qmf_syn_sin_lut[64][64];
static float qmf_ana_cos_lut[32][32];
static float qmf_ana_sin_lut[32][32];
static bool qmf_twiddles_init = false;

static void init_qmf_twiddles(void)
{
    if (qmf_twiddles_init) return;

    for (int n = 0; n < 64; n++) {
        for (int k = 0; k < 64; k++) {
            float angle = (float)M_PI * (k + 0.5f) * (n - 0.25f) / 64.0f;
            qmf_syn_cos_lut[n][k] = cosf(angle);
            qmf_syn_sin_lut[n][k] = sinf(angle);
        }
    }

    for (int k = 0; k < 32; k++) {
        for (int n = 0; n < 32; n++) {
            float angle = (float)M_PI * (k + 0.5f) * (n - 0.5f) / 32.0f;
            qmf_ana_cos_lut[k][n] = cosf(angle);
            qmf_ana_sin_lut[k][n] = sinf(angle);
        }
    }

    qmf_twiddles_init = true;
}

static const float ps_iid_scale_lut[15] = {
    0.000f, 0.125f, 0.250f, 0.375f, 0.500f, 0.625f, 0.750f, 0.875f,
    1.000f, 1.125f, 1.250f, 1.375f, 1.500f, 1.750f, 2.000f
};

static int sbr_decode_huffman_env_delta(BitReader *bs, const SBRHuffEntry *table, int nsyms, int offset)
{
    uint32_t val = 0;
    int len = 0;
    while (len < 20) {
        val = (val << 1) | bits_get(bs, 1);
        len++;
        for (int i = 0; i < nsyms; i++) {
            if (table[i].len == len && table[i].code == val) {
                return i - offset;
            }
        }
    }
    return 0;
}

static void ps_decode_payload(struct faad_decoder *dec, BitReader *bs)
{
    PSState *ps = &dec->ps;
    dec->ps_present = true;

    ps->enable_iid = bits_get(bs, 1);
    if (ps->enable_iid) {
        bool iid_mode = bits_get(bs, 1);
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
        bits_skip(bs, 2);
    }

    /* SBR Frame Grid Decoding */
    sbr->bs_frame_class = bits_get(bs, 2);
    sbr->bs_num_env = bits_get(bs, 2) + 1;

    /* SBR Inverse Filtering Mode */
    for (int i = 0; i < 4; i++) {
        bits_skip(bs, 2);
    }

    /* SBR Envelope Data (E_orig) decoding using ISO/IEC 14496-3 SBR Huffman tables */
    bool bs_amp_res = bits_get(bs, 1);
    const SBRHuffEntry *huff_tab = bs_amp_res ? f_huff_env_1_5dB : f_huff_env_3_0dB;
    int huff_nsyms = bs_amp_res ? F_HUFF_ENV_1_5DB_NSYMS : F_HUFF_ENV_3_0DB_NSYMS;
    int huff_offset = bs_amp_res ? F_HUFF_ENV_1_5DB_OFFSET : F_HUFF_ENV_3_0DB_OFFSET;

    int num_bands = sbr_compute_num_bands(dec->asc.sbr_sample_rate > 0 ? dec->asc.sbr_sample_rate : 2 * dec->sample_rate, sbr->bs_start_freq, sbr->bs_stop_freq);
    for (int env = 0; env < sbr->bs_num_env && env < 8; env++) {
        bool bs_df_env = bits_get(bs, 1);
        int prev_val = bs_amp_res ? 60 : 30;

        for (int band = 0; band < num_bands; band++) {
            if (env == 0 && !bs_df_env) {
                /* First envelope, frequency direction: absolute value or delta */
                if (band == 0) {
                    prev_val = bits_get(bs, bs_amp_res ? 7 : 6);
                } else {
                    int delta = sbr_decode_huffman_env_delta(bs, huff_tab, huff_nsyms, huff_offset);
                    prev_val += delta;
                }
            } else if (bs_df_env) {
                /* Time direction delta coding from previous envelope */
                int delta = sbr_decode_huffman_env_delta(bs, huff_tab, huff_nsyms, huff_offset);
                prev_val = sbr->E_orig[env == 0 ? 0 : env - 1][band] + delta;
            } else {
                /* Frequency direction delta coding from previous band */
                int delta = sbr_decode_huffman_env_delta(bs, huff_tab, huff_nsyms, huff_offset);
                prev_val += delta;
            }

            sbr->E_orig[env][band] = (int8_t)prev_val;
        }
    }

    /* SBR Noise Floor Data (Q_orig) */
    sbr->bs_num_noise = (sbr->bs_num_env > 1) ? 2 : 1;
    for (int n = 0; n < sbr->bs_num_noise && n < 8; n++) {
        bool bs_df_noise = bits_get(bs, 1);
        int prev_val = 30;
        for (int band = 0; band < 5; band++) {
            if (n == 0 && !bs_df_noise) {
                if (band == 0) {
                    prev_val = bits_get(bs, 5);
                } else {
                    int delta = sbr_decode_huffman_env_delta(bs, f_huff_env_3_0dB, F_HUFF_ENV_3_0DB_NSYMS, F_HUFF_ENV_3_0DB_OFFSET);
                    prev_val += delta;
                }
            } else if (bs_df_noise) {
                int delta = sbr_decode_huffman_env_delta(bs, f_huff_env_3_0dB, F_HUFF_ENV_3_0DB_NSYMS, F_HUFF_ENV_3_0DB_OFFSET);
                prev_val = sbr->Q_orig[n == 0 ? 0 : n - 1][band] + delta;
            } else {
                int delta = sbr_decode_huffman_env_delta(bs, f_huff_env_3_0dB, F_HUFF_ENV_3_0DB_NSYMS, F_HUFF_ENV_3_0DB_OFFSET);
                prev_val += delta;
            }
            sbr->Q_orig[n][band] = (int8_t)prev_val;
        }
    }

    /* SBR Synthetics / Harmonics */
    bool bs_add_harmonic_flag = bits_get(bs, 1);
    if (bs_add_harmonic_flag) {
        for (int band = 0; band < num_bands; band++) {
            sbr->bs_add_harmonic[band] = bits_get(bs, 1);
        }
    }

    /* Check for Parametric Stereo (PS) extension payload */
    if (bits_get_consumed(bs) + 12 <= bs->len * 8) {
        bool ps_extended = bits_get(bs, 1);
        if (ps_extended) {
            uint32_t sync_ext = bits_get(bs, 11);
            if (sync_ext == 0x548) {
                ps_decode_payload(dec, bs);
            }
        }
    }

    return FAAD_OK;
}

/* 32-subband QMF analysis filterbank with 320-tap prototype windowing and persistent state */
static void qmf_analysis_320(SBRState *sbr, const float *in, float qmf_real[32][32], float qmf_imag[32][32])
{
    init_qmf_twiddles();
    float *ovl = sbr ? sbr->qmf_ana_ovl : NULL;
    float local_ovl[320];
    if (!ovl) {
        memset(local_ovl, 0, sizeof(local_ovl));
        ovl = local_ovl;
    }

    for (int t = 0; t < 32; t++) {
        memmove(&ovl[0], &ovl[32], 288 * sizeof(float));
        for (int n = 0; n < 32; n++) {
            ovl[288 + n] = in[t * 32 + n];
        }

        float samples[32];
        for (int n = 0; n < 32; n++) {
            float sample = 0.0f;
            for (int j = 0; j < 10; j++) {
                int idx = j * 64 + 2 * n;
                sample += ovl[j * 32 + n] * qmf_c[idx];
            }
            samples[n] = sample;
        }

        for (int k = 0; k < 32; k++) {
            float sum_r = 0.0f;
            float sum_i = 0.0f;
            const float *cos_row = qmf_ana_cos_lut[k];
            const float *sin_row = qmf_ana_sin_lut[k];
            for (int n = 0; n < 32; n++) {
                float s = samples[n];
                sum_r += s * cos_row[n];
                sum_i += s * sin_row[n];
            }
            qmf_real[t][k] = sum_r * 0.03125f;
            qmf_imag[t][k] = sum_i * 0.03125f;
        }
    }
}

/* 64-subband QMF synthesis filterbank with 640-sample overlapping delay line history */
static void qmf_synthesis_640(SBRState *sbr, float qmf_real[32][64], float qmf_imag[32][64], float *out)
{
    init_qmf_twiddles();
    for (int t = 0; t < 32; t++) {
        /* Shift 640-sample QMF delay line history by 64 samples */
        memmove(&sbr->qmf_ovl[0], &sbr->qmf_ovl[64], 576 * sizeof(float));

        /* Fast precalculated twiddle 64-subband IDFT for current slot with restrict pointers */
        const float * restrict re_ptr = qmf_real[t];
        const float * restrict im_ptr = qmf_imag[t];

        for (int n = 0; n < 64; n++) {
            float sum = 0.0f;
            const float * restrict c_row = qmf_syn_cos_lut[n];
            const float * restrict s_row = qmf_syn_sin_lut[n];
            for (int k = 0; k < 64; k++) {
                sum += re_ptr[k] * c_row[k] - im_ptr[k] * s_row[k];
            }
            sbr->qmf_ovl[576 + n] = sum * 0.03125f;
        }

        /* Extract 64 time-domain output samples with unit-stride auto-vectorizable inner loop */
        const float * restrict ovl_ptr = sbr->qmf_ovl;
        const float * restrict win_ptr = qmf_c;
        float * restrict out_ptr = out + t * 64;

        for (int n = 0; n < 64; n++) {
            float sample = ovl_ptr[n] * win_ptr[n]
                         + ovl_ptr[64 + n] * win_ptr[64 + n]
                         + ovl_ptr[128 + n] * win_ptr[128 + n]
                         + ovl_ptr[192 + n] * win_ptr[192 + n]
                         + ovl_ptr[256 + n] * win_ptr[256 + n]
                         + ovl_ptr[320 + n] * win_ptr[320 + n]
                         + ovl_ptr[384 + n] * win_ptr[384 + n]
                         + ovl_ptr[448 + n] * win_ptr[448 + n]
                         + ovl_ptr[512 + n] * win_ptr[512 + n]
                         + ovl_ptr[576 + n] * win_ptr[576 + n];
            out_ptr[n] = sample;
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

        qmf_analysis_320(&dec->sbr[0], pcm_in, qmf_ana_r, qmf_ana_i);

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

        qmf_synthesis_640(&dec->sbr[0], qmf_left_r, qmf_left_i, pcm_out);
        qmf_synthesis_640(&dec->sbr[1], qmf_right_r, qmf_right_i, pcm_out + 2048);
        return;
    }

    /* Standard HE-AAC v1 SBR Synthesis */
    for (uint32_t ch = 0; ch < num_ch; ch++) {
        SBRState *sbr = &dec->sbr[ch];
        float qmf_ana_r[32][32];
        float qmf_ana_i[32][32];
        float qmf_syn_r[32][64];
        float qmf_syn_i[32][64];

        memset(qmf_syn_r, 0, sizeof(qmf_syn_r));
        memset(qmf_syn_i, 0, sizeof(qmf_syn_i));

        qmf_analysis_320(sbr, pcm_in + ch * FRAME_LEN_LONG, qmf_ana_r, qmf_ana_i);

        for (int t = 0; t < 32; t++) {
            for (int k = 0; k < 32; k++) {
                qmf_syn_r[t][k] = qmf_ana_r[t][k];
                qmf_syn_i[t][k] = qmf_ana_i[t][k];
            }
        }

        /* High Frequency Reconstruction with Envelope Scalefactor Gain Control */
        for (int t = 0; t < 32; t++) {
            int env_idx = (t * sbr->bs_num_env) / 32;
            if (env_idx >= 8) env_idx = 7;

            for (int k = 32; k < 64; k++) {
                int src_k = k - 32;
                int band_idx = (k - 32) * 48 / 32;
                float gain = 1.0f;

                int e_val = sbr->E_orig[env_idx][band_idx];
                gain = powf(2.0f, 0.25f * (e_val - 20));

                qmf_syn_r[t][k] = qmf_ana_r[t][src_k] * gain;
                qmf_syn_i[t][k] = qmf_ana_i[t][src_k] * gain;
            }
        }

        qmf_synthesis_640(sbr, qmf_syn_r, qmf_syn_i, pcm_out + ch * 2048);
    }
}
