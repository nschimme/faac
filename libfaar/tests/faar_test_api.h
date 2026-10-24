#ifndef FAAR_TEST_API_H
#define FAAR_TEST_API_H

#include <faar.h>
#include <stdlib.h>

/* Test adapter keeps stream-focused regression cases compact while routing
 * every operation through the public status-returning API. */
typedef struct faar_test_handle {
    faar_config config;
    faar_resampler *resampler;
    int started;
} faar_test_handle;

static inline faar_test_handle *faar_test_open(uint32_t in_rate,
        uint32_t out_rate, uint16_t channels, faar_quality_t quality,
        uint32_t decimate_factor, faar_boundary_t boundary,
        faar_dither_t dither, uint8_t bits) {
    faar_test_handle *h = calloc(1, sizeof(*h));
    if (!h) return NULL;
    if (faar_config_init(&h->config, sizeof(h->config)) != FAAR_OK) { free(h); return NULL; }
    h->config.in_rate = in_rate;
    h->config.out_rate = out_rate;
    h->config.channels = channels;
    h->config.quality = quality;
    h->config.decimate_factor = decimate_factor;
    h->config.boundary = boundary;
    h->config.dither = dither;
    h->config.dither_bits = bits;
    if (faar_resampler_open(&h->config, &h->resampler) != FAAR_OK) { free(h); return NULL; }
    return h;
}
static inline faar_test_handle *faar_test_create(uint32_t in_rate,
        uint32_t out_rate, uint16_t channels, faar_quality_t quality) {
    return faar_test_open(in_rate, out_rate, channels, quality, 0,
            FAAR_BOUNDARY_HOLD, FAAR_DITHER_OFF, 0);
}
static inline faar_test_handle *faar_test_create_ext(uint32_t in_rate,
        uint32_t out_rate, uint16_t channels, faar_quality_t quality,
        uint32_t factor) {
    return faar_test_open(in_rate, out_rate, channels, quality, factor,
            FAAR_BOUNDARY_HOLD, FAAR_DITHER_OFF, 0);
}
static inline int faar_test_reopen(faar_test_handle *h) {
    faar_resampler_close(&h->resampler);
    return faar_resampler_open(&h->config, &h->resampler) == FAAR_OK ? 0 : -1;
}
static inline int faar_test_set_boundary(faar_test_handle *h, faar_boundary_t b) {
    if (!h || h->started || (b != FAAR_BOUNDARY_HOLD && b != FAAR_BOUNDARY_ZERO)) return -1;
    h->config.boundary = b;
    return faar_test_reopen(h);
}
static inline int faar_test_set_dither(faar_test_handle *h, faar_dither_t d, uint8_t bits) {
    if (!h || h->started || (d != FAAR_DITHER_OFF && d != FAAR_DITHER_TPDF && d != FAAR_DITHER_SHAPED)
            || (d != FAAR_DITHER_OFF && bits != 16 && bits != 24)) return -1;
    h->config.dither = d; h->config.dither_bits = bits;
    return faar_test_reopen(h);
}
static inline size_t faar_test_delay(faar_test_handle *h) {
    faar_resampler_info i = {.struct_size = sizeof(i)};
    return h && faar_resampler_get_info(h->resampler, &i) == FAAR_OK ? i.delay : 0;
}
static inline size_t faar_test_process(faar_test_handle *h, const float *in,
        size_t n, float *out, size_t cap) {
    size_t made = 0;
    if (!h) return 0;
    faar_status st = faar_resampler_process(h->resampler, in, n, out, cap, &made);
    if (st != FAAR_OK) return 0;
    if (in && n) h->started = 1;
    return made;
}
static inline size_t faar_test_flush(faar_test_handle *h, float *out, size_t cap) {
    size_t made = 0;
    if (!h) return 0;
    faar_status st = faar_resampler_flush(h->resampler, out, cap, &made);
    if (st != FAAR_OK) return 0;
    h->started = 1;
    return made;
}
static inline void faar_test_close(faar_test_handle *h) {
    if (h) { faar_resampler_close(&h->resampler); free(h); }
}
#endif
