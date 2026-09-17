/*
 * FAAC - Freeware Advanced Audio Coder
 * Data table content verification unit test.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>

#include "coder.h"
#include "huffdata.h"
#include "sbr_tables.h"

/* Reference ISO/IEC 13818-7 / 14496-3 / FFmpeg scalefactor band widths for 1024 long blocks */
static const uint8_t ref_long_widths[12][51] = {
    /* 96000 Hz (41 bands) */
    { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 8, 12, 12, 12, 12, 12, 16, 16, 24, 28, 36, 44, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64 },
    /* 88200 Hz (41 bands) */
    { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 8, 12, 12, 12, 12, 12, 16, 16, 24, 28, 36, 44, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64 },
    /* 64000 Hz (47 bands) */
    { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 12, 12, 12, 16, 16, 16, 20, 24, 24, 28, 36, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40 },
    /* 48000 Hz (49 bands) */
    { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 8, 8, 8, 12, 12, 12, 12, 16, 16, 20, 20, 24, 24, 28, 28, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 96 },
    /* 44100 Hz (49 bands) */
    { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 8, 8, 8, 12, 12, 12, 12, 16, 16, 20, 20, 24, 24, 28, 28, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 96 },
    /* 32000 Hz (51 bands) */
    { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 8, 8, 8, 12, 12, 12, 12, 16, 16, 20, 20, 24, 24, 28, 28, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32 },
    /* 24000 Hz (47 bands) */
    { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 12, 12, 12, 12, 16, 16, 16, 20, 20, 24, 24, 28, 28, 32, 36, 36, 40, 44, 48, 52, 52, 64, 64, 64, 64, 64 },
    /* 22050 Hz (47 bands) */
    { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 12, 12, 12, 12, 16, 16, 16, 20, 20, 24, 24, 28, 28, 32, 36, 36, 40, 44, 48, 52, 52, 64, 64, 64, 64, 64 },
    /* 16000 Hz (43 bands) */
    { 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 12, 12, 12, 12, 12, 12, 12, 12, 12, 16, 16, 16, 16, 20, 20, 20, 24, 24, 28, 28, 32, 36, 40, 40, 44, 48, 52, 56, 60, 64, 64, 64 },
    /* 12000 Hz (43 bands) */
    { 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 12, 12, 12, 12, 12, 12, 12, 12, 12, 16, 16, 16, 16, 20, 20, 20, 24, 24, 28, 28, 32, 36, 40, 40, 44, 48, 52, 56, 60, 64, 64, 64 },
    /* 11025 Hz (43 bands) */
    { 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 12, 12, 12, 12, 12, 12, 12, 12, 12, 16, 16, 16, 16, 20, 20, 20, 24, 24, 28, 28, 32, 36, 40, 40, 44, 48, 52, 56, 60, 64, 64, 64 },
    /*  8000 Hz (40 bands) */
    { 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 16, 16, 16, 16, 16, 16, 16, 20, 20, 20, 20, 24, 24, 24, 28, 28, 32, 36, 36, 40, 44, 48, 52, 56, 60, 64, 80 }
};

/* Reference ISO/IEC 13818-7 / 14496-3 / FFmpeg scalefactor band widths for 128 short blocks */
static const uint8_t ref_short_widths[12][15] = {
    /* 96000 Hz (12 bands) */
    { 4, 4, 4, 4, 4, 4, 8, 8, 8, 16, 28, 36 },
    /* 88200 Hz (12 bands) */
    { 4, 4, 4, 4, 4, 4, 8, 8, 8, 16, 28, 36 },
    /* 64000 Hz (12 bands) */
    { 4, 4, 4, 4, 4, 4, 8, 8, 8, 16, 28, 36 },
    /* 48000 Hz (14 bands) */
    { 4, 4, 4, 4, 4, 8, 8, 8, 12, 12, 12, 16, 16, 16 },
    /* 44100 Hz (14 bands) */
    { 4, 4, 4, 4, 4, 8, 8, 8, 12, 12, 12, 16, 16, 16 },
    /* 32000 Hz (14 bands) */
    { 4, 4, 4, 4, 4, 8, 8, 8, 12, 12, 12, 16, 16, 16 },
    /* 24000 Hz (15 bands) */
    { 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 12, 12, 16, 16, 20 },
    /* 22050 Hz (15 bands) */
    { 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 12, 12, 16, 16, 20 },
    /* 16000 Hz (15 bands) */
    { 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 12, 12, 16, 20, 20 },
    /* 12000 Hz (15 bands) */
    { 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 12, 12, 16, 20, 20 },
    /* 11025 Hz (15 bands) */
    { 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 12, 12, 16, 20, 20 },
    /*  8000 Hz (15 bands) */
    { 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 12, 16, 20, 20 }
};

static const unsigned long expected_rates[12] = {
    96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000
};

