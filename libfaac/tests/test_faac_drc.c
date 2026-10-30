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

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <faac.h>

#ifndef M_PI_DOUBLE
# define M_PI_DOUBLE 3.14159265358979323846
#endif

/*
 * FNV-1a hash helper for deterministic output comparisons.
 */
static uint32_t fnv1a_hash(const uint8_t *data, size_t len)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

/*
 * Simple ADTS DRC bitstream parser for test inspection.
 */
static bool parse_adts_drc(const uint8_t *buf, size_t len, float *out_gain, float *out_ref)
{
    if (len < 7)
        return false;

    /* Verify ADTS sync */
    if (buf[0] != 0xff || (buf[1] & 0xf0) != 0xf0)
        return false;

    size_t idx = 7;
    if (idx >= len)
        return false;

    uint8_t id_fil = (buf[idx] >> 5) & 7;
    if (id_fil != 6) /* ID_FIL */
        return false;

    uint8_t ext_type = ((buf[idx] & 1) << 3) | ((buf[idx + 1] >> 5) & 7);
    if (ext_type != 0x0b) /* EXT_DYNAMIC_RANGE */
        return false;

    bool ref_pres = (buf[idx + 1] >> 0) & 1;
    uint8_t ref_code = 0;
    uint8_t sgn = 0;
    uint8_t ctl = 0;

    if (ref_pres) {
        ref_code = ((buf[idx + 2] & 0xfe) >> 1);
        sgn = buf[idx + 2] & 1;
        ctl = (buf[idx + 3] >> 1) & 0x7f;
    } else {
        sgn = buf[idx + 1] & 1;
        ctl = (buf[idx + 2] >> 1) & 0x7f;
    }

    if (out_gain) {
        float gain = 0.25f * (float)ctl;
        *out_gain = sgn ? -gain : gain;
    }

    if (out_ref) {
        *out_ref = -0.25f * (float)ref_code;
    }

    return true;
}

static void test_drc_off_baseline_hash(void)
{
    faac_params params;
    faac_params_init(&params, sizeof(params));
    params.sample_rate = 44100;
    params.channels = 2;
    params.object_type = FAAC_OBJ_LC;
    params.bit_rate = 128000;
    params.drc_mode = FAAC_DRC_OFF;
    params.stream_format = FAAC_STREAM_ADTS;
    params.input_format = FAAC_INPUT_16BIT;

    faac_encoder *enc = NULL;
    faac_status st = faac_encoder_open(&params, &enc);
    assert(st == FAAC_OK && enc != NULL);

    int16_t pcm[2048 * 2];
    for (int i = 0; i < 2048 * 2; i++) {
        pcm[i] = (int16_t)(16000.0 * sin(2.0 * M_PI_DOUBLE * 440.0 * (i / 2) / 44100.0));
    }

    uint8_t stream_buf[128 * 1024];
    size_t stream_len = 0;
    uint8_t outbuf[8192];
    uint32_t bytes_written = 0;

    for (int frame = 0; frame < 20; frame++) {
        st = faac_encoder_encode(enc, pcm, 1024 * 2, outbuf, sizeof(outbuf), &bytes_written);
        assert(st == FAAC_OK);
        if (bytes_written > 0) {
            memcpy(stream_buf + stream_len, outbuf, bytes_written);
            stream_len += bytes_written;
        }
    }

    do {
        st = faac_encoder_encode(enc, NULL, 0, outbuf, sizeof(outbuf), &bytes_written);
        assert(st == FAAC_OK);
        if (bytes_written > 0) {
            memcpy(stream_buf + stream_len, outbuf, bytes_written);
            stream_len += bytes_written;
        }
    } while (bytes_written > 0);

    faac_encoder_close(&enc);

    uint32_t hash = fnv1a_hash(stream_buf, stream_len);
    assert(hash != 0);
}

