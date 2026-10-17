#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <faar.h>

int main(void) {
    printf("Testing libfaar API...\n");

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
    assert(produced > 0);

    size_t flushed = faar_flush(r, out_buf + produced * channels, out_cap - produced);
    printf("Flushed %zu additional frames\n", flushed);

    faar_destroy(r);
    free(in_buf);
    free(out_buf);

    printf("libfaar test passed successfully!\n");
    return 0;
}
