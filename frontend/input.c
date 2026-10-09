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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include "input.h"
#include "cli_io.h"
#include "charset.h"
#include "endian.h"

#define PCM_16BIT_FLOAT_SCALE 32768.0f
#define PCM_32BIT_FLOAT_SCALE 65536.0f


typedef struct
{
  uint32_t label;           /* 'RIFF' */
  uint32_t length;        /* Length of rest of file */
  uint32_t chunk_type;      /* 'WAVE' */
}
riff_t;

typedef struct
{
  uint32_t label;
  uint64_t len;
}
riffsub_t;

#ifdef _MSC_VER
#pragma pack(push, 1)
#endif

#define WAVE_FORMAT_PCM		1
#define WAVE_FORMAT_FLOAT	3
#define WAVE_FORMAT_EXTENSIBLE	0xfffe
struct WAVEFORMATEX
{
  uint16_t wFormatTag;
  uint16_t nChannels;
  uint32_t nSamplesPerSec;
  uint32_t nAvgBytesPerSec;
  uint16_t nBlockAlign;
  uint16_t wBitsPerSample;
  uint16_t cbSize;
}
#ifdef __GNUC__
__attribute__((packed))
#endif
;

struct WAVEFORMATEXTENSIBLE
{
  struct WAVEFORMATEX Format;
  union {
    uint16_t wValidBitsPerSample;	// bits of precision
    uint16_t wSamplesPerBlock;		// valid if wBitsPerSample==0
    uint16_t wReserved;		// If neither applies, set to zero.
  } Samples;
  uint32_t dwChannelMask;		// which channels are present in stream
  unsigned char SubFormat[16];		// guid
}
#ifdef __GNUC__
__attribute__((packed))
#endif
;

#ifdef _MSC_VER
#pragma pack(pop)
#endif

static const unsigned char waveformat_pcm_guid[16] =
{
  WAVE_FORMAT_PCM,0,0,0,
  0x00, 0x00,
  0x10, 0x00,
  0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71
};

static const unsigned char waveformat_float_guid[16] =
{
  WAVE_FORMAT_FLOAT,0,0,0,
  0x00, 0x00,
  0x10, 0x00,
  0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71
};

static void unsuperr(const char *name)
{
  fprintf(stderr, "%s: file format not supported\n", name);
}

typedef struct {
    char id[4];
    uint64_t size;
    bool used;
} rf64_entry;

typedef struct {
    bool enabled;
    uint64_t data_size;
    uint32_t count;
    rf64_entry *entries;
} rf64_sizes;

static bool seekcur(FILE *f, uint64_t ofs)
{
    uint64_t pos = cli_ftell(f);
    if (pos != UINT64_MAX && ofs <= INT64_MAX && pos <= INT64_MAX - ofs &&
        cli_fseek(f, pos + ofs)) return true;
    uint8_t discard[16384];
    while (ofs) {
        size_t n = ofs < sizeof(discard) ? (size_t)ofs : sizeof(discard);
        if (fread(discard, 1, n, f) != n) return false;
        ofs -= n;
    }
    return true;
}

static bool read_ds64(FILE *f, rf64_sizes *sizes)
{
    uint8_t header[8], body[28];
    uint32_t length, count;
    uint64_t data_size;
    if (fread(header, 1, 8, f) != 8 || memcmp(header, "ds64", 4)) return false;
    memcpy(&length, header + 4, 4);
    length = le32toh(length);
    if (length < 28 || fread(body, 1, 28, f) != 28) return false;
    memcpy(&data_size, body + 8, 8);
    memcpy(&count, body + 24, 4);
    sizes->data_size = le64toh(data_size);
    sizes->count = le32toh(count);
    if (sizes->data_size > INT64_MAX || sizes->count > 65536 ||
        sizes->count > (length - 28) / 12) return false;
    if (sizes->count) {
        sizes->entries = calloc(sizes->count, sizeof(*sizes->entries));
        if (!sizes->entries) return false;
    }
    for (uint32_t i = 0; i < sizes->count; i++) {
        uint8_t entry[12];
        if (fread(entry, 1, 12, f) != 12) return false;
        memcpy(sizes->entries[i].id, entry, 4);
        memcpy(&data_size, entry + 4, 8);
        sizes->entries[i].size = le64toh(data_size);
    }
    return seekcur(f, (uint64_t)length + (length & 1) - 28 - 12 * sizes->count);
}

