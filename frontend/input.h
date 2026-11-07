/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2002 Krzysztof Nikiel
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#ifndef _INPUT_H
#define _INPUT_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
  FILE *f;
  uint16_t channels;
  uint8_t samplebytes;
  uint32_t samplerate;
  int64_t samples;
  uint64_t data_remaining;
  bool bounded_data;
  bool bigendian;
  bool isfloat;
  uint32_t channel_mask; /* WAVE_FORMAT_EXTENSIBLE speaker mask; 0 when absent */
} pcmfile_t;

/* Why wav_open_read_ex() failed. NOT_WAV means the header is not RIFF/RF64,
   so the caller may still treat the file as headerless PCM. */
typedef enum
{
  WAV_OPEN_NOT_WAV,
  WAV_OPEN_UNSUPPORTED,
  WAV_OPEN_MALFORMED,
  WAV_OPEN_IO
} wav_open_error_t;

pcmfile_t *wav_open_read(const char *path, bool rawchans);
pcmfile_t *wav_open_read_ex(const char *path, bool rawchans, wav_open_error_t *err);
/* The input order FAAC encodes for a channel count, as WAVE speaker names, or
   NULL for a count it has no layout for. */
const char *wav_layout_name(uint16_t channels);
/* True when a WAVE speaker mask lists the speakers of that layout in the
   order FAAC reads them. A zero mask says nothing and always matches. */
bool wav_mask_matches_layout(uint16_t channels, uint32_t mask);
size_t wav_read_float32(pcmfile_t *sndf, float *buf, size_t num, int *map);
int wav_close(pcmfile_t *file);

/* NULL on a successful read/valid EOF, otherwise a diagnostic. Check after
   every nonzero-size read, passing the returned sample count, before treating
   a short/zero count as end of input. */
const char *wav_read_error(const pcmfile_t *file, size_t samples_read);

/* True when the file's PCM can go to the encoder as-is (little-endian 16/24-bit
   integers), sparing the float conversion pass. */
bool wav_native_ok(const pcmfile_t *sndf);

/* Read up to num samples without conversion; returns the number read. */
size_t wav_read_native(pcmfile_t *sndf, void *buf, size_t num);

/* Create channel remapping array for multi-channel input, consumed by
   wav_read_float32()'s internal chan_remap(). */
int *mk_chan_map(uint16_t channels, uint16_t center, uint16_t lf);

#ifdef __cplusplus
}
#endif
#endif /* _INPUT_H */
