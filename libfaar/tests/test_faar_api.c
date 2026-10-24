#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <faar.h>

int main(void) {
    faar_config config;
    assert(faar_config_init(&config, sizeof(config)) == FAAR_OK);
    assert(config.quality == FAAR_QUALITY_GOOD);
    assert(config.boundary == FAAR_BOUNDARY_HOLD);

    faar_library_info library = {.struct_size = sizeof(library)};
    assert(faar_get_library_info(&library) == FAAR_OK);
    assert(library.max_channels >= 1 && library.max_channels <= 8);
    assert(library.quality_mask & FAAR_QUALITY_MASK_GOOD);
    assert(strcmp(library.version, FAAR_VERSION_STRING) == 0);
    assert(library.quality_mask & FAAR_QUALITY_MASK_HIGH);

    config.in_rate = 48000;
    config.out_rate = 44100;
    config.channels = 1;
    faar_resampler *resampler = NULL;
    assert(faar_resampler_open(&config, &resampler) == FAAR_OK);
    assert(resampler != NULL);

    faar_resampler_info info = {.struct_size = sizeof(info)};
    assert(faar_resampler_get_info(resampler, &info) == FAAR_OK);
    assert(info.in_rate == config.in_rate && info.out_rate == config.out_rate);
    assert(info.channels == 1 && info.quality == FAAR_QUALITY_GOOD);

    float input[480] = {0};
    float output[512] = {0};
    size_t produced = 0;
    assert(faar_resampler_process(resampler, input, 480, output,
                                  512, &produced) == FAAR_OK);
    size_t total = produced;
    size_t flushed = 0;
    do {
        assert(faar_resampler_flush(resampler, output + total,
                                    512 - total, &flushed) == FAAR_OK);
        total += flushed;
    } while (flushed != 0);
    assert(total == 441);

    assert(faar_resampler_process(resampler, input, 1, output, 1,
                                  &produced) == FAAR_ERR_INVALID_ARGUMENT);
    assert(produced == 0);
    assert(faar_resampler_close(&resampler) == FAAR_OK);
    assert(resampler == NULL);
    assert(faar_resampler_close(&resampler) == FAAR_OK);

    config.channels = (uint16_t)(library.max_channels + 1);
    assert(faar_resampler_open(&config, &resampler)
           == FAAR_ERR_INVALID_ARGUMENT);
    assert(resampler == NULL);

    config.channels = 1;
    config.reserved1[1] = 1;
    assert(faar_resampler_open(&config, &resampler)
           == FAAR_ERR_INVALID_ARGUMENT);
    config.reserved1[1] = 0;
    config.dither = FAAR_DITHER_TPDF;
    assert(faar_resampler_open(&config, &resampler)
           == FAAR_ERR_INVALID_ARGUMENT);
    assert(resampler == NULL);
    config.dither = FAAR_DITHER_OFF;
    config.channels = 1;
    config.quality = FAAR_QUALITY_HIGH;
    assert(faar_resampler_open(&config, &resampler) == FAAR_OK);
    assert(resampler != NULL);
    assert(faar_resampler_close(&resampler) == FAAR_OK);
    assert(resampler == NULL);
    assert(strcmp(faar_strerror(FAAR_ERR_NO_MEMORY), "out of memory") == 0);
    return 0;
}