static int seekchunk(FILE *f, riffsub_t *chunk, const char *name, rf64_sizes *sizes)
{
    for (unsigned skipped = 0; skipped < 10000; skipped++) {
        uint8_t header[8];
        uint32_t length;
        if (fread(header, 1, 8, f) != 8) return 0;
        memcpy(&chunk->label, header, 4);
        memcpy(&length, header + 4, 4);
        chunk->len = le32toh(length);
        if (sizes->enabled && chunk->len == UINT32_MAX) {
            if (!memcmp(header, "data", 4)) chunk->len = sizes->data_size;
            else {
                bool found = false;
                for (uint32_t i = 0; i < sizes->count; i++) {
                    rf64_entry *entry = &sizes->entries[i];
                    if (!entry->used && !memcmp(entry->id, header, 4)) {
                        chunk->len = entry->size;
                        entry->used = true;
                        found = true;
                        break;
                    }
                }
                if (!found) return 0;
            }
        }
        if (!memcmp(header, name, 4)) return 1;
        if (chunk->len > INT64_MAX || !seekcur(f, chunk->len + (chunk->len & 1))) return 0;
    }
    return 0;
}

pcmfile_t *wav_open_read(const char *name, bool rawinput)
{
  FILE *wave_f;
  riff_t riff;
  riffsub_t riffsub = {0};
  struct WAVEFORMATEXTENSIBLE wave = {0};
  int fmtsize;
  pcmfile_t *sndf;
  int dostdin = 0;
  rf64_sizes sizes = {0};

  if (!strcmp(name, "-"))
  {
#ifdef _WIN32
    _setmode(_fileno(stdin), O_BINARY);
#endif
    wave_f = stdin;
    dostdin = 1;
  }
  else
  {
    wave_f = cli_fopen(name, "rb");
    if (!wave_f)
    {
      perror(name);
      return NULL;
    }
  }

  if (!rawinput) // header input
  {
    if (fread(&riff, 1, sizeof(riff), wave_f) != sizeof(riff))
      goto bad;
    sizes.enabled = !memcmp(&(riff.label), "RF64", 4);
    if (!sizes.enabled && memcmp(&(riff.label), "RIFF", 4)) goto bad;
    if (sizes.enabled && le32toh(riff.length) != UINT32_MAX) goto bad;
    if (memcmp(&(riff.chunk_type), "WAVE", 4))
      goto bad;

    if (sizes.enabled && !read_ds64(wave_f, &sizes)) goto bad;

    if (!seekchunk(wave_f, &riffsub, "fmt ", &sizes))
      goto bad;

    if (memcmp(&(riffsub.label), "fmt ", 4))
        goto bad;
    memset(&wave, 0, sizeof(wave));

    fmtsize = (riffsub.len < sizeof(wave)) ? riffsub.len : sizeof(wave);
    // check if format is at least 16 bytes long
    if (fmtsize < 16)
	goto bad;

   if (fread(&wave, 1, fmtsize, wave_f) != (size_t)fmtsize)
        goto bad;

    if (riffsub.len > INT64_MAX ||
        !seekcur(wave_f, riffsub.len + (riffsub.len & 1) - fmtsize)) goto bad;

    if (!seekchunk(wave_f, &riffsub, "data", &sizes))
      goto bad;

    uint16_t tag = le16toh(wave.Format.wFormatTag);
    if (tag != WAVE_FORMAT_PCM && tag != WAVE_FORMAT_FLOAT)
    {
      if (tag == WAVE_FORMAT_EXTENSIBLE)
      {
        if (le16toh(wave.Format.cbSize) < 22) // struct too small
          goto bad;
        if (memcmp(wave.SubFormat, waveformat_pcm_guid, 16) != 0 &&
            memcmp(wave.SubFormat, waveformat_float_guid, 16) != 0)
        {
          unsuperr(name);
          goto bad;
        }
      }
      else
      {
        unsuperr(name);
        goto bad;
      }
    }
  }

  free(sizes.entries);
  sizes.entries = NULL;
  sndf = (pcmfile_t*)malloc(sizeof(*sndf));
  if (!sndf)
  {
      if (wave_f != stdin) fclose(wave_f);
      return NULL;
  }
  memset(sndf, 0, sizeof(*sndf));
  sndf->f = wave_f;

  /* rawinput never populates `wave` (no header to parse), and raw mode has
     no float flag of its own -- isfloat stays false, its memset() default. */
  if (!rawinput)
  {
    if (le16toh(wave.Format.wFormatTag) == WAVE_FORMAT_FLOAT) {
      sndf->isfloat = true;
    } else {
      sndf->isfloat = (wave.SubFormat[0] == WAVE_FORMAT_FLOAT);
    }
  }

  if (rawinput)
  {
    sndf->bigendian = true;
    if (dostdin)
      sndf->samples = 0;
    else
    {
      uint64_t bytes = 0;
      cli_fsize(sndf->f, &bytes);
      sndf->samples = (int64_t)bytes;
    }
  }
  else
  {
    sndf->bigendian = false;
    sndf->channels = le16toh(wave.Format.nChannels);
    sndf->samplebytes = (uint8_t)(le16toh(wave.Format.wBitsPerSample) / 8);
    sndf->samplerate = le32toh(wave.Format.nSamplesPerSec);

    /* channel/sample-width bounds guard against a corrupt header (e.g. a
       bogus huge channel count) driving an oversized allocation downstream */
    if (sndf->channels < 1 || sndf->channels > 64 ||
        sndf->samplebytes < 1 || sndf->samplebytes > 4)
    {
      if (wave_f != stdin) fclose(wave_f);
      free(sndf);
      return NULL;
    }

    if (riffsub.len > INT64_MAX) { free(sndf); goto bad; }
    sndf->bounded_data = sizes.enabled || riffsub.len != UINT32_MAX;
    sndf->data_remaining = riffsub.len;
    sndf->samples = sndf->bounded_data
        ? (int64_t)riffsub.len / ((int64_t)sndf->samplebytes * sndf->channels) : 0;
  }

  return sndf;

bad:
  free(sizes.entries);
  if (wave_f != stdin) fclose(wave_f);
  return NULL;
}

