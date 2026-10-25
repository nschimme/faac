/*
 * FAAR - Freeware Advanced Audio Resampler
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 */
#define _POSIX_C_SOURCE 200809L
#include <faar.h>
#include <faar_alloc_hooks.h>
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef posix_memalign

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

enum { DEFAULT_HANDLE_COUNT = 16, MAX_HANDLE_COUNT = 128, REPEATS = 7 };

typedef struct {
  uint32_t in_rate;
  uint32_t out_rate;
  uint16_t channels;
  faar_quality_t quality;
} bench_case;

typedef struct {
  double ordinary_ms;
  double plan_design_ms;
  double plan_streams_ms;
  size_t ordinary_bytes;
  size_t plan_design_bytes;
  size_t plan_all_bytes;
} bench_result;

static double now_seconds(void) {
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    return -1.0;
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int close_handles(faar_resampler **handles, size_t count) {
  int failed = 0;

  for (size_t i = 0; i < count; ++i) {
    if (faar_resampler_close(&handles[i]) != FAAR_OK)
      failed = 1;
  }
  return failed;
}

static int warm_up(const faar_config *config) {
  faar_resampler *ordinary = NULL;
  faar_resampler *planned = NULL;
  faar_plan *plan = NULL;

  if (faar_resampler_open(config, &ordinary) != FAAR_OK)
    return 0;
  if (faar_resampler_close(&ordinary) != FAAR_OK)
    return 0;
  if (faar_plan_open(config, &plan) != FAAR_OK)
    return 0;
  if (faar_resampler_open_from_plan(plan, &planned) != FAAR_OK) {
    faar_plan_close(&plan);
    return 0;
  }
  faar_resampler_close(&planned);
  faar_plan_close(&plan);
  return 1;
}

static int time_ordinary(const faar_config *config, size_t handle_count,
                         bench_result *result) {
  faar_resampler *handles[MAX_HANDLE_COUNT] = {0};
  double start = now_seconds();
  if (start < 0.0)
    return 0;

  size_t opened = 0;
  for (; opened < handle_count; ++opened) {
    if (faar_resampler_open(config, &handles[opened]) != FAAR_OK)
      break;
  }
  double finish = now_seconds();
  size_t live_bytes = faar_alloc_live_bytes();
  if (close_handles(handles, opened) || opened != handle_count ||
      finish < start)
    return 0;

  result->ordinary_ms = (finish - start) * 1000.0;
  result->ordinary_bytes = live_bytes;
  return 1;
}

static int time_plan(const faar_config *config, size_t handle_count,
                     bench_result *result) {
  faar_resampler *handles[MAX_HANDLE_COUNT] = {0};
  faar_plan *plan = NULL;
  double start = now_seconds();
  if (start < 0.0 || faar_plan_open(config, &plan) != FAAR_OK)
    return 0;
  double designed = now_seconds();
  if (designed < start) {
    faar_plan_close(&plan);
    return 0;
  }
  result->plan_design_ms = (designed - start) * 1000.0;
  result->plan_design_bytes = faar_alloc_live_bytes();

  start = now_seconds();
  if (start < 0.0) {
    faar_plan_close(&plan);
    return 0;
  }
  size_t opened = 0;
  for (; opened < handle_count; ++opened) {
    if (faar_resampler_open_from_plan(plan, &handles[opened]) != FAAR_OK)
      break;
  }
  double finish = now_seconds();
  if (opened != handle_count || finish < start) {
    close_handles(handles, opened);
    faar_plan_close(&plan);
    return 0;
  }

  result->plan_streams_ms = (finish - start) * 1000.0;
  result->plan_all_bytes = faar_alloc_live_bytes();
  int failed = close_handles(handles, opened);
  failed |= faar_plan_close(&plan) != FAAR_OK;
  return !failed;
}

static void sort_doubles(double values[REPEATS]) {
  for (size_t i = 1; i < REPEATS; ++i) {
    double value = values[i];
    size_t j = i;
    while (j > 0 && values[j - 1] > value) {
      values[j] = values[j - 1];
      --j;
    }
    values[j] = value;
  }
}

static void sort_sizes(size_t values[REPEATS]) {
  for (size_t i = 1; i < REPEATS; ++i) {
    size_t value = values[i];
    size_t j = i;
    while (j > 0 && values[j - 1] > value) {
      values[j] = values[j - 1];
      --j;
    }
    values[j] = value;
  }
}

static int benchmark_case(const bench_case *test_case, size_t handle_count) {
  faar_config config;
  bench_result results[REPEATS];
  double ordinary_ms[REPEATS], design_ms[REPEATS], streams_ms[REPEATS];
  size_t ordinary_bytes[REPEATS], design_bytes[REPEATS], all_bytes[REPEATS];

  if (faar_config_init(&config, sizeof(config)) != FAAR_OK)
    return 0;
  config.in_rate = test_case->in_rate;
  config.out_rate = test_case->out_rate;
  config.channels = test_case->channels;
  config.quality = test_case->quality;

  for (size_t i = 0; i < REPEATS; ++i) {
    if (!warm_up(&config))
      return 0;
    /* Reverse the measured order on alternating repetitions. */
    if (i & 1) {
      if (!time_plan(&config, handle_count, &results[i]) ||
          !time_ordinary(&config, handle_count, &results[i]))
        return 0;
    } else {
      if (!time_ordinary(&config, handle_count, &results[i]) ||
          !time_plan(&config, handle_count, &results[i]))
        return 0;
    }
    if (faar_alloc_live() != 0)
      return 0;
    ordinary_ms[i] = results[i].ordinary_ms;
    design_ms[i] = results[i].plan_design_ms;
    streams_ms[i] = results[i].plan_streams_ms;
    ordinary_bytes[i] = results[i].ordinary_bytes;
    design_bytes[i] = results[i].plan_design_bytes;
    all_bytes[i] = results[i].plan_all_bytes;
  }

  sort_doubles(ordinary_ms);
  sort_doubles(design_ms);
  sort_doubles(streams_ms);
  sort_sizes(ordinary_bytes);
  sort_sizes(design_bytes);
  sort_sizes(all_bytes);
  printf("%u,%u,%u,%u,%d,%.3f,%.3f,%.3f,%zu,%zu,%zu\n", test_case->in_rate,
         test_case->out_rate, test_case->channels, (unsigned)test_case->quality,
         (int)handle_count, ordinary_ms[REPEATS / 2], design_ms[REPEATS / 2],
         streams_ms[REPEATS / 2], ordinary_bytes[REPEATS / 2],
         design_bytes[REPEATS / 2], all_bytes[REPEATS / 2]);
  return 1;
}

static int parse_unsigned(const char *text, unsigned long *value) {
  char *end = NULL;
  errno = 0;
  unsigned long parsed = strtoul(text, &end, 10);
  if (errno == ERANGE || end == text || *end != '\0')
    return 0;
  *value = parsed;
  return 1;
}

int main(int argc, char **argv) {
  static const bench_case default_cases[] = {
      {48000, 44100, 2, FAAR_QUALITY_FAST},
      {48000, 44100, 2, FAAR_QUALITY_GOOD},
      {48000, 44100, 2, FAAR_QUALITY_HIGH},
      {48000, 44100, 8, FAAR_QUALITY_HIGH},
  };

  puts("in_rate,out_rate,channels,quality,handles,ordinary_open_median_ms,"
       "plan_design_median_ms,plan_streams_median_ms,ordinary_live_bytes,"
       "plan_design_bytes,plan_all_live_bytes");
  if (argc == 1) {
    for (size_t i = 0; i < sizeof(default_cases) / sizeof(default_cases[0]);
         ++i) {
      if (!benchmark_case(&default_cases[i], DEFAULT_HANDLE_COUNT))
        return 1;
    }
    return 0;
  }
  if (argc != 6) {
    fprintf(stderr, "usage: %s [in_rate out_rate channels quality handles]\n",
            argv[0]);
    return 2;
  }

  unsigned long in_rate, out_rate, channels, quality, handles;
  if (!parse_unsigned(argv[1], &in_rate) ||
      !parse_unsigned(argv[2], &out_rate) ||
      !parse_unsigned(argv[3], &channels) ||
      !parse_unsigned(argv[4], &quality) ||
      !parse_unsigned(argv[5], &handles) || in_rate == 0 ||
      in_rate > UINT32_MAX || out_rate == 0 || out_rate > UINT32_MAX ||
      channels == 0 || channels > 8 || quality > FAAR_QUALITY_HIGH ||
      handles == 0 || handles > MAX_HANDLE_COUNT) {
    fprintf(stderr, "custom workload expects valid rates, channels 1..8, "
                    "quality 0..2, handles 1..128\n");
    return 2;
  }
  bench_case custom = {(uint32_t)in_rate, (uint32_t)out_rate,
                       (uint16_t)channels, (faar_quality_t)quality};
  return benchmark_case(&custom, (size_t)handles) ? 0 : 1;
}
