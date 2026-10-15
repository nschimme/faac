/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * Dynamic Range Control (DRC) unit tests.
 */

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "faac.h"
#include "bitstream.h"
#include "drc.h"

#ifndef M_PI_DOUBLE
#define M_PI_DOUBLE 3.141592653589793238462643383279502884
#endif

/*
 * Bitstream payload verification.
 */
static void test_bitstream_payloads(void)
{
    uint8_t buf[64];

    /* Test Case 1: Cut gain (-6.0 dB), no program reference level */
    {
        DRCInfo drc;
        memset(&drc, 0, sizeof(drc));
        drc.enabled = true;
        drc.gain_db = -6.0f;
        drc.prog_ref_level_present = false;

        memset(buf, 0, sizeof(buf));
        BitStream bs;
        InitBitStream(&bs, buf, sizeof(buf));
        int bits = DrcGetBits(&drc, &bs);
        assert(bits == 23); /* 7 header bits + 16 payload bits (2 bytes) */
        assert(buf[0] == 0xC5); /* 11000101 */
        assert(buf[1] == 0x61); /* 01100001 */
        assert(buf[2] == 0x30); /* 00110000 */
    }

    /* Test Case 2: Boost gain (+3.5 dB), program reference level present (-12.0 dBFS) */
    {
        DRCInfo drc;
        memset(&drc, 0, sizeof(drc));
        drc.enabled = true;
        drc.gain_db = 3.5f;
        drc.prog_ref_level_present = true;
        drc.prog_ref_level_db = -12.0f;

        memset(buf, 0, sizeof(buf));
        BitStream bs;
        InitBitStream(&bs, buf, sizeof(buf));
        int bits = DrcGetBits(&drc, &bs);
        assert(bits == 31); /* 7 header bits + 24 payload bits (3 bytes) */
        assert(buf[0] == 0xC7); /* 11000111 */
        assert(buf[1] == 0x62); /* 01100010 */
        assert(buf[2] == 0xC0); /* 11000000 */
        assert(buf[3] == 0x1C); /* 00011100 */
    }

    /* Test Case 3: DRC Disabled */
    {
        DRCInfo drc;
        memset(&drc, 0, sizeof(drc));
        drc.enabled = false;
        int bits = DrcGetBits(&drc, NULL);
        assert(bits == 0);
    }
}

/*
 * Helper to compute 32-bit FNV-1a hash of a memory buffer.
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
 * (a) Encode a deterministic synthetic signal with DRC off and compare
 *     output hash against constant captured from baseline.
 */
static void test_drc_off_baseline_hash(void)
{
    faac_params params;
    faac_params_init(&params, sizeof(params));
    params.sample_rate = 44100;
    params.num_channels = 2;
    params.object_type = FAAC_OBJ_LOW;
    params.bit_rate = 64000;
    params.output_format = FAAC_STREAM_ADTS;
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
    /* Baseline constant hash for this deterministic 20-frame encode */
    assert(hash == 0x4b84fe80u);
}

/*
 * (b) Encode with DRC enabled through faac_encoder_set_drc(), parse ADTS output,
 *     and assert a FIL element with EXT_DYNAMIC_RANGE (0xB) and expected sgn/ctl
 *     appears before the first SCE/CPE in each frame.
 */
static void test_drc_adts_parsing(void)
{
    faac_params params;
    faac_params_init(&params, sizeof(params));
    params.sample_rate = 44100;
    params.num_channels = 2;
    params.object_type = FAAC_OBJ_LOW;
    params.bit_rate = 64000;
    params.output_format = FAAC_STREAM_ADTS;
    params.input_format = FAAC_INPUT_16BIT;

    faac_encoder *enc = NULL;
    faac_status st = faac_encoder_open(&params, &enc);
    assert(st == FAAC_OK && enc != NULL);

    faac_drc_info drc;
    memset(&drc, 0, sizeof(drc));
    drc.enabled = true;
    drc.dynamic_range_gain = -6.0f; /* cut -6.0 dB: sgn=1, ctl=24 */
    drc.prog_ref_level_present = false;

    st = faac_encoder_set_drc(enc, &drc);
    assert(st == FAAC_OK);

    int16_t pcm[2048 * 2];
    for (int i = 0; i < 2048 * 2; i++) {
        pcm[i] = (int16_t)(16000.0 * sin(2.0 * M_PI_DOUBLE * 440.0 * (i / 2) / 44100.0));
    }

    uint8_t outbuf[8192];
    uint32_t bytes_written = 0;
    int frames_checked = 0;

    for (int frame = 0; frame < 15; frame++) {
        st = faac_encoder_encode(enc, pcm, 1024 * 2, outbuf, sizeof(outbuf), &bytes_written);
        assert(st == FAAC_OK);
        if (bytes_written > 7) { /* ADTS header is 7 bytes */
            /* Parse raw_data_block starting right after ADTS header (byte 7) */
            uint32_t b_idx = 7;
            /* Read ID_FIL (3 bits) */
            uint32_t id_fil = (outbuf[b_idx] >> 5) & 7;
            assert(id_fil == 6); /* ID_FIL = 6 */

            uint32_t cnt = (outbuf[b_idx] >> 1) & 0xF;
            assert(cnt == 2); /* 2 bytes payload */

            uint32_t ext_type = ((outbuf[b_idx] & 1) << 3) | ((outbuf[b_idx + 1] >> 5) & 7);
            assert(ext_type == 0xB); /* EXT_DYNAMIC_RANGE = 11 */

            uint32_t sgn = (outbuf[b_idx + 1] >> 0) & 1;
            assert(sgn == 1); /* sgn = 1 (cut) */

            uint32_t ctl = (outbuf[b_idx + 2] >> 1) & 0x7F;
            assert(ctl == 24); /* ctl = 24 (-6.0 dB / 0.25) */

            frames_checked++;
        }
    }

    faac_encoder_close(&enc);
    assert(frames_checked > 0);
}

/*
 * Test parameter validation for faac_encoder_set_drc.
 */
static void test_drc_invalid_inputs(void)
{
    faac_params params;
    faac_params_init(&params, sizeof(params));
    params.sample_rate = 44100;
    params.num_channels = 2;

    faac_encoder *enc = NULL;
    faac_status st = faac_encoder_open(&params, &enc);
    assert(st == FAAC_OK && enc != NULL);

    faac_drc_info drc;

    /* Out of range gain (> +31.75 dB) */
    memset(&drc, 0, sizeof(drc));
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

    faac_encoder_close(&enc);
}

int main(void)
{
    test_bitstream_payloads();
    test_drc_off_baseline_hash();
    test_drc_adts_parsing();
    test_drc_invalid_inputs();
    printf("DRC unit tests passed successfully.\n");
    return 0;
}