static void test_sr_info(void)
{
    printf("Testing srInfo tables...\n");
    for (int i = 0; i < 12; i++) {
        SR_INFO *sr = &srInfo[i];
        assert(sr->sampling_rate == expected_rates[i]);

        /* Verify long block bands */
        int sum_long = 0;
        for (int b = 0; b < sr->num_cb_long; b++) {
            assert(sr->cb_width_long[b] == ref_long_widths[i][b]);
            sum_long += sr->cb_width_long[b];
        }
        assert(sum_long == BLOCK_LEN_LONG);

        /* Verify short block bands */
        int sum_short = 0;
        for (int b = 0; b < sr->num_cb_short; b++) {
            assert(sr->cb_width_short[b] == ref_short_widths[i][b]);
            sum_short += sr->cb_width_short[b];
        }
        assert(sum_short == BLOCK_LEN_SHORT);
    }
    printf("srInfo tables validation PASSED.\n");
}

static void test_huffman_books(void)
{
    printf("Testing Huffman codebook contents...\n");

    /* Check book01: 81 entries */
    assert(book01[0].len == 11 && book01[0].data == 2040);
    assert(book01[80].len == 11 && book01[80].data == 2036);

    /* Check book02: 81 entries */
    assert(book02[0].len == 9 && book02[0].data == 499);
    assert(book02[80].len == 9 && book02[80].data == 502);

    /* Check book03: 81 entries */
    assert(book03[0].len == 1 && book03[0].data == 0);
    assert(book03[80].len == 15 && book03[80].data == 32762);

    /* Check book04: 81 entries */
    assert(book04[0].len == 4 && book04[0].data == 7);
    assert(book04[80].len == 11 && book04[80].data == 2044);

    /* Check book05: 81 entries */
    assert(book05[0].len == 13 && book05[0].data == 8191);
    assert(book05[80].len == 13 && book05[80].data == 8190);

    /* Check book06: 81 entries */
    assert(book06[0].len == 11 && book06[0].data == 2046);
    assert(book06[80].len == 11 && book06[80].data == 2044);

    /* Check book07: 64 entries */
    assert(book07[0].len == 1 && book07[0].data == 0);
    assert(book07[63].len == 12 && book07[63].data == 4095);

    /* Check book08: 64 entries */
    assert(book08[0].len == 5 && book08[0].data == 14);
    assert(book08[63].len == 10 && book08[63].data == 1023);

    /* Check book09: 169 entries */
    assert(book09[0].len == 1 && book09[0].data == 0);
    assert(book09[168].len == 15 && book09[168].data == 32767);

    /* Check book10: 169 entries */
    assert(book10[0].len == 6 && book10[0].data == 34);
    assert(book10[168].len == 12 && book10[168].data == 4095);

    /* Check book11: 289 entries */
    assert(book11[0].len == 4 && book11[0].data == 0);
    assert(book11[288].len == 5 && book11[288].data == 4);

    /* Check book12: 121 entries */
    assert(book12[0].len == 18 && book12[0].data == 262120);
    assert(book12[120].len == 19 && book12[120].data == 524275);

    printf("Huffman codebook contents validation PASSED.\n");
}

static void test_sbr_tables(void)
{
    printf("Testing SBR tables...\n");

    /* Check qmf_c 640 filter coefficients */
    assert(qmf_c[0] == 0.0f);
    assert(fabsf(qmf_c[319] - 0.85357205739f) < 1e-6f);
    assert(fabsf(qmf_c[639] - (-5.5252865047e-04f)) < 1e-6f);

    /* Check sbr_offset table */
    assert(sbr_offset[0][0] == -8 && sbr_offset[0][15] == 7);
    assert(sbr_offset[4][0] == -4 && sbr_offset[4][15] == 20); /* 44-64k */
    assert(sbr_offset[5][0] == -2 && sbr_offset[5][15] == 24); /* >64k */

    /* Check SBR envelope Huffman table bounds */
    assert(f_huff_env_1_5dB[0].len == 19 && f_huff_env_1_5dB[0].code == 0x0007ffe7u);
    assert(f_huff_env_1_5dB[120].len == 20 && f_huff_env_1_5dB[120].code == 0x000fffffu);

    assert(f_huff_env_3_0dB[0].len == 20 && f_huff_env_3_0dB[0].code == 0x000ffff0u);
    assert(f_huff_env_3_0dB[62].len == 20 && f_huff_env_3_0dB[62].code == 0x000fffffu);

    printf("SBR tables validation PASSED.\n");
}

int main(void)
{
    printf("=== Starting Data Table Content Unit Tests ===\n");
    test_sr_info();
    test_huffman_books();
    test_sbr_tables();
    printf("=== All Data Table Content Unit Tests PASSED Successfully ===\n");
    return 0;
}
