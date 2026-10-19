#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <faar.h>

#ifdef FAAR_FLOAT
#define TEST_QUALITY FAAR_QUALITY_GOOD
#else
#define TEST_QUALITY FAAR_QUALITY_HIGH
#endif

int main(void) {
    printf("Testing libfaar API...\n");
#ifdef FAAR_FLOAT
    static const uint32_t rates[][2] = {
        {96000, 44100}, {44100, 48000}, {48000, 48000}
    };
    for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) {
        assert(faar_create(rates[i][0], rates[i][1], 2,
                           FAAR_QUALITY_HIGH) == NULL);
        assert(faar_create_ext(rates[i][0], rates[i][1], 2,
                               FAAR_QUALITY_HIGH, 2) == NULL);
    }
#endif

    uint32_t in_rate = 96000;
    uint32_t out_rate = 44100;
    uint16_t channels = 2;

    faar_handle_t r = faar_create(in_rate, out_rate, channels, FAAR_QUALITY_GOOD);
    assert(r != NULL);

    size_t delay = faar_get_delay(r);
    printf("Resampler delay: %zu samples\n", delay);

    size_t in_frames = 1024;
    float *in_buf = (float *)calloc(in_frames * channels, sizeof(float));
    /* Fill with 1 kHz sine wave */
    for (size_t i = 0; i < in_frames; i++) {
        float val = (float)sin(2.0 * 3.141592653589793 * 1000.0 * i / in_rate);
        in_buf[i * 2 + 0] = val;
        in_buf[i * 2 + 1] = val;
    }

    size_t out_cap = 2048;
    float *out_buf = (float *)calloc(out_cap * channels, sizeof(float));

    size_t produced = faar_process(r, in_buf, in_frames, out_buf, out_cap);
    printf("Processed %zu input frames -> %zu output frames\n", in_frames, produced);
    size_t total = produced;
    size_t flushed;
    do {
        flushed = faar_flush(r, out_buf + total * channels, out_cap - total);
        total += flushed;
    } while (flushed && total < out_cap);
    printf("Flushed %zu additional frames\n", total - produced);
    assert(total == (in_frames * (size_t)out_rate + in_rate - 1) / in_rate);

    faar_destroy(r);

    /* Test Dithering Modes (TPDF & SHAPED for 16-bit and 24-bit) */
    printf("Testing libfaar dithering modes...\n");
    faar_dither_t test_dithers[] = {FAAR_DITHER_OFF, FAAR_DITHER_TPDF, FAAR_DITHER_SHAPED};
    uint8_t test_bits[] = {16, 24};

    for (size_t d = 0; d < 3; d++) {
        for (size_t b = 0; b < 2; b++) {
            faar_handle_t r_dither = faar_create(in_rate, out_rate, channels, TEST_QUALITY);
            assert(r_dither != NULL);
            if (test_dithers[d] != FAAR_DITHER_OFF) {
                int status = faar_set_dither(r_dither, test_dithers[d], test_bits[b]);
                assert(status == 0);
            }

            size_t d_produced = faar_process(r_dither, in_buf, in_frames, out_buf, out_cap);
            size_t d_flushed = faar_flush(r_dither, out_buf + d_produced * channels, out_cap - d_produced);
            assert(d_produced + d_flushed > 0);

            faar_destroy(r_dither);
        }
    }

    free(in_buf);
    free(out_buf);

    printf("libfaar test passed successfully!\n");
    return 0;
}
