/* Public-API callback latency probe; timings are observations, not deadlines. */
#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <faar.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/resource.h>

#define MAX_TIMING_SAMPLES 2000000u
#define MAX_OUTPUT_CAPACITY 4096u
#define MAX_INPUT_BLOCK 16384u

static double now(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) return -1.0;
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

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

static int compare_double(const void *left, const void *right)
{
    double a = *(const double *)left;
    double b = *(const double *)right;
    return (a > b) - (a < b);
}

static int add_output(size_t made, size_t expected, size_t *total)
{
    if (made > expected - *total) {
        fprintf(stderr, "resampler produced excess output\n");
        return 0;
    }
    *total += made;
    return 1;
}

static int timed_process(faar_resampler *resampler,
                         const float *input, size_t input_frames,
                         float *output, size_t output_capacity,
                         double *durations, size_t duration_capacity,
                         size_t *duration_count, size_t *process_call_count,
                         size_t expected_output,
                         size_t *total_output, size_t *made_out)
{
    double start, elapsed;
    size_t made = 0;
    faar_status status;

    if (*duration_count >= duration_capacity) {
        fprintf(stderr, "timing sample limit exceeded\n");
        return 0;
    }
    start = now();
    if (start < 0.0) {
        perror("clock_gettime");
        return 0;
    }
    status = faar_resampler_process(resampler, input, input_frames,
                                    output, output_capacity, &made);
    elapsed = now() - start;
    if (elapsed < 0.0) {
        perror("clock_gettime");
        return 0;
    }
    durations[(*duration_count)++] = elapsed * 1e6;
    ++*process_call_count;
    if (status != FAAR_OK) {
        fprintf(stderr, "process: %s\n", faar_strerror(status));
        return 0;
    }
    if (!add_output(made, expected_output, total_output)) return 0;
    *made_out = made;
    return 1;
}

static int timed_flush(faar_resampler *resampler,
                       float *output, size_t output_capacity,
                       double *durations, size_t duration_capacity,
                       size_t *duration_count,
                       size_t expected_output, size_t *total_output,
                       size_t *flush_calls, double *flush_max_us,
                       size_t call_limit, size_t *made_out)
{
    double start, elapsed;
    size_t made = 0;
    faar_status status;

    if (*flush_calls >= call_limit || *duration_count >= duration_capacity) {
        fprintf(stderr, "flush call limit exceeded\n");
        return 0;
    }
    ++*flush_calls;
    start = now();
    if (start < 0.0) {
        perror("clock_gettime");
        return 0;
    }
    status = faar_resampler_flush(resampler, output, output_capacity, &made);
    elapsed = now() - start;
    if (elapsed < 0.0) {
        perror("clock_gettime");
        return 0;
    }
    if (elapsed * 1e6 > *flush_max_us) *flush_max_us = elapsed * 1e6;
    durations[(*duration_count)++] = elapsed * 1e6;
    if (status != FAAR_OK) {
        fprintf(stderr, "flush: %s\n", faar_strerror(status));
        return 0;
    }
    if (!add_output(made, expected_output, total_output)) return 0;
    *made_out = made;
    return 1;
}

static double percentile(const double *sorted, size_t count, size_t pct)
{
    size_t rank = (count * pct + 99) / 100;
    if (rank == 0) rank = 1;
    if (rank > count) rank = count;
    return sorted[rank - 1];
}

