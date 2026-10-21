/* PS feature availability and stereo input are checked before allocation. */
#include <assert.h>
#include "config.h"
#include "faac.h"

int main(void)
{
    faac_params p;
    faac_encoder *enc = NULL;
    assert(faac_params_init(&p, sizeof(p)) == FAAC_OK);
    p.sample_rate = 48000;
    p.num_channels = 1;
    p.input_format = FAAC_INPUT_16BIT;
    p.object_type = FAAC_OBJ_HE_AAC_V2;
    p.bit_rate = 8000;
#if FAAC_ENCODER_PS
    assert(faac_encoder_open(&p, &enc) == FAAC_ERR_INVALID_ARGUMENT);
#else
    assert(faac_encoder_open(&p, &enc) == FAAC_ERR_UNSUPPORTED);
#endif
    assert(enc == NULL);
#if MAX_CHANNELS >= 2
    p.num_channels = 2;
#if FAAC_ENCODER_PS
    const unsigned rates[] = {32000, 44100, 48000};
    for (unsigned i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
        p.sample_rate = rates[i];
        assert(faac_encoder_open(&p, &enc) == FAAC_OK);
        faac_encoder_info info = {0};
        info.struct_size = sizeof(info);
        assert(faac_encoder_get_info(enc, &info) == FAAC_OK);
        assert(info.object_type == FAAC_OBJ_HE_AAC_V2);
        assert(info.frame_samples == 2048 && info.sample_rate == rates[i]);
        assert(info.bit_rate == p.bit_rate);
        assert(faac_encoder_close(&enc) == FAAC_OK && enc == NULL);
    }
#else
    assert(faac_encoder_open(&p, &enc) == FAAC_ERR_UNSUPPORTED);
    assert(enc == NULL);
#endif
#endif
    p.object_type = FAAC_OBJ_AUTO;
    p.num_channels = 1;
    p.sample_rate = 48000;
    p.bit_rate = 4000;
    assert(faac_encoder_open(&p, &enc) == FAAC_OK);
    faac_encoder_info info = {0};
    info.struct_size = sizeof(info);
    assert(faac_encoder_get_info(enc, &info) == FAAC_OK);
    assert(info.object_type == FAAC_OBJ_LOW);
    assert(faac_encoder_close(&enc) == FAAC_OK);
#if MAX_CHANNELS >= 2
    p.num_channels = 2;
    const unsigned auto_rates[] = {32000, 44100, 48000};
    for (unsigned i = 0; i < 3; i++) {
        p.sample_rate = auto_rates[i];
        unsigned ceiling = p.sample_rate == 48000 ? 8000 : 6000;
        const unsigned bits[] = {3999, 4000, ceiling, ceiling + 1};
        for (unsigned j = 0; j < 4; j++) {
            p.bit_rate = bits[j];
            assert(faac_encoder_open(&p, &enc) == FAAC_OK);
            assert(faac_encoder_get_info(enc, &info) == FAAC_OK);
#if FAAC_ENCODER_PS
            enum faac_object_type expected = j == 0 ? FAAC_OBJ_LOW :
                j == 3 ? FAAC_OBJ_HE_AAC_V1 : FAAC_OBJ_HE_AAC_V2;
#else
            enum faac_object_type expected = bits[j] < 8000 ? FAAC_OBJ_LOW : FAAC_OBJ_HE_AAC_V1;
#endif
            assert(info.object_type == expected);
            assert(faac_encoder_close(&enc) == FAAC_OK && enc == NULL);
        }
    }
    p.sample_rate = 44100;
    p.bit_rate = 6000;
    p.mpeg_version = FAAC_MPEG2;
    assert(faac_encoder_open(&p, &enc) == FAAC_OK);
    assert(faac_encoder_get_info(enc, &info) == FAAC_OK);
    assert(info.object_type == FAAC_OBJ_LOW);
    assert(faac_encoder_close(&enc) == FAAC_OK);
    p.mpeg_version = FAAC_MPEG4;
    p.bit_rate = 0;
    p.quant_quality = 20;
    assert(faac_encoder_open(&p, &enc) == FAAC_OK);
    assert(faac_encoder_get_info(enc, &info) == FAAC_OK);
    assert(info.object_type == FAAC_OBJ_HE_AAC_V1);
    assert(faac_encoder_close(&enc) == FAAC_OK);
#endif
    return 0;
}
