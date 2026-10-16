/*
 * FAAD - Freeware Advanced Audio Decoder
 * Unit test for DAB+ 960-sample frame size
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "faad3.h"

int main(void)
{
#ifndef FAAD_DISABLE_DAB
    faad_library_info lib_info;
    lib_info.struct_size = sizeof(lib_info);
    faad_status st = faad_get_library_info(&lib_info);
    assert(st == FAAD_OK);
    printf("Testing FAAD version %s (SBR: %d, PS: %d)\n", lib_info.version, lib_info.sbr_supported, lib_info.ps_supported);

    /* Test 1: ASC build with 960 frame length (frame_length_flag = 1) */
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

    printf("DAB+ unit tests passed successfully!\n");
#else
    printf("DAB+ support disabled via FAAD_DISABLE_DAB.\n");
#endif
    return 0;
}
