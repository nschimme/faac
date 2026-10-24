/* Reproducible public-API throughput/setup benchmark; not a correctness test. */
#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <faar.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/resource.h>

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static void check(faar_status s) {
    if (s != FAAR_OK) { fprintf(stderr, "%s\n", faar_strerror(s)); exit(1); }
}
int main(int argc, char **argv) {
    if (argc != 7) {
        fprintf(stderr, "usage: %s in_rate out_rate channels quality input_block output_capacity\n", argv[0]);
        return 1;
    }
    faar_config c;
    check(faar_config_init(&c, sizeof(c)));
    c.in_rate = (uint32_t)strtoul(argv[1], NULL, 10);
    c.out_rate = (uint32_t)strtoul(argv[2], NULL, 10);
    c.channels = (uint16_t)strtoul(argv[3], NULL, 10);
    c.quality = (faar_quality_t)atoi(argv[4]);
    size_t block = strtoul(argv[5], NULL, 10), cap = strtoul(argv[6], NULL, 10);
    if (!block || !cap || !c.channels || !c.in_rate) return 1;
    float *in = calloc(block * c.channels, sizeof(*in));
    float *out = calloc(cap * c.channels, sizeof(*out));
    if (!in || !out) return 1;
    for (size_t i = 0; i < block * c.channels; ++i) in[i] = (float)(16000 * sin(i * .137));
    double setup = 0, elapsed = 0;
    size_t total = 0, delay = 0, first_output_input_frames = 0;
    /* Several complete streams include startup and flush, with enough audio
     * to amortize setup. Run each configuration in its own process for RSS. */
    for (int repeat = 0; repeat < 3; ++repeat) {
        faar_resampler *r = NULL;
        double t = now();
        check(faar_resampler_open(&c, &r));
        setup += now() - t;
        faar_resampler_info info = {.struct_size = sizeof(info)};
        check(faar_resampler_get_info(r, &info));
        delay = info.delay;
        t = now();
        size_t frames = (size_t)c.in_rate * 4, produced = 0;
        for (size_t pos = 0; pos < frames;) {
            size_t n = frames - pos < block ? frames - pos : block, made;
            check(faar_resampler_process(r, in, n, out, cap, &made));
            if (made && !first_output_input_frames) first_output_input_frames = pos + n;
            produced += made;
            pos += n;
            do {
                check(faar_resampler_process(r, NULL, 0, out, cap, &made));
                produced += made;
            } while (made);
        }
        size_t made;
        do { check(faar_resampler_flush(r, out, cap, &made)); produced += made; } while (made);
        elapsed += now() - t;
        if (produced != ((uint64_t)frames * c.out_rate + c.in_rate - 1) / c.in_rate) return 1;
        total += frames;
        check(faar_resampler_close(&r));
    }
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
#ifdef __APPLE__
    double rss_kib = usage.ru_maxrss / 1024.0;
#else
    double rss_kib = usage.ru_maxrss;
#endif
    printf("%u,%u,%u,%d,%zu,%zu,%.3f,%.3f,%.0f,%zu,%zu\n", c.in_rate, c.out_rate,
        c.channels, (int)c.quality, block, cap, setup * 1000 / 3,
        total / elapsed / 1e6, rss_kib, delay, first_output_input_frames);
    free(in); free(out);
    return 0;
}