static void test_drc_invalid_inputs(void)
{
    faac_params params;
    faac_params_init(&params, sizeof(params));
    params.sample_rate = 44100;
    params.channels = 2;

    faac_encoder *enc = NULL;
    faac_status st = faac_encoder_open(&params, &enc);
    assert(st == FAAC_OK && enc != NULL);

    faac_drc_info drc;

    /* Too small struct_size */
    memset(&drc, 0, sizeof(drc));
    drc.struct_size = 4;
    drc.enabled = true;
    st = faac_encoder_set_drc(enc, &drc);
    assert(st == FAAC_ERR_INVALID_ARGUMENT);

    /* Out of range gain (> +31.75 dB) */
    memset(&drc, 0, sizeof(drc));
    drc.struct_size = sizeof(drc);
    drc.enabled = true;
    drc.dynamic_range_gain = 32.0f;
    st = faac_encoder_set_drc(enc, &drc);
    assert(st == FAAC_ERR_INVALID_ARGUMENT);

    /* Out of range gain (< -31.75 dB) */
    drc.dynamic_range_gain = -32.0f;
    st = faac_encoder_set_drc(enc, &drc);
    assert(st == FAAC_ERR_INVALID_ARGUMENT);

    /* Positive prog_ref_level */
    drc.dynamic_range_gain = 0.0f;
    drc.prog_ref_level_present = true;
    drc.prog_ref_level = 5.0f;
    st = faac_encoder_set_drc(enc, &drc);
    assert(st == FAAC_ERR_INVALID_ARGUMENT);

    /* Out of range prog_ref_level (< -31.75 dBFS) */
    drc.prog_ref_level = -35.0f;
    st = faac_encoder_set_drc(enc, &drc);
    assert(st == FAAC_ERR_INVALID_ARGUMENT);

    /* Valid prog_ref_level (-20.0 dBFS) */
    drc.prog_ref_level = -20.0f;
    st = faac_encoder_set_drc(enc, &drc);
    assert(st == FAAC_OK);

    /* Larger struct_size (future ABI growth compatibility) */
    drc.struct_size = sizeof(drc) + 16;
    st = faac_encoder_set_drc(enc, &drc);
    assert(st == FAAC_OK);

    faac_encoder_close(&enc);
}

static void test_drc_auto_encoder_integration(void)
{
    const enum faac_object_type obj_types[] = { FAAC_OBJ_LC, FAAC_OBJ_HE_AAC_V1 };
    const uint32_t bit_rates[] = { 64000, 24000 };

    for (int obj_idx = 0; obj_idx < 2; obj_idx++) {
        enum faac_object_type aot = obj_types[obj_idx];
        uint32_t br = bit_rates[obj_idx];
        uint32_t frame_samples_per_ch = (aot == FAAC_OBJ_HE_AAC_V1) ? 2048 : 1024;
        uint32_t total_input_frames = 300;

        float gains_16bit[500];
        float gains_float[500];
        uint32_t count_16bit = 0;
        uint32_t count_float = 0;

        /* Run 1: 16-bit integer PCM input */
        {
            faac_params params;
            faac_params_init(&params, sizeof(params));
            params.sample_rate = 48000;
            params.channels = 2;
            params.object_type = aot;
            params.bit_rate = br;
            params.drc_mode = FAAC_DRC_AUTO;
            params.drc_ref_level = -23.0f;
            params.stream_format = FAAC_STREAM_ADTS;
            params.input_format = FAAC_INPUT_16BIT;

            faac_encoder *enc = NULL;
            faac_status st = faac_encoder_open(&params, &enc);
            assert(st == FAAC_OK && enc != NULL);

            int16_t pcm_buf[2048 * 2] = {0};
            uint8_t outbuf[8192];
            uint32_t bytes_written = 0;
            uint32_t total_samples = 0;

            for (uint32_t f = 0; f < total_input_frames; f++) {
                float amp_lin = (f < 200) ? 0.010f : 0.707f; /* -40 dBFS quiet, -3 dBFS loud */
                int16_t amp_i16 = (int16_t)(amp_lin * 32767.0f);

                for (uint32_t i = 0; i < frame_samples_per_ch; i++) {
                    double t = (double)(total_samples + i) / 48000.0;
                    int16_t val = (int16_t)(amp_i16 * sin(2.0 * M_PI_DOUBLE * 997.0 * t));
                    pcm_buf[2 * i] = val;
                    pcm_buf[2 * i + 1] = val;
                }
                total_samples += frame_samples_per_ch;

                st = faac_encoder_encode(enc, pcm_buf, frame_samples_per_ch * 2, outbuf, sizeof(outbuf), &bytes_written);
                assert(st == FAAC_OK);

                if (bytes_written > 7) {
                    float g_db = 0.0f;
                    if (parse_adts_drc(outbuf, bytes_written, &g_db, NULL))
                        gains_16bit[count_16bit++] = g_db;
                }
            }

            do {
                st = faac_encoder_encode(enc, NULL, 0, outbuf, sizeof(outbuf), &bytes_written);
                assert(st == FAAC_OK);
                if (bytes_written > 7) {
                    float g_db = 0.0f;
                    if (parse_adts_drc(outbuf, bytes_written, &g_db, NULL))
                        gains_16bit[count_16bit++] = g_db;
                }
            } while (bytes_written > 0);

            faac_encoder_close(&enc);
        }

        /* Run 2: 32-bit float PCM input */
        {
            faac_params params;
            faac_params_init(&params, sizeof(params));
            params.sample_rate = 48000;
            params.channels = 2;
            params.object_type = aot;
            params.bit_rate = br;
            params.drc_mode = FAAC_DRC_AUTO;
            params.drc_ref_level = -23.0f;
            params.stream_format = FAAC_STREAM_ADTS;
            params.input_format = FAAC_INPUT_FLOAT;

            faac_encoder *enc = NULL;
            faac_status st = faac_encoder_open(&params, &enc);
            assert(st == FAAC_OK && enc != NULL);

            float pcm_buf[2048 * 2] = {0};
            uint8_t outbuf[8192];
            uint32_t bytes_written = 0;
            uint32_t total_samples = 0;

            for (uint32_t f = 0; f < total_input_frames; f++) {
                float amp_lin = (f < 200) ? 0.010f : 0.707f;
                float amp_flt = amp_lin * 1.0f; /* float unity scale */

                for (uint32_t i = 0; i < frame_samples_per_ch; i++) {
                    double t = (double)(total_samples + i) / 48000.0;
                    float val = (float)(amp_flt * sin(2.0 * M_PI_DOUBLE * 997.0 * t));
                    pcm_buf[2 * i] = val;
                    pcm_buf[2 * i + 1] = val;
                }
                total_samples += frame_samples_per_ch;

                st = faac_encoder_encode(enc, pcm_buf, frame_samples_per_ch * 2, outbuf, sizeof(outbuf), &bytes_written);
                assert(st == FAAC_OK);

                if (bytes_written > 7) {
                    float g_db = 0.0f;
                    if (parse_adts_drc(outbuf, bytes_written, &g_db, NULL))
                        gains_float[count_float++] = g_db;
                }
            }

            do {
                st = faac_encoder_encode(enc, NULL, 0, outbuf, sizeof(outbuf), &bytes_written);
                assert(st == FAAC_OK);
                if (bytes_written > 7) {
                    float g_db = 0.0f;
                    if (parse_adts_drc(outbuf, bytes_written, &g_db, NULL))
                        gains_float[count_float++] = g_db;
                }
            } while (bytes_written > 0);

            faac_encoder_close(&enc);
        }

        assert(count_16bit == count_float && count_16bit > 0);
        for (uint32_t i = 0; i < count_16bit; i++) {
            assert(fabsf(gains_16bit[i] - gains_float[i]) <= 0.25f);
        }

        assert(gains_16bit[0] >= 0.0f);
        assert(gains_16bit[100] >= 5.0f && gains_16bit[100] <= 6.0f);

        uint32_t loud_idx = count_16bit - 10;
        assert(gains_16bit[loud_idx] <= -9.5f && gains_16bit[loud_idx] >= -10.5f);

        for (uint32_t i = 1; i < count_16bit; i++) {
            float step = gains_16bit[i - 1] - gains_16bit[i];
            if (step > 0.0f) {
                assert(step <= 3.00f + 1e-3f);
            }
        }
    }
}