int *mk_chan_map(uint16_t channels, uint16_t center, uint16_t lf)
{
  if (!center && !lf)
    return NULL;

  if (channels < 3)
    return NULL;

  uint16_t lf_pos = (lf > 0) ? (lf - 1) : (uint16_t)(channels - 1);       /* default AAC position */
  uint16_t center_pos = (center > 0) ? (center - 1) : 0;                 /* default AAC position */

  int *map = malloc((size_t)channels * sizeof(int));
  if (!map)
    return NULL;
  memset(map, 0, (size_t)channels * sizeof(int));

  uint16_t outpos = 0;
  if (center_pos < channels)
    map[outpos++] = (int)center_pos;

  uint16_t inpos = 0;
  for (; outpos < (channels - 1) && inpos < channels; inpos++)
  {
    if (inpos == center_pos || inpos == lf_pos)
      continue;

    map[outpos++] = (int)inpos;
  }
  if (outpos < channels)
  {
    if (lf_pos < channels)
      map[outpos] = (int)lf_pos;
    else if (inpos < channels)
      map[outpos] = (int)inpos;
  }

  return map;
}

static void chan_remap(int32_t *buf, int channels, int blocks, int *map)
{
  if (channels < 1)
    return;

  int32_t tmp_stack[64];
  int32_t *tmp = tmp_stack;

  if (channels > 64)
  {
    tmp = (int32_t *)malloc((size_t)channels * sizeof(int32_t));
    if (!tmp)
      return;
  }

  for (int i = 0; i < blocks; i++)
  {
    memcpy(tmp, buf + i * channels, (size_t)channels * sizeof(int32_t));

    for (int chn = 0; chn < channels; chn++)
      buf[i * channels + chn] = tmp[map[chn]];
  }

  if (channels > 64)
    free(tmp);
}

