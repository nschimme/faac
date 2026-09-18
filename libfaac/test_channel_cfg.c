#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <faac.h>

int main(void)
{
    faac_params params;
    faac_encoder *enc = NULL;
    const uint8_t *asc = NULL;
    uint32_t asc_len = 0;
    faac_status st;

    printf("Running AAC channel configuration tests...\n");

    /* 1. Test 1 channel (Mono, 44.1 kHz, LC) */
    assert(faac_params_init(&params, sizeof(params)) == FAAC_OK);
    params.sample_rate = 44100;
    params.num_channels = 1;
    st = faac_encoder_open(&params, &enc);
    assert(st == FAAC_OK && enc != NULL);
    st = faac_encoder_asc(enc, &asc, &asc_len);
    assert(st == FAAC_OK && asc_len == 2);
    /* audioObjectType=2 (5b), srIdx=4 (4b), chCfg=1 (4b) -> 00010 0100 0001 000 = 0x12, 0x08 */
    assert(asc[0] == 0x12 && asc[1] == 0x08);
    faac_encoder_close(&enc);

    /* 2. Test 2 channels (Stereo, 44.1 kHz, LC) */
    assert(faac_params_init(&params, sizeof(params)) == FAAC_OK);
    params.sample_rate = 44100;
    params.num_channels = 2;
    st = faac_encoder_open(&params, &enc);
    assert(st == FAAC_OK && enc != NULL);
    st = faac_encoder_asc(enc, &asc, &asc_len);
    assert(st == FAAC_OK && asc_len == 2);
    /* audioObjectType=2 (5b), srIdx=4 (4b), chCfg=2 (4b) -> 00010 0100 0010 000 = 0x12, 0x10 */
    assert(asc[0] == 0x12 && asc[1] == 0x10);
    faac_encoder_close(&enc);

    /* 3. Test 6 channels (5.1 surround, 44.1 kHz, LC) */
    assert(faac_params_init(&params, sizeof(params)) == FAAC_OK);
    params.sample_rate = 44100;
    params.num_channels = 6;
    st = faac_encoder_open(&params, &enc);
    assert(st == FAAC_OK && enc != NULL);
    st = faac_encoder_asc(enc, &asc, &asc_len);
    assert(st == FAAC_OK && asc_len == 2);
    /* audioObjectType=2 (5b), srIdx=4 (4b), chCfg=6 (4b) -> 00010 0100 0110 000 = 0x12, 0x30 */
    assert(asc[0] == 0x12 && asc[1] == 0x30);
    faac_encoder_close(&enc);

    /* 4. Test 8 channels (7.1 surround, 44.1 kHz, LC) */
    assert(faac_params_init(&params, sizeof(params)) == FAAC_OK);
    params.sample_rate = 44100;
    params.num_channels = 8;
    st = faac_encoder_open(&params, &enc);
    assert(st == FAAC_OK && enc != NULL);
    st = faac_encoder_asc(enc, &asc, &asc_len);
    assert(st == FAAC_OK && asc_len == 2);
    /* audioObjectType=2 (5b), srIdx=4 (4b), chCfg=7 (4b) -> 00010 0100 0111 000 = 0x12, 0x38 */
    assert(asc[0] == 0x12 && asc[1] == 0x38);

    /* Test ADTS header generation for 8 channels */
    {
        uint8_t dummy_in[8 * 2 * 1024] = {0};
        uint8_t out_buf[16384] = {0};
        uint32_t written = 0;
        st = faac_encoder_encode(enc, dummy_in, 8 * 1024, out_buf, sizeof(out_buf), &written);
        assert(st == FAAC_OK);
        if (written >= 7) {
            /* ADTS header: byte 2 bit 0 and byte 3 bits 6-7 hold 3-bit channel_configuration.
             * For chCfg=7 (111 in binary): byte 2 LSB = 1, byte 3 MSBs = 11 (0xC0). */
            assert((out_buf[2] & 0x01) == 0x01);
            assert((out_buf[3] & 0xC0) == 0xC0);
        }
    }
    faac_encoder_close(&enc);

    /* 5. Test 7 channels (Unsupported, must be rejected) */
    assert(faac_params_init(&params, sizeof(params)) == FAAC_OK);
    params.sample_rate = 44100;
    params.num_channels = 7;
    st = faac_encoder_open(&params, &enc);
    assert(st == FAAC_ERR_INVALID_ARGUMENT);
    assert(enc == NULL);

    /* 6. Test 8 channels HE-AAC v1 (SBR) */
    assert(faac_params_init(&params, sizeof(params)) == FAAC_OK);
    params.sample_rate = 44100;
    params.num_channels = 8;
    params.object_type = FAAC_OBJ_HE_AAC_V1;
    st = faac_encoder_open(&params, &enc);
    assert(st == FAAC_OK && enc != NULL);
    st = faac_encoder_asc(enc, &asc, &asc_len);
    assert(st == FAAC_OK && asc_len == 5);
    /* Core rate is Fs/2 = 22.05 kHz (srIdx=7). Core chCfg=7.
     * bits: LOW(2)=5b, coreSRIdx(7)=4b, chCfg(7)=4b */
    assert((asc[0] >> 3) == 0x02);
    assert(((asc[1] >> 3) & 0x0F) == 0x07);
    faac_encoder_close(&enc);

    printf("ALL AAC channel configuration tests PASSED successfully!\n");
    return 0;
}