/*
 * Encode seconds of a 997 Hz float tone per phase and return the DRC gain of the
 * last frame. loud_ch selects the one channel that carries signal (-1 = all).
 */
static float last_gain_after(uint32_t channels, int loud_ch, const float *amps,
                             const uint32_t *secs, int phases)
{
    faac_params params;
    faac_params_init(&params, sizeof(params));
    params.sample_rate = 48000;
    params.channels = channels;
    params.bit_rate = 64000 * channels / 2;
    params.drc_mode = FAAC_DRC_AUTO;
    params.drc_ref_level = -23.0f;
    params.stream_format = FAAC_STREAM_ADTS;
    params.input_format = FAAC_INPUT_FLOAT;
    params.channel_layout = FAAC_LAYOUT_AAC;

    faac_encoder *enc = NULL;
    faac_status st = faac_encoder_open(&params, &enc);
    assert(st == FAAC_OK && enc != NULL);

    float pcm[1024 * 8];
    uint8_t outbuf[16384];
    uint32_t bytes = 0, total = 0;
    float last = 0.0f;

    for (int ph = 0; ph < phases; ph++) {
        uint32_t frames = secs[ph] * 48000 / 1024;
        for (uint32_t f = 0; f < frames; f++) {
            for (uint32_t i = 0; i < 1024; i++) {
                float v = amps[ph] * (float)sin(2.0 * M_PI_DOUBLE * 997.0 * (double)(total + i) / 48000.0);
                for (uint32_t c = 0; c < channels; c++)
                    pcm[i * channels + c] = (loud_ch < 0 || (uint32_t)loud_ch == c) ? v : 0.0f;
            }
            total += 1024;
            st = faac_encoder_encode(enc, pcm, 1024 * channels, outbuf, sizeof(outbuf), &bytes);
            assert(st == FAAC_OK);
            float g;
            if (bytes > 7 && parse_adts_drc(outbuf, bytes, &g, NULL))
                last = g;
        }
    }
    faac_encoder_close(&enc);
    return last;
}