int main(int argc, char **argv)
{
    unsigned long in_rate, out_rate, channels, quality, block_arg, cap_arg;
    unsigned long prepared_arg;
    faar_config config;
    faar_resampler *resampler = NULL;
    float *input = NULL, *output = NULL;
    double *durations = NULL;
    size_t duration_capacity, duration_count = 0, process_call_count = 0;
    size_t block, output_capacity, input_frames, expected_output;
    size_t total_output = 0, flush_calls = 0;
    double prepare_ms = 0.0, flush_max_us = 0.0;
    struct rusage usage;
    double rss_kib;
    int failed = 0;

    if (argc != 8 ||
        !parse_ulong(argv[1], &in_rate) ||
        !parse_ulong(argv[2], &out_rate) ||
        !parse_ulong(argv[3], &channels) ||
        !parse_ulong(argv[4], &quality) ||
        !parse_ulong(argv[5], &block_arg) ||
        !parse_ulong(argv[6], &cap_arg) ||
        !parse_ulong(argv[7], &prepared_arg) ||
        in_rate == 0 || in_rate > UINT32_MAX ||
        out_rate == 0 || out_rate > UINT32_MAX ||
        channels < 1 || channels > 8 || quality > 2 ||
        block_arg < 1 || block_arg > MAX_INPUT_BLOCK ||
        cap_arg < 1 || cap_arg > MAX_OUTPUT_CAPACITY || prepared_arg > 1) {
        fprintf(stderr, "usage: %s in_rate out_rate channels quality input_block output_capacity prepared(0|1)\n",
                argv[0]);
        return 1;
    }
#ifdef FAAR_BENCH_NO_PREPARE
    if (prepared_arg != 0) {
        fprintf(stderr, "this build does not support prepared mode\n");
        return 1;
    }
#endif
    if (in_rate > SIZE_MAX / 4u || out_rate > SIZE_MAX / 4u) {
        fprintf(stderr, "rate is too large for this platform\n");
        return 1;
    }

    block = (size_t)block_arg;
    output_capacity = (size_t)cap_arg;
    input_frames = (size_t)in_rate * 4u;
    expected_output = (size_t)out_rate * 4u;
    {
        uint64_t input_calls = ((uint64_t)input_frames + block - 1) / block;
        /* A producing call may return fewer than output_capacity frames, so
         * expected_output is the conservative bound on producing calls. */
        uint64_t bound = 2 * input_calls + (uint64_t)expected_output + 100000;
        if (bound > MAX_TIMING_SAMPLES || bound > SIZE_MAX / sizeof(*durations)) {
            fprintf(stderr, "configuration needs too many timing samples\n");
            return 1;
        }
        duration_capacity = (size_t)bound;
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

    input = calloc(block * (size_t)channels, sizeof(*input));
    output = calloc(output_capacity * (size_t)channels, sizeof(*output));
    durations = malloc(duration_capacity * sizeof(*durations));
    if (!input || !output || !durations) {
        fprintf(stderr, "could not allocate benchmark buffers\n");
        failed = 1;
        goto cleanup;
    }
    for (size_t frame = 0; frame < block; ++frame) {
        float sample = (float)(16000.0 * sin(6.283185307179586 * 997.0 *
                                             (double)frame / (double)in_rate));
        for (size_t channel = 0; channel < (size_t)channels; ++channel)
            input[frame * (size_t)channels + channel] = sample;
    }

    {
        faar_status status = faar_resampler_open(&config, &resampler);
        if (status != FAAR_OK) {
            fprintf(stderr, "open: %s\n", faar_strerror(status));
            failed = 1;
            goto cleanup;
        }
    }
#ifndef FAAR_BENCH_NO_PREPARE
    if (prepared_arg) {
        double start = now();
        faar_status status;
        if (start < 0.0) {
            perror("clock_gettime");
            failed = 1;
            goto cleanup;
        }
        status = faar_resampler_prepare(resampler, block);
        prepare_ms = (now() - start) * 1000.0;
        if (prepare_ms < 0.0) {
            perror("clock_gettime");
            failed = 1;
            goto cleanup;
        }
        if (status != FAAR_OK) {
            fprintf(stderr, "prepare: %s\n", faar_strerror(status));
            failed = 1;
            goto cleanup;
        }
    }
#endif

    for (size_t pos = 0; pos < input_frames;) {
        size_t count = input_frames - pos < block ? input_frames - pos : block;
        size_t made = 0;
        if (!timed_process(resampler, input, count, output, output_capacity,
                           durations, duration_capacity, &duration_count,
                           &process_call_count,
                           expected_output, &total_output, &made)) {
            failed = 1;
            goto cleanup;
        }
        pos += count;
        do {
            if (!timed_process(resampler, NULL, 0, output, output_capacity,
                               durations, duration_capacity, &duration_count,
                               &process_call_count,
                               expected_output, &total_output, &made)) {
                failed = 1;
                goto cleanup;
            }
        } while (made != 0);
    }

    {
        size_t made = 0;
        do {
            if (!timed_flush(resampler, output, output_capacity, durations,
                             duration_capacity, &duration_count,
                             expected_output, &total_output, &flush_calls,
                             &flush_max_us, duration_capacity, &made)) {
                failed = 1;
                goto cleanup;
            }
        } while (made != 0);
    }
    if (total_output != expected_output) {
        fprintf(stderr, "wrong output frame count: got %zu, expected %zu\n",
                total_output, expected_output);
        failed = 1;
        goto cleanup;
    }
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        perror("getrusage");
        failed = 1;
        goto cleanup;
    }
#ifdef __APPLE__
    rss_kib = usage.ru_maxrss / 1024.0;
#else
    rss_kib = usage.ru_maxrss;
#endif
    qsort(durations, process_call_count, sizeof(*durations), compare_double);
    printf("%lu,%lu,%lu,%lu,%zu,%zu,%lu,%.3f,%zu,%.3f,%.3f,%.3f,%.3f,%.0f\n",
           in_rate, out_rate, channels, quality, block, output_capacity,
           prepared_arg, prepare_ms, process_call_count,
           percentile(durations, process_call_count, 50),
           percentile(durations, process_call_count, 99),
           durations[process_call_count - 1], flush_max_us, rss_kib);

cleanup:
    if (resampler) {
        faar_status status = faar_resampler_close(&resampler);
        if (status != FAAR_OK) {
            fprintf(stderr, "close: %s\n", faar_strerror(status));
            failed = 1;
        }
    }
    free(durations);
    free(output);
    free(input);
    return failed ? 1 : 0;
}
