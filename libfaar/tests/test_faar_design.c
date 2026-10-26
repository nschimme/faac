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
#include <faar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef FAAR_PLAN_PTHREAD
#include <pthread.h>
#endif
#ifdef FAAR_PLAN_ALLOC_TEST
#include <faar_alloc_hooks.h>
#endif

#define N 321
#define OUT 97
#define CH 8
#define MAX_FRAMES (N * 8 + 1024)

static int bad(const char *s) {
  fprintf(stderr, "test_faar_design: %s\n", s);
  return 1;
}
static float sample(size_t i, size_t c, size_t seed) {
  uint32_t x =
      (uint32_t)(i * 2654435761u + c * 2246822519u + seed * 3266489917u);
  x ^= x >> 15;
  x *= 2246822519u;
  x ^= x >> 13;
  return (float)((int32_t)(x & 0xffffff) - 0x800000) / 8388608.0f;
}
static int config_for(faar_config *c, uint32_t in, uint32_t out, unsigned ch,
                      faar_quality q, uint32_t dither) {
  if (faar_config_init(c, sizeof(*c)) != FAAR_OK)
    return 0;
  c->in_rate = in;
  c->out_rate = out;
  c->channels = (uint16_t)ch;
  c->quality = q;
  c->flags = dither ? dither | FAAR_DITHER_16 : 0;
  return 1;
}
static int stream(faar_resampler *r, unsigned ch, size_t seed, float *dst,
                  size_t *frames) {
  float in[N * CH], out[OUT * CH];
  size_t got = 0, pos = 0, calls = 0;
  for (size_t i = 0; i < N; i++)
    for (unsigned c = 0; c < ch; c++)
      in[i * ch + c] = sample(i, c, seed);
  while (pos < N) {
    size_t n = (pos % 11) + 1;
    if (n > N - pos)
      n = N - pos;
    size_t made = 999;
    if (faar_resampler_process(r, in + pos * ch, n, out, OUT, &made) !=
            FAAR_OK ||
        made > OUT || made > MAX_FRAMES - got)
      return 0;
    memcpy(dst + got * ch, out, made * ch * sizeof(float));
    got += made;
    pos += n;
    do {
      made = 999;
      if (faar_resampler_process(r, NULL, 0, out, OUT, &made) != FAAR_OK ||
          made > OUT || made > MAX_FRAMES - got)
        return 0;
      memcpy(dst + got * ch, out, made * ch * sizeof(float));
      got += made;
    } while (made && ++calls < 10000);
    if (calls >= 10000)
      return 0;
  }
  for (size_t guard = 0; guard < 10000; guard++) {
    size_t made = 0;
    if (faar_resampler_finalize(r, out, OUT, &made) != FAAR_OK || made > OUT ||
        made > MAX_FRAMES - got)
      return 0;
    memcpy(dst + got * ch, out, made * ch * sizeof(float));
    got += made;
    if (!made) {
      *frames = got;
      return 1;
    }
  }
  return 0;
}
static int compare(const faar_config *c) {
  faar_design *p = NULL;
  faar_resampler *a = NULL, *b = NULL;
  float x[MAX_FRAMES * CH], y[MAX_FRAMES * CH];
  size_t nx = 0, ny = 0;
  faar_config reserved = *c;
  reserved.max_input_frames = N; /* the design reserves each stream's storage */
  if (faar_design_open(&reserved, &p) != FAAR_OK || !p)
    return 0;
  if (faar_resampler_open(c, &a) != FAAR_OK ||
      faar_resampler_open_from_design(p, &b) != FAAR_OK)
    return 0;
  if (!stream(a, c->channels, 19, x, &nx))
    return 0;
  faar_resampler_close(&a);
  faar_design_close(&p); /* stream keeps plan alive */
#ifdef FAAR_PLAN_ALLOC_TEST
  size_t prepared_calls = faar_alloc_calls();
  size_t prepared_frees = faar_alloc_frees();
#endif
  if (!stream(b, c->channels, 19, y, &ny) || nx != ny ||
      memcmp(x, y, nx * c->channels * sizeof(float)))
    return 0;
#ifdef FAAR_PLAN_ALLOC_TEST
  if (faar_alloc_calls() != prepared_calls ||
      faar_alloc_frees() != prepared_frees)
    return 0;
#endif
  faar_resampler_close(&b);
  return 1;
}
static int multi_stream(const faar_config *c) {
  static float a[3][MAX_FRAMES * CH], b[MAX_FRAMES * CH];
  faar_design *p = NULL;
  faar_resampler *r[3] = {0}, *ref[3] = {0};
  size_t na[3], nb;
  if (faar_design_open(c, &p) != FAAR_OK)
    return 0;
  for (int i = 0; i < 3; i++)
    if (faar_resampler_open_from_design(p, &r[i]) != FAAR_OK ||
        faar_resampler_open(c, &ref[i]) != FAAR_OK)
      return 0;
  for (int i = 0; i < 3; i++)
    if (!stream(r[i], c->channels, 40 + i, a[i], &na[i]) ||
        !stream(ref[i], c->channels, 40 + i, b, &nb) || na[i] != nb ||
        memcmp(a[i], b, nb * c->channels * sizeof(float)))
      return 0;
  for (int i = 0; i < 3; i++) {
    faar_resampler_close(&r[i]);
    faar_resampler_close(&ref[i]);
  }
  faar_design_close(&p);
  return 1;
}
#ifdef FAAR_PLAN_PTHREAD
typedef struct {
  faar_design *plan;
  faar_config config;
  unsigned id;
  int ok;
} worker_arg;
static pthread_mutex_t gate_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_cond = PTHREAD_COND_INITIALIZER;
static int gate_open;
static void *worker(void *opaque) {
  pthread_mutex_lock(&gate_mutex);
  while (!gate_open)
    pthread_cond_wait(&gate_cond, &gate_mutex);
  pthread_mutex_unlock(&gate_mutex);
  worker_arg *a = opaque;
  faar_resampler *r = NULL, *ref = NULL;
  float x[MAX_FRAMES * CH], y[MAX_FRAMES * CH];
  size_t nx, ny;
  a->ok = faar_resampler_open_from_design(a->plan, &r) == FAAR_OK &&
          faar_resampler_open(&a->config, &ref) == FAAR_OK;
  if (a->ok)
    a->ok = stream(r, a->config.channels, a->id, x, &nx) &&
            stream(ref, a->config.channels, a->id, y, &ny) && nx == ny &&
            !memcmp(x, y, nx * a->config.channels * sizeof(float));
  faar_resampler_close(&r);
  faar_resampler_close(&ref);
  return NULL;
}
static int concurrent(const faar_config *c) {
  pthread_t t[4];
  worker_arg a[4];
  faar_design *p = NULL;
  if (faar_design_open(c, &p) != FAAR_OK)
    return 0;
  gate_open = 0;
  unsigned started = 0;
  for (unsigned i = 0; i < 4; i++) {
    a[i] = (worker_arg){p, *c, i + 90, 0};
    if (pthread_create(&t[i], NULL, worker, &a[i])) {
      pthread_mutex_lock(&gate_mutex);
      gate_open = 1;
      pthread_cond_broadcast(&gate_cond);
      pthread_mutex_unlock(&gate_mutex);
      for (unsigned j = 0; j < started; ++j)
        pthread_join(t[j], NULL);
      faar_design_close(&p);
      return 0;
    }
    started++;
  }
  pthread_mutex_lock(&gate_mutex);
  gate_open = 1;
  pthread_cond_broadcast(&gate_cond);
  pthread_mutex_unlock(&gate_mutex);
  int ok = 1;
  for (int i = 0; i < 4; i++) {
    pthread_join(t[i], NULL);
    ok &= a[i].ok;
  }
  faar_design_close(&p);
  return ok;
}
#endif
#ifdef FAAR_PLAN_ALLOC_TEST
static int allocation_failures(const faar_config *cfg) {
  faar_design *plan = NULL;
  faar_resampler *stream = NULL;
  size_t live = faar_alloc_live();
  faar_alloc_reset_counters();
  if (faar_design_open(cfg, &plan) != FAAR_OK || !plan)
    return 0;
  size_t plan_points = faar_alloc_calls();
  faar_design_close(&plan);
  if (faar_alloc_live() != live)
    return 0;
  for (size_t nth = 1; nth <= plan_points; nth++) {
    faar_alloc_fail_nth(nth);
    plan = NULL;
    faar_status status = faar_design_open(cfg, &plan);
    faar_alloc_fail_nth(0);
    if (status == FAAR_OK || plan || faar_alloc_live() != live)
      return 0;
  }
  if (faar_design_open(cfg, &plan) != FAAR_OK || !plan)
    return 0;
  size_t plan_live = faar_alloc_live();
  faar_alloc_reset_counters();
  if (faar_resampler_open_from_design(plan, &stream) != FAAR_OK || !stream)
    return 0;
  size_t stream_points = faar_alloc_calls();
  faar_resampler_close(&stream);
  if (faar_alloc_live() != plan_live)
    return 0;
  for (size_t nth = 1; nth <= stream_points; nth++) {
    faar_alloc_fail_nth(nth);
    stream = NULL;
    faar_status status = faar_resampler_open_from_design(plan, &stream);
    faar_alloc_fail_nth(0);
    if (status == FAAR_OK || stream || faar_alloc_live() != plan_live)
      return 0;
  }
  faar_design_close(&plan);
  return faar_alloc_live() == live;
}
#endif
int main(void) {
  faar_library_info li = {0};
  li.struct_size = sizeof(li);
  if (faar_get_library_info(&li) != FAAR_OK)
    return bad("library info");
  if (faar_design_open(NULL, (faar_design **)0) != FAAR_ERR_INVALID_ARGUMENT)
    return bad("null plan arguments");
  faar_config c;
  if (!config_for(&c, 48000, 44100, 1, FAAR_QUALITY_FAST, 0))
    return bad("config");
  faar_design *p = NULL;
  if (faar_design_open(&c, &p) != FAAR_OK)
    return bad("open");
  faar_resampler *r = NULL;
  if (faar_resampler_open_from_design(p, &r) != FAAR_OK)
    return bad("stream open");
  if (faar_design_close(&p) != FAAR_OK || p != NULL)
    return bad("plan close");
  faar_resampler_close(&r);
  faar_design *nullp = NULL;
  if (faar_resampler_open_from_design(NULL, &r) != FAAR_ERR_INVALID_ARGUMENT ||
      r || faar_design_close(&nullp) != FAAR_OK)
    return bad("null handling");
  const uint32_t rates[][2] = {{44100, 48000}, {48000, 24000}, {96000, 8000},
                               {32000, 32000}, {48000, 44100}, {48000, 32000},
                               {48000, 22050}, {48000, 50000}};
  const unsigned channel_cases[] = {1, 2, 3, 8};
  for (size_t route = 0; route < sizeof(rates) / sizeof(rates[0]); route++)
    for (int q = 0; q <= 2; q++)
      for (size_t ci = 0; ci < sizeof(channel_cases) / sizeof(channel_cases[0]);
           ci++) {
        unsigned ch = channel_cases[ci];
        if (ch > li.max_channels)
          continue;
        for (int d = 0; d <= 1; d++) {
          if (!config_for(
                  &c, rates[route][0], rates[route][1], ch, (faar_quality)q,
                  (d ? FAAR_DITHER_SHAPED : 0u)))
            return bad("config");
          if (!compare(&c) || !multi_stream(&c))
            return bad("route comparison or independent streams");
        }
      }
#ifdef FAAR_PLAN_PTHREAD
  if (!config_for(&c, 96000, 32000, li.max_channels < 2 ? 1 : 2,
                  FAAR_QUALITY_HIGH, FAAR_DITHER_SHAPED) ||
      !concurrent(&c))
    return bad("concurrent opens and streams");
#endif
#ifdef FAAR_PLAN_ALLOC_TEST
  const struct {
    uint32_t in, out;
    uint16_t ch;
    faar_quality q;
  } alloc_cases[] = {
      {48000, 44100, 2, FAAR_QUALITY_HIGH},
      {96000, 8000, 2, FAAR_QUALITY_GOOD},
      {48000, 50000, 2, FAAR_QUALITY_FAST},
  };
  for (size_t i = 0; i < sizeof(alloc_cases) / sizeof(alloc_cases[0]); i++) {
    if (!config_for(&c, alloc_cases[i].in, alloc_cases[i].out,
                    alloc_cases[i].ch > li.max_channels ? li.max_channels
                                                        : alloc_cases[i].ch,
                    alloc_cases[i].q, 0) ||
        !allocation_failures(&c))
      return bad("allocation failure sweep or recovery");
  }
#endif
  puts("test_faar_design: ok");
  return 0;
}