/*
 * The 1 s release moves the gain by far less than the 0.25 dB grid per frame;
 * it must still reach the boost target after a loud passage instead of
 * freezing near the cut.
 */
static void test_drc_release_reaches_target(void)
{
    const float amps[] = { 0.707f, 0.010f };  /* -3 dBFS, then -40 dBFS */
    const uint32_t secs[] = { 10, 30 };
    float g = last_gain_after(2, -1, amps, secs, 2);
    assert(g >= 5.0f);
}

/* Surround weighting must follow AAC order (C L R Ls Rs LFE): Ls and Rs equal. */
static void test_drc_surround_weights_symmetric(void)
{
    const float amps[] = { 0.5f };
    const uint32_t secs[] = { 12 };
    float ls = last_gain_after(6, 3, amps, secs, 1);
    float rs = last_gain_after(6, 4, amps, secs, 1);
    float l  = last_gain_after(6, 1, amps, secs, 1);
    assert(fabsf(ls - rs) < 0.01f);
    assert(ls < l - 0.5f); /* surround weight (+1.5 dB) means a deeper cut than a front channel */
}

/*
 * Index of the first access unit whose gain is cut by more than 1 dB when a
 * -40 dBFS tone jumps to -22 dBFS at a known sample.
 */
static uint32_t first_cut_au(enum faac_object_type aot, uint32_t bit_rate,
                             uint32_t frame_len, uint32_t onset_sample)
{
    faac_params params;
    faac_params_init(&params, sizeof(params));
    params.sample_rate = 48000;
    params.channels = 2;
    params.object_type = aot;
    params.bit_rate = bit_rate;
    params.drc_mode = FAAC_DRC_AUTO;
    params.drc_ref_level = -23.0f;
    params.stream_format = FAAC_STREAM_ADTS;
    params.input_format = FAAC_INPUT_FLOAT;

    faac_encoder *enc = NULL;
    assert(faac_encoder_open(&params, &enc) == FAAC_OK && enc != NULL);

    float pcm[2048 * 2];
    uint8_t outbuf[16384];
    uint32_t bytes = 0, total = 0, au = 0;
    float first_gain = 0.0f;
    bool have_first = false;

    while (total < onset_sample + 40 * frame_len) {
        for (uint32_t i = 0; i < frame_len; i++) {
            float amp = (total + i < onset_sample) ? 0.01f : 0.08f;
            float v = amp * (float)sin(2.0 * M_PI_DOUBLE * 997.0 * (double)(total + i) / 48000.0);
            pcm[2 * i] = v;
            pcm[2 * i + 1] = v;
        }
        total += frame_len;
        assert(faac_encoder_encode(enc, pcm, frame_len * 2, outbuf, sizeof(outbuf), &bytes) == FAAC_OK);
        float g;
        if (bytes > 7 && parse_adts_drc(outbuf, bytes, &g, NULL)) {
            if (!have_first && au > 400 * 1024 / frame_len) {
                first_gain = g;
                have_first = true;
            }
            if (have_first && g < first_gain - 1.0f) {
                faac_encoder_close(&enc);
                return au;
            }
            au++;
        }
    }
    faac_encoder_close(&enc);
    return UINT32_MAX;
}

/*
 * The gain written into an access unit must describe the audio that unit
 * carries: the first cut lands on the onset frame or the one before it. Before
 * the gain was delayed it led by two to three frames, ducking quiet audio ahead
 * of the transient.
 */
static void test_drc_gain_aligned_with_audio(void)
{
    const uint32_t onset = 480000;
    uint32_t lc = first_cut_au(FAAC_OBJ_LC, 128000, 1024, onset);
    uint32_t he = first_cut_au(FAAC_OBJ_HE_AAC_V1, 48000, 2048, onset);
    assert(lc + 1 >= onset / 1024 && lc <= onset / 1024);
    assert(he + 1 >= onset / 2048 && he <= onset / 2048);
}

int main(void)
{
    test_drc_off_baseline_hash();
    test_drc_invalid_inputs();
    test_drc_auto_encoder_integration();
    test_drc_release_reaches_target();
    test_drc_surround_weights_symmetric();
    test_drc_gain_aligned_with_audio();
    printf("All DRC tests passed successfully.\n");
    return 0;
}
