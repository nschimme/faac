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

static void test_bitstream_payloads(void)
{
    uint8_t buf[64];

    /* Test Case 1: Cut gain (-6.0 dB), no program reference level */
    {
        memset(buf, 0, sizeof(buf));
        BitStream bs;
        InitBitStream(&bs, buf, sizeof(buf));
        DRCInfo drc = { .enabled = true, .gain_db = -6.0f, .prog_ref_level_present = false };
        int bits = DrcGetBits(&drc, &bs);
        assert(bits == 23); /* 7 header bits + 16 payload bits (2 bytes) */
        assert(buf[0] == 0xC5); /* 11000101 */
        assert(buf[1] == 0x61); /* 01100001 */
        assert(buf[2] == 0x30); /* 00110000 */
    }

    /* Test Case 2: Boost gain (+3.5 dB), program reference level present (-12.0 dBFS) */
    {
        memset(buf, 0, sizeof(buf));
        BitStream bs;
        InitBitStream(&bs, buf, sizeof(buf));
        DRCInfo drc = {
            .enabled = true,
            .gain_db = 3.5f,
            .prog_ref_level_present = true,
            .prog_ref_level_db = -12.0f
        };
        int bits = DrcGetBits(&drc, &bs);
        assert(bits == 31); /* 7 header bits + 24 payload bits (3 bytes) */
        assert(buf[0] == 0xC7); /* 11000111 */
        assert(buf[1] == 0x62); /* 01100010 */
        assert(buf[2] == 0xC0); /* 11000000 */
        assert(buf[3] == 0x1C); /* 00011100 */
    }

    /* Test Case 3: DRC Disabled */
    {
        DRCInfo drc = { .enabled = false };
        int bits = DrcGetBits(&drc, NULL);
        assert(bits == 0);
    }
}

static void test_drc_off_identity(void)
{
    faac_params params;
    faac_params_init(&params, sizeof(params));
    params.sample_rate = 44100;
    params.num_channels = 2;
    params.object_type = FAAC_OBJ_LOW;
    params.bit_rate = 64000;
    params.output_format = FAAC_STREAM_ADTS;
    params.input_format = FAAC_INPUT_16BIT;

    faac_encoder *enc1 = NULL, *enc2 = NULL;
    faac_status st = faac_encoder_open(&params, &enc1);
    assert(st == FAAC_OK && enc1 != NULL);
    st = faac_encoder_open(&params, &enc2);
    assert(st == FAAC_OK && enc2 != NULL);

    /* Explicitly disable DRC on enc2 */
    faac_drc_info drc = { .enabled = false };
    st = faac_encoder_set_drc(enc2, &drc);
    assert(st == FAAC_OK);

    int16_t pcm[2048 * 2];
    for (int i = 0; i < 2048 * 2; i++) {
        pcm[i] = (int16_t)(16000.0 * sin(2.0 * M_PI * 440.0 * (i / 2) / 44100.0));
    }

    uint8_t out1[8192], out2[8192];
    uint32_t bytes1 = 0, bytes2 = 0;

    for (int frame = 0; frame < 10; frame++) {
        st = faac_encoder_encode(enc1, pcm, 1024 * 2, out1, sizeof(out1), &bytes1);
        assert(st == FAAC_OK);
        st = faac_encoder_encode(enc2, pcm, 1024 * 2, out2, sizeof(out2), &bytes2);
        assert(st == FAAC_OK);
        assert(bytes1 == bytes2);
        assert(memcmp(out1, out2, bytes1) == 0);
    }

    faac_encoder_close(&enc1);
    faac_encoder_close(&enc2);
}

int main(void)
{
    test_bitstream_payloads();
    test_drc_off_identity();
    printf("DRC unit tests passed successfully.\n");
    return 0;
}