size_t wav_read_float32(pcmfile_t *sndf, float *buf, size_t num, int *map)
{
  size_t cnt;
  size_t isize;
  char *bufi;

  if ((sndf->samplebytes > 4) || (sndf->samplebytes < 1))
    return 0;

  isize = num * sndf->samplebytes;
  bufi = (char*)(buf + num);
  bufi -= isize;
  if (sndf->bounded_data && isize > sndf->data_remaining) isize = (size_t)sndf->data_remaining;
  isize = fread(bufi, 1, isize, sndf->f);
  if (sndf->bounded_data) sndf->data_remaining -= isize;
  isize /= sndf->samplebytes;

  // perform in-place conversion
  cnt = (num < isize) ? num : isize;

  if (sndf->isfloat)
  {
      if (sndf->samplebytes == 4)
      {
          for (size_t i = 0; i < cnt; i++) {
              uint32_t bits;
              float value;
              memcpy(&bits, bufi + 4 * i, 4);
              bits = sndf->bigendian ? be32toh(bits) : le32toh(bits);
              memcpy(&value, &bits, 4);
              buf[i] = value * PCM_16BIT_FLOAT_SCALE;
          }
      }
      else
      {
          return 0;
      }
  }
  else
  {
      switch (sndf->samplebytes)
      {
      case 1:
          {
              uint8_t *in = (uint8_t*)bufi;
              for (size_t i = 0; i < cnt; i++)
                  buf[i] = ((float)in[i] - 128.0f) * 256.0f;
          }
          break;
      case 2:
          {
              const int16_t *in = (const int16_t *)bufi;
              bool be = sndf->bigendian;
              for (size_t i = 0; i < cnt; i++)
                  buf[i] = (float)read_pcm16(&in[i], be);
          }
          break;
      case 3:
          {
              const uint8_t *in = (const uint8_t *)bufi;
              bool be = sndf->bigendian;
              for (size_t i = 0; i < cnt; i++)
                  buf[i] = (float)read_pcm24(&in[3*i], be) / 256.0f;
          }
          break;
      case 4:
          {
              const int32_t *in = (const int32_t *)bufi;
              bool be = sndf->bigendian;
              for (size_t i = 0; i < cnt; i++)
                  buf[i] = (float)read_pcm32(&in[i], be) / PCM_32BIT_FLOAT_SCALE;
          }
          break;
      default:
          return 0;
      }
  }

  if (map)
    chan_remap((int32_t *)buf, sndf->channels, (int)(cnt / sndf->channels), map);

  return cnt;
}

bool wav_native_ok(const pcmfile_t *sndf)
{
  return !sndf->isfloat && sndf->bigendian == WORDS_BIGENDIAN &&
         (sndf->samplebytes == 2 || sndf->samplebytes == 3);
}

size_t wav_read_native(pcmfile_t *sndf, void *buf, size_t num)
{
  if (sndf->bounded_data && num > sndf->data_remaining / sndf->samplebytes)
    num = (size_t)(sndf->data_remaining / sndf->samplebytes);
  size_t got = fread(buf, sndf->samplebytes, num, sndf->f);
  if (sndf->bounded_data) sndf->data_remaining -= got * sndf->samplebytes;
  return got;
}

int wav_close(pcmfile_t *sndf)
{
  int i = fclose(sndf->f);
  free(sndf);
  return i;
}
