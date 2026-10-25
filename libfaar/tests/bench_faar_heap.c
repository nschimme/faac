/* Measure requested libfaar heap at open and after stream preparation. */
#include <faar_alloc_hooks.h>
#include <faar.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int parse_ulong(const char *text, unsigned long *value) {
    char *end = NULL;
    errno = 0;
    unsigned long parsed = strtoul(text, &end, 10);
    if (errno || end == text || !end || *end) return 0;
    *value = parsed;
    return 1;
}

int main(int argc, char **argv) {
    unsigned long in_rate, out_rate, channels, quality, prepare_frames;
    if (argc != 6 || !parse_ulong(argv[1], &in_rate)
            || !parse_ulong(argv[2], &out_rate)
            || !parse_ulong(argv[3], &channels)
            || !parse_ulong(argv[4], &quality)
            || !parse_ulong(argv[5], &prepare_frames)
            || !in_rate || in_rate > UINT32_MAX || !out_rate
            || out_rate > UINT32_MAX || prepare_frames > SIZE_MAX
            || channels < 1 || channels > 8
            || quality > FAAR_QUALITY_HIGH) {
        fprintf(stderr, "usage: %s in_rate out_rate channels quality prepare_frames\n",
                argv[0]);
        return 1;
    }

    faar_config config;
    if (faar_config_init(&config, sizeof(config)) != FAAR_OK) return 1;
    config.in_rate = (uint32_t)in_rate;
    config.out_rate = (uint32_t)out_rate;
    config.channels = (uint16_t)channels;
    config.quality = (faar_quality_t)quality;

    faar_resampler *resampler = NULL;
    faar_alloc_reset_counters();
    faar_status status = faar_resampler_open(&config, &resampler);
    if (status != FAAR_OK) {
        fprintf(stderr, "open: %s\n", faar_strerror(status));
        return 1;
    }
    size_t open_live = faar_alloc_live_bytes();
    size_t open_peak = faar_alloc_peak_bytes();

    size_t prepared_live = open_live;
    size_t prepared_peak = open_peak;
    if (prepare_frames) {
        status = faar_resampler_prepare(resampler, prepare_frames);
        if (status != FAAR_OK) {
            fprintf(stderr, "prepare: %s\n", faar_strerror(status));
            faar_resampler_close(&resampler);
            return 1;
        }
        prepared_live = faar_alloc_live_bytes();
        prepared_peak = faar_alloc_peak_bytes();
    }

    status = faar_resampler_close(&resampler);
    size_t after_close = faar_alloc_live_bytes();
    printf("%lu,%lu,%lu,%lu,%lu,%zu,%zu,%zu,%zu,%zu\n",
            in_rate, out_rate, channels, quality, prepare_frames,
            open_live, open_peak, prepared_live, prepared_peak, after_close);
    return status == FAAR_OK && after_close == 0 ? 0 : 1;
}
