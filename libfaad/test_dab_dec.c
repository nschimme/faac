/*
 * FAAD - Freeware Advanced Audio Decoder
 * Unit test for DAB+ 960-sample frame size and ADIF decoding
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "faad.h"

int main(void)
{
    faad_library_info lib_info;
    lib_info.struct_size = sizeof(lib_info);
    faad_status st = faad_get_library_info(&lib_info);
    assert(st == FAAD_OK);
    printf("Testing FAAD version %s (SBR: %d, PS: %d)\n", lib_info.version, lib_info.sbr_supported, lib_info.ps_supported);

    /* Test 1: ASC build with 960 frame length (frame_length_flag = 1) */
    /* 5 bits AOT=2 (00010), 4 bits sr_idx=4 (0100), 4 bits channels=2 (0010), 1 bit frameLengthFlag=1, 1 bit dependsOnCoreCoder=0, 1 bit extensionFlag=0 */
    /* 0001 0010 0001 0100 -> 0x12, 0x14 */
    uint8_t asc_960[2] = { 0x12, 0x14 };

    faad_config cfg;
    faad_config_init(&cfg, sizeof(cfg));
    cfg.stream_format = FAAD_STREAM_RAW;
    cfg.output_format = FAAD_OUTPUT_16BIT;

    faad_decoder *dec = NULL;
    st = faad_decoder_open(&cfg, asc_960, sizeof(asc_960), &dec);
    assert(st == FAAD_OK);
    assert(dec != NULL);

    faad_stream_info info;
    info.struct_size = sizeof(info);
    st = faad_decoder_get_info(dec, &info);
    assert(st == FAAD_OK);
    assert(info.frame_samples == 960);
    assert(info.sample_rate == 44100);
    assert(info.channels == 2);
    printf("ASC 960 frame samples: %u, rate: %u, channels: %u\n", info.frame_samples, info.sample_rate, info.channels);

    faad_decoder_close(&dec);
    assert(dec == NULL);

    /* Test 2: ADIF header parsing */
    uint8_t adif_buf[17] = {
        0x41, 0x44, 0x49, 0x46, 0x00, 0x3E, 0x80, 0x00,
        0x10, 0x00, 0x00, 0xA0, 0x80, 0x00, 0x04, 0x00, 0x00
    };
    cfg.stream_format = FAAD_STREAM_ADIF;
    st = faad_decoder_open(&cfg, NULL, 0, &dec);
    assert(st == FAAD_OK);

    info.struct_size = sizeof(info);
    faad_decoder_get_info(dec, &info);

    uint32_t consumed = 0, written = 0, flags = 0;
    uint8_t pcm_out[65536];
    st = faad_decode_frame(dec, adif_buf, sizeof(adif_buf), &consumed, pcm_out, sizeof(pcm_out), &written, &flags);
    printf("faad_decode_frame status: %d (%s), consumed: %u, written: %u\n", st, faad_strerror(st), consumed, written);
    assert(st == FAAD_OK);

    info.struct_size = sizeof(info);
    st = faad_decoder_get_info(dec, &info);
    assert(st == FAAD_OK);
    printf("ADIF info parsed: format_known: %d, rate: %u, channels: %u\n", info.format_known, info.sample_rate, info.channels);
    assert(info.format_known);
    assert(info.sample_rate == 44100);
    assert(info.channels == 2);

    faad_decoder_close(&dec);

    printf("DAB+ / ADIF unit tests passed successfully!\n");
    return 0;
}
