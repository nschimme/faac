/* Measure resampler open cost and process memory with handles kept alive. */
#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <faar.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/resource.h>

static int parse_ulong(const char *text, unsigned long *value)
{
    char *end = NULL;
    unsigned long parsed;

    if (!text || !*text || *text == '-') return 0;
    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0') return 0;
    *value = parsed;
    return 1;
}

static int close_handles(faar_resampler **handles, size_t count)
{
    int failed = 0;
    while (count > 0) {
        faar_status status;
        --count;
        status = faar_resampler_close(&handles[count]);
        if (status != FAAR_OK) {
            fprintf(stderr, "close handle %zu: %s\n", count,
                    faar_strerror(status));
            failed = 1;
        }
    }
    return failed;
}

static double now(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) return -1.0;
    return t.tv_sec + t.tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
    unsigned long in_rate, out_rate, channels, quality, handle_count;
    faar_config config;
    faar_resampler **handles;
    double first_open_ms = 0.0, subsequent_open_ms = 0.0;
    struct rusage usage;
    double rss_kib;
    size_t opened = 0;
    int failed = 0;

    if (argc != 6 ||
        !parse_ulong(argv[1], &in_rate) ||
        !parse_ulong(argv[2], &out_rate) ||
        !parse_ulong(argv[3], &channels) ||
        !parse_ulong(argv[4], &quality) ||
        !parse_ulong(argv[5], &handle_count) ||
        in_rate == 0 || in_rate > UINT32_MAX ||
        out_rate == 0 || out_rate > UINT32_MAX ||
        channels < 1 || channels > 8 || quality > 2 ||
        handle_count < 1 || handle_count > 128) {
        fprintf(stderr, "usage: %s in_rate out_rate channels quality handles\n",
                argv[0]);
        return 1;
    }

    {
        faar_status status = faar_config_init(&config, sizeof(config));
        if (status != FAAR_OK) {
            fprintf(stderr, "config init: %s\n", faar_strerror(status));
            return 1;
        }
    }
    config.in_rate = (uint32_t)in_rate;
    config.out_rate = (uint32_t)out_rate;
    config.channels = (uint16_t)channels;
    config.quality = (faar_quality_t)quality;

    handles = calloc((size_t)handle_count, sizeof(*handles));
    if (!handles) {
        fprintf(stderr, "could not allocate handle array\n");
        return 1;
    }

    for (size_t i = 0; i < (size_t)handle_count; ++i) {
        double start = now();
        double elapsed;
        faar_status status;
        if (start < 0.0) {
            perror("clock_gettime");
            failed = 1;
            break;
        }
        status = faar_resampler_open(&config, &handles[i]);
        elapsed = now() - start;
        if (elapsed < 0.0) {
            perror("clock_gettime");
            failed = 1;
            if (status == FAAR_OK) ++opened;
            break;
        }
        if (status != FAAR_OK) {
            fprintf(stderr, "open handle %zu: %s\n", i, faar_strerror(status));
            failed = 1;
            break;
        }
        ++opened;
        if (i == 0) first_open_ms = elapsed * 1000.0;
        else subsequent_open_ms += elapsed * 1000.0;
    }

    if (!failed && getrusage(RUSAGE_SELF, &usage) != 0) {
        perror("getrusage");
        failed = 1;
    }
    if (!failed) {
#ifdef __APPLE__
        rss_kib = usage.ru_maxrss / 1024.0;
#else
        rss_kib = usage.ru_maxrss;
#endif
        if (handle_count > 1)
            subsequent_open_ms /= (double)(handle_count - 1);
        printf("%lu,%lu,%lu,%lu,%lu,%.3f,%.3f,%.0f\n",
               in_rate, out_rate, channels, quality, handle_count,
               first_open_ms, subsequent_open_ms, rss_kib);
    }

    if (close_handles(handles, opened)) failed = 1;
    free(handles);
    return failed ? 1 : 0;
}
