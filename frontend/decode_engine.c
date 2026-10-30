/*
 * FAAD - Freeware Advanced Audio Decoder
 * Copyright (C) 2026 Nils Schimmelmann
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
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdarg.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif

#include "faad3.h"
#include "adts.h"
#include "charset.h"
#include "cli_io.h"
#include "endian.h"
#include "asc_codec.h"
#include "mp4read.h"
#include "decode_engine.h"

#define INPUT_BUFFER_SIZE (256u * 1024u)
#define INPUT_MEMORY_LIMIT (32u * 1024u * 1024u)
#define PCM_IO_BUFFER_SIZE (256u * 1024u)

typedef struct {
    uint8_t *data;
    uint32_t size;
    uint32_t head;
    uint32_t tail;
    uint32_t fill;
} PCMFifo;

typedef struct {
    FILE *fout;
    char *io_buf;
    bool write_stdout;
    bool raw_format;
    bool is_float;
    uint32_t bit_depth;
    uint16_t header_channels;
    uint32_t header_channel_mask;
    uint32_t header_sample_rate;
    bool header_pending;
    uint64_t total_pcm_bytes;

    bool is_mp4;
    bool gapless;
    uint32_t samples_to_skip;
    uint32_t padding_samples;
    bool gapless_scaled;
    uint32_t padding_bytes;
    bool fallback_mode;
    bool fifo_initialized;
    bool trim_file;
    uint64_t data_offset;
    PCMFifo fifo;
} PCMWriter;

void init_decode_options(decode_options_t *opts)
{
    if (!opts)
        return;

    memset(opts, 0, sizeof(*opts));
    opts->bit_depth = 16;
    opts->gapless = true;
    opts->verbose = 1;
}

static double calc_speed(uint64_t current_sample, unsigned int sample_rate, double time_used)
{
    if (time_used < 0.001 || sample_rate == 0)
        return 0.0;

    return ((double)current_sample / (double)sample_rate) / time_used;
}

static double calc_eta(uint64_t current_sample, uint64_t total_samples, double time_used)
{
    if (current_sample == 0 || time_used < 0.001 || total_samples < current_sample)
        return 0.0;

    return time_used * (double)(total_samples - current_sample) / (double)current_sample;
}

static double get_wall_time_sec(void)
{
#ifdef _WIN32
    return (double)GetTickCount() / 1000.0;
#else
#ifdef CLOCK_MONOTONIC
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == 0)
        return (double)tv.tv_sec + (double)tv.tv_usec * 1e-6;
    return (double)clock() / CLOCKS_PER_SEC;
#endif
}

static progress_info_t build_progress_info(uint64_t current_input_samples,
                                            uint64_t total_input_samples,
                                            uint32_t sample_rate, uint16_t num_channels,
                                            uint32_t current_frame, uint32_t total_frames,
                                            uint64_t total_bytes_written, double time_used,
                                            bool is_final)
{
    return (progress_info_t){
        .current_input_samples = current_input_samples,
        .total_input_samples = total_input_samples,
        .sample_rate = sample_rate,
        .num_channels = num_channels,
        .current_frame = current_frame,
        .total_frames = total_frames,
        .total_bytes_written = total_bytes_written,
        .time_elapsed_sec = time_used,
        .speed_factor = calc_speed(current_input_samples, sample_rate, time_used),
        .eta_sec = is_final ? 0.0 : calc_eta(current_input_samples, total_input_samples, time_used),
        .is_final = is_final
    };
}

static void log_msgf(decode_log_message_callback_t log_cb, void *user_data,
                      int level, const char *fmt, ...)
{
    if (!log_cb)
        return;

    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    log_cb(level, msg, user_data);
}

static void json_str(const char *s)
{
    putchar('"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') printf("\\%c", c);
        else if (c < 0x20) printf("\\u%04x", c);
        else putchar(c);
    }
    putchar('"');
}

static bool fifo_init(PCMFifo *f, uint32_t capacity)
{
    f->data = (uint8_t *)malloc(capacity > 0 ? capacity : 65536);
    if (!f->data) {
        f->size = 0;
        f->head = 0;
        f->tail = 0;
        f->fill = 0;
        return false;
    }
    f->size = capacity > 0 ? capacity : 65536;
    f->head = 0;
    f->tail = 0;
    f->fill = 0;
    return true;
}

static void fifo_free(PCMFifo *f)
{
    if (f->data) free(f->data);
    f->data = NULL;
    f->size = 0;
    f->head = 0;
    f->tail = 0;
    f->fill = 0;
}

static bool fifo_push(PCMFifo *f, const uint8_t *src, uint32_t len)
{
    if (len == 0) return true;
    if (len > UINT32_MAX - f->fill) return false;
    if (f->fill + len > f->size) {
        uint32_t new_size = f->size > 0 ? f->size : 65536;
        while (new_size < f->fill + len) {
            if (new_size > UINT32_MAX / 2) { new_size = f->fill + len; break; }
            new_size *= 2;
        }
        uint8_t *new_data = (uint8_t *)malloc(new_size);
        if (!new_data) return false;
        if (f->fill > 0) {
            if (f->tail < f->head) {
                memcpy(new_data, f->data + f->tail, f->fill);
            } else {
                uint32_t first = f->size - f->tail;
                memcpy(new_data, f->data + f->tail, first);
                memcpy(new_data + first, f->data, f->head);
            }
        }
        if (f->data) free(f->data);
        f->data = new_data;
        f->size = new_size;
        f->tail = 0;
        f->head = f->fill;
    }
    uint32_t first = f->size - f->head;
    if (len <= first) {
        memcpy(f->data + f->head, src, len);
        f->head = (f->head + len) % f->size;
    } else {
        memcpy(f->data + f->head, src, first);
        memcpy(f->data, src + first, len - first);
        f->head = len - first;
    }
    f->fill += len;
    return true;
}

static uint32_t fifo_write(FILE *out, PCMFifo *f, uint32_t len)
{
    if (len > f->fill) len = f->fill;
    if (len == 0) return 0;

    uint32_t first = f->size - f->tail;
    if (first > len) first = len;
    fwrite(f->data + f->tail, 1, first, out);
    if (first < len)
        fwrite(f->data, 1, len - first, out);

    uint32_t to_end = f->size - f->tail;
    f->tail = len >= to_end ? len - to_end : f->tail + len;
    f->fill -= len;
    return len;
}

static void fifo_truncate_tail(PCMFifo *f, uint32_t bytes_to_remove)
{
    if (bytes_to_remove >= f->fill) {
        f->head = 0;
        f->tail = 0;
        f->fill = 0;
        return;
    }
    if (f->head >= bytes_to_remove) {
        f->head -= bytes_to_remove;
    } else {
        f->head = f->size - (bytes_to_remove - f->head);
    }
    f->fill -= bytes_to_remove;
}

static void pcm_to_little_endian(uint8_t *buf, uint32_t bytes, uint32_t sample_bytes, bool int24_in_32)
{
    if (int24_in_32) {
        for (uint32_t i = 0; i + 4 <= bytes; i += 4) {
            uint32_t v;
            memcpy(&v, buf + i, 4);
            v <<= 8;
            memcpy(buf + i, &v, 4);
        }
    }
    if (!FAAC_BIG_ENDIAN)
        return;
    for (uint32_t i = 0; i + sample_bytes <= bytes; i += sample_bytes) {
        uint8_t *p = buf + i;
        if (sample_bytes == 2) {
            uint16_t v; memcpy(&v, p, 2); v = htole16(v); memcpy(p, &v, 2);
        } else if (sample_bytes == 3) {
            write_pcm24_le(p, read_pcm24_be(p));
        } else {
            uint32_t v; memcpy(&v, p, 4); v = htole32(v); memcpy(p, &v, 4);
        }
    }
}

static bool write_wav_header(FILE *f, uint32_t sample_rate, uint16_t num_channels, uint64_t total_pcm_bytes, uint16_t bits_per_sample, bool is_float, uint32_t channel_mask)
{
    bool stream = (f == stdout);
    if (!stream && !cli_fseek(f, 0)) return false;
    bool extensible = num_channels > 2;
    uint32_t fmt_size = extensible ? 40 : 16;
    uint64_t riff_size = 4 + 36 + 8 + fmt_size + 8 + total_pcm_bytes + (total_pcm_bytes & 1);
    bool rf64 = !stream && riff_size > UINT32_MAX;
    uint32_t file_size = htole32(stream || rf64 ? UINT32_MAX : (uint32_t)riff_size);
    uint16_t bytes_per_sample = bits_per_sample / 8;
    uint32_t byte_rate = htole32(sample_rate * num_channels * bytes_per_sample);
    uint16_t block_align = htole16(num_channels * bytes_per_sample);
    uint32_t sr_le = htole32(sample_rate);
    uint16_t ch_le = htole16(num_channels);
    uint16_t bps_le = htole16(bits_per_sample);

    fwrite(rf64 ? "RF64" : "RIFF", 1, 4, f);
    fwrite(&file_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite(rf64 ? "ds64" : "JUNK", 1, 4, f);
    uint32_t ds_size = htole32(28), table_length = 0;
    uint64_t sizes[3] = { htole64(rf64 ? riff_size : 0),
                         htole64(rf64 ? total_pcm_bytes : 0),
                         htole64(rf64 ? total_pcm_bytes / (num_channels * (bits_per_sample / 8)) : 0) };
    fwrite(&ds_size, 4, 1, f);
    fwrite(sizes, 8, 3, f);
    fwrite(&table_length, 4, 1, f);
    fwrite("fmt ", 1, 4, f);

    uint32_t fmt_chunk_size = htole32(fmt_size);
    uint16_t audio_format = htole16(extensible ? 0xFFFE : (is_float ? 3 : 1));
    fwrite(&fmt_chunk_size, 4, 1, f);
    fwrite(&audio_format, 2, 1, f);
    fwrite(&ch_le, 2, 1, f);
    fwrite(&sr_le, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bps_le, 2, 1, f);
    if (extensible) {
        uint16_t cb_size = htole16(22);
        uint16_t valid_bits = htole16(bits_per_sample);
        uint32_t mask = htole32(channel_mask);
        uint8_t guid[16] = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
                             0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 };
        if (is_float) guid[0] = 0x03;
        fwrite(&cb_size, 2, 1, f);
        fwrite(&valid_bits, 2, 1, f);
        fwrite(&mask, 4, 1, f);
        fwrite(guid, 1, 16, f);
    }

    uint32_t pcm_bytes_le = htole32(stream || rf64 ? UINT32_MAX : (uint32_t)total_pcm_bytes);
    fwrite("data", 1, 4, f);
    fwrite(&pcm_bytes_le, 4, 1, f);
    return !ferror(f);
}

static bool pcm_writer_open(PCMWriter *w, const char *outfile, bool write_stdout, bool raw_format,
                            uint32_t bit_depth, bool is_float, bool is_mp4, bool gapless,
                            uint32_t delay, uint32_t padding)
{
    memset(w, 0, sizeof(*w));
    w->write_stdout = write_stdout;
    w->raw_format = raw_format;
    w->bit_depth = bit_depth;
    w->is_float = is_float;
    w->is_mp4 = is_mp4;
    w->gapless = gapless;
    w->samples_to_skip = (is_mp4 && gapless) ? delay : 0;
    w->padding_samples = (is_mp4 && gapless) ? padding : 0;
    w->header_pending = !raw_format;

    if (write_stdout) {
        if (!cli_binary_stream(stdout)) return false;
        w->fout = stdout;
    } else if (outfile) {
        w->fout = cli_fopen(outfile, "wb");
        if (!w->fout) return false;

#ifdef _WIN32
        struct _stat64 statbuf;
        w->trim_file = is_mp4 && _fstat64(_fileno(w->fout), &statbuf) == 0 &&
                       (statbuf.st_mode & _S_IFMT) == _S_IFREG;
#else
        struct stat statbuf;
        w->trim_file = is_mp4 && fstat(fileno(w->fout), &statbuf) == 0 && S_ISREG(statbuf.st_mode);
#endif

        w->io_buf = (char *)malloc(PCM_IO_BUFFER_SIZE);
        if (w->io_buf) {
            if (setvbuf(w->fout, w->io_buf, _IOFBF, PCM_IO_BUFFER_SIZE) != 0) {
                free(w->io_buf);
                w->io_buf = NULL;
            }
        }
    }
    return true;
}

static void update_stream_info(faad_decoder *dec, faad_stream_info *sinfo, uint32_t flags,
                               uint32_t *sample_rate, uint32_t *num_channels,
                               enum faad_object_type *obj_type)
{
    if (flags & FAAD_FRAME_FORMAT_CHANGED) {
        faad_decoder_get_info(dec, sinfo);
        *sample_rate = sinfo->sample_rate;
        *num_channels = sinfo->channels;
    }
    *obj_type = (flags & FAAD_FRAME_PS) ? FAAD_OBJ_HE_AAC_V2 :
                (flags & FAAD_FRAME_SBR) ? FAAD_OBJ_HE_AAC_V1 : FAAD_OBJ_LC;
}

static const char *object_type_name(enum faad_object_type obj_type)
{
    switch (obj_type) {
    case FAAD_OBJ_HE_AAC_V1: return "HE-AAC v1 (AAC-LC + SBR)";
    case FAAD_OBJ_HE_AAC_V2: return "HE-AAC v2 (AAC-LC + SBR + PS)";
    case FAAD_OBJ_LC:        return "AAC-LC";
    default:                 return "Unknown";
    }
}

static bool pcm_writer_write_frame(PCMWriter *w, faad_decoder *dec, faad_stream_info *sinfo,
                                   const MP4Track *track, uint32_t flags, uint8_t *outbuf,
                                   uint32_t bytes_written, uint32_t *sample_rate,
                                   uint32_t *num_channels, enum faad_object_type *obj_type)
{
    uint32_t previous_rate = *sample_rate;
    uint32_t previous_channels = *num_channels;
    update_stream_info(dec, sinfo, flags, sample_rate, num_channels, obj_type);
    if (flags & FAAD_FRAME_FORMAT_CHANGED) {
        if (w->gapless_scaled && (sinfo->channels != previous_channels ||
                                  sinfo->sample_rate != previous_rate)) {
            if (w->is_mp4 && !w->trim_file && !w->fallback_mode) {
                w->fallback_mode = true;
                if (!w->fifo_initialized) {
                    if (!fifo_init(&w->fifo, 262144)) return false;
                    w->fifo_initialized = true;
                } else if (w->fifo.size < 262144) {
                    uint8_t *new_data = (uint8_t *)malloc(262144);
                    if (!new_data) return false;
                    if (w->fifo.fill > 0) {
                        if (w->fifo.tail < w->fifo.head) {
                            memcpy(new_data, w->fifo.data + w->fifo.tail, w->fifo.fill);
                        } else {
                            uint32_t first = w->fifo.size - w->fifo.tail;
                            memcpy(new_data, w->fifo.data + w->fifo.tail, first);
                            memcpy(new_data + first, w->fifo.data, w->fifo.head);
                        }
                    }
                    free(w->fifo.data);
                    w->fifo.data = new_data;
                    w->fifo.size = 262144;
                    w->fifo.tail = 0;
                    w->fifo.head = w->fifo.fill;
                }
            }
        }
    }

    if (!w->raw_format && !w->header_pending && w->fout &&
        (*sample_rate != w->header_sample_rate || *num_channels != w->header_channels)) {
        fprintf(stderr, "WAV output cannot change sample rate or channel count\n");
        return false;
    }

    if (w->header_pending && w->fout) {
        if (!write_wav_header(w->fout, *sample_rate, (uint16_t)*num_channels, 0, w->bit_depth, w->is_float, sinfo->channel_mask)) return false;
        w->header_channels = (uint16_t)*num_channels;
        w->header_channel_mask = sinfo->channel_mask;
        w->header_sample_rate = *sample_rate;
        w->header_pending = false;
        if (w->trim_file) w->data_offset = cli_ftell(w->fout);
    }

    uint32_t dec_bytes_per_sample = w->is_float ? 4 : w->bit_depth / 8;
    uint32_t dec_bytes_per_frame_sample = *num_channels * dec_bytes_per_sample;
    pcm_to_little_endian(outbuf, bytes_written, dec_bytes_per_sample, !w->raw_format && !w->is_float && w->bit_depth == 32);
    if (!w->is_mp4) {
        fwrite(outbuf, 1, bytes_written, w->fout);
        w->total_pcm_bytes += bytes_written;
        return true;
    }
    uint32_t frame_samples = bytes_written / dec_bytes_per_frame_sample;

    if (!w->gapless_scaled) {
        uint32_t core_rate = (flags & FAAD_FRAME_SBR) && sinfo->frame_samples == 2048
            ? sinfo->sample_rate / 2 : sinfo->sample_rate;
        uint32_t timescale = track->timescale ? track->timescale : core_rate;
        uint32_t delay = w->gapless ? sinfo->decoder_delay : 0;
        w->samples_to_skip = (uint32_t)((uint64_t)w->samples_to_skip * sinfo->sample_rate / timescale) + delay;
        w->padding_samples = (uint32_t)((uint64_t)w->padding_samples * sinfo->sample_rate / timescale);
        w->padding_samples = w->padding_samples > delay ? w->padding_samples - delay : 0;
        w->gapless_scaled = true;

        uint64_t pad_bytes_64 = (uint64_t)w->padding_samples * 8 * dec_bytes_per_sample;
        if (pad_bytes_64 > 64 * 1024 * 1024) return false;
        w->padding_bytes = (uint32_t)pad_bytes_64;

        if (w->padding_bytes > 0 && !w->trim_file && !w->fallback_mode) {
            if (!fifo_init(&w->fifo, w->padding_bytes)) return false;
            w->fifo_initialized = true;
        }
    }

    uint8_t *write_ptr = outbuf;
    uint32_t samples_to_write = frame_samples;

    if (w->samples_to_skip > 0) {
        if (w->samples_to_skip >= samples_to_write) {
            w->samples_to_skip -= samples_to_write;
            samples_to_write = 0;
        } else {
            write_ptr += w->samples_to_skip * dec_bytes_per_frame_sample;
            samples_to_write -= w->samples_to_skip;
            w->samples_to_skip = 0;
        }
    }

    if (w->fout && samples_to_write > 0) {
        uint32_t frame_bytes = samples_to_write * dec_bytes_per_frame_sample;

        if (w->fallback_mode) {
            w->padding_bytes = w->padding_samples * *num_channels * (w->bit_depth / 8);
        }

        if (w->trim_file || (w->padding_bytes == 0 && !w->fallback_mode)) {
            fwrite(write_ptr, 1, frame_bytes, w->fout);
            w->total_pcm_bytes += frame_bytes;
        } else if (!w->fallback_mode) {
            uint32_t total_avail = w->fifo.fill + frame_bytes;
            if (total_avail > w->padding_bytes) {
                uint32_t safe_bytes = total_avail - w->padding_bytes;
                if (w->fifo.fill > 0) {
                    uint32_t from_ring = (w->fifo.fill < safe_bytes) ? w->fifo.fill : safe_bytes;
                    w->total_pcm_bytes += fifo_write(w->fout, &w->fifo, from_ring);
                    safe_bytes -= from_ring;
                }
                if (safe_bytes > 0) {
                    fwrite(write_ptr, 1, safe_bytes, w->fout);
                    w->total_pcm_bytes += safe_bytes;
                    write_ptr += safe_bytes;
                    frame_bytes -= safe_bytes;
                }
            }
            if (frame_bytes > 0) {
                if (!fifo_push(&w->fifo, write_ptr, frame_bytes)) return false;
            }
        } else {
            if (!w->write_stdout && frame_bytes > w->fifo.size - w->fifo.fill &&
                w->fifo.fill > w->padding_bytes) {
                w->total_pcm_bytes += fifo_write(w->fout, &w->fifo, w->fifo.fill - w->padding_bytes);
            }
            if (!fifo_push(&w->fifo, write_ptr, frame_bytes)) return false;

            if (w->fifo.fill > w->padding_bytes) {
                uint32_t can_pop = w->fifo.fill - w->padding_bytes;
                if (w->write_stdout || can_pop >= 64u * 1024u) {
                    w->total_pcm_bytes += fifo_write(w->fout, &w->fifo, can_pop);
                }
            }
        }
    }
    return true;
}

static bool pcm_writer_finish(PCMWriter *w, uint32_t sample_rate, uint32_t num_channels)
{
    if (w->fout && ferror(w->fout)) return false;
    if (w->fout && w->trim_file && w->gapless && w->padding_samples > 0) {
        if (fflush(w->fout) != 0) return false;
        uint64_t end = cli_ftell(w->fout);
        if (end == UINT64_MAX || end < w->data_offset) return false;
        uint64_t audio_bytes = end - w->data_offset;
        uint64_t padding = (uint64_t)w->padding_samples * num_channels * (w->bit_depth / 8);
        if (padding > audio_bytes) padding = audio_bytes;
#ifdef _WIN32
        if (_chsize_s(_fileno(w->fout), end - padding) != 0) return false;
#else
        if (ftruncate(fileno(w->fout), (off_t)(end - padding)) != 0) return false;
#endif
        w->total_pcm_bytes = audio_bytes - padding;
    } else if (w->fout && w->is_mp4) {
        if (w->gapless && w->padding_samples > 0) {
            uint32_t final_padding_bytes = w->padding_samples * num_channels * (w->bit_depth / 8);
            if (w->fifo.fill > final_padding_bytes) {
                fifo_truncate_tail(&w->fifo, final_padding_bytes);
            } else {
                w->fifo.fill = 0;
            }
        }
        w->total_pcm_bytes += fifo_write(w->fout, &w->fifo, w->fifo.fill);
    }
    if (w->fout && w->fout != stdout) {
        if (!w->raw_format) {
            if (!w->header_pending) num_channels = w->header_channels;
            if (w->total_pcm_bytes & 1) {
                if (!cli_fseek(w->fout, (num_channels > 2 ? 104 : 80) + w->total_pcm_bytes) ||
                    fputc(0, w->fout) == EOF) return false;
            }
            if (!write_wav_header(w->fout, sample_rate, (uint16_t)num_channels,
                                  w->total_pcm_bytes, w->bit_depth, w->is_float, w->header_channel_mask)) return false;
        }
        if (!cli_finish_output(&w->fout)) return false;
    } else if (w->fout && !cli_finish_output(&w->fout)) {
        return false;
    }
    if (w->io_buf) {
        free(w->io_buf);
        w->io_buf = NULL;
    }
    fifo_free(&w->fifo);
    return true;
}

static void pcm_writer_close(PCMWriter *w)
{
    if (w->fout && w->fout != stdout) {
        fclose(w->fout);
        w->fout = NULL;
    }
    if (w->io_buf) {
        free(w->io_buf);
        w->io_buf = NULL;
    }
    fifo_free(&w->fifo);
}

static void print_strict_error(const char *filename, uint64_t offset, uint64_t frame_idx, faad_status st)
{
    fprintf(stderr, "%s:0x%04llx: frame %" PRIu64 ": error %d (%s)\n",
            filename ? filename : "input", (unsigned long long)offset, frame_idx, st, faad_strerror(st));
}

int run_decoding_session(const decode_options_t *opts,
                          progress_callback_t progress_cb,
                          void *user_data)
{
    decode_callbacks_t cbs;
    memset(&cbs, 0, sizeof(cbs));
    cbs.progress_cb = progress_cb;
    cbs.user_data = user_data;
    return run_decoding_session_ext(opts, &cbs);
}

int run_decoding_session_ext(const decode_options_t *opts,
                              const decode_callbacks_t *callbacks)
{
    if (!opts || !opts->input_filename)
        return DECODE_ERROR;

    progress_callback_t progress_cb = callbacks ? callbacks->progress_cb : NULL;
    decode_session_start_callback_t session_start_cb = callbacks ? callbacks->session_start_cb : NULL;
    decode_summary_callback_t summary_cb = callbacks ? callbacks->summary_cb : NULL;
    decode_log_message_callback_t log_cb = callbacks ? callbacks->log_cb : NULL;
    void *user_data = callbacks ? callbacks->user_data : NULL;

    const char *infile = opts->input_filename;
    const char *outfile = opts->output_filename;
    const char *adts_outfile = opts->adts_outfile;
    bool write_stdout = opts->write_stdout;
    bool raw_format = opts->raw_format;
    uint32_t bit_depth = opts->bit_depth;
    bool is_float = opts->is_float;
    enum faad_downmix_mode downmix = opts->downmix;
    uint8_t drc_cut = opts->drc_cut, drc_boost = opts->drc_boost, drc_target = opts->drc_target;
    bool gapless = opts->gapless;
    bool info_only = opts->info_only;
    uint32_t want_track = opts->want_track;
    bool json_info = opts->json_info;
    bool quiet = (opts->verbose == 0);
    bool strict_mode = opts->strict_mode;
    bool overwrite = opts->overwrite;
    double jump_seconds = opts->jump_seconds;

    FILE *fin = NULL;
    uint8_t *inbuf = NULL;
    MP4Track track;
    faad_decoder *dec = NULL;
    faad_status st = FAAD_OK;
    PCMWriter writer = {0};
    int ret_status = DECODE_SUCCESS;

    memset(&track, 0, sizeof(track));

    uint64_t file_len = 0;
    bool is_mp4 = false;
    size_t input_fill = 0;
    size_t input_pos = 0;
    bool input_eof = false;
    size_t input_capacity = INPUT_BUFFER_SIZE;
    bool memory_input = false;

    fin = !strcmp(infile, "-") ? stdin : cli_fopen(infile, "rb");
    if (!fin) {
        log_msgf(log_cb, user_data, 0, "Error opening input file %s\n", infile);
        return DECODE_ERROR;
    }
    if (fin == stdin && !cli_binary_stream(stdin)) {
        log_msgf(log_cb, user_data, 0, "Cannot set binary input mode\n");
        ret_status = DECODE_ERROR;
        goto cleanup;
    }
    if (fin == stdin && !outfile && !write_stdout && !info_only && !adts_outfile)
        write_stdout = true;
    if (fin != stdin) setvbuf(fin, NULL, _IOFBF, INPUT_BUFFER_SIZE);
    if (fin != stdin && cli_fsize(fin, &file_len) && file_len <= INPUT_MEMORY_LIMIT) {
        size_t length;
        if (!cli_read_all(fin, INPUT_MEMORY_LIMIT, &inbuf, &length)) {
            log_msgf(log_cb, user_data, 0, "Error reading input\n");
            ret_status = DECODE_ERROR;
            goto cleanup;
        }
        input_fill = length;
        file_len = length;
        input_capacity = length;
        input_eof = true;
        memory_input = true;
    } else {
        inbuf = malloc(INPUT_BUFFER_SIZE);
        if (!inbuf) { ret_status = DECODE_ERROR; goto cleanup; }
        setvbuf(fin, NULL, _IONBF, 0);
        input_fill = fread(inbuf, 1, 32, fin);
        if (ferror(fin)) { ret_status = DECODE_ERROR; goto cleanup; }
    }
    bool container = input_fill >= 8 && (!memcmp(inbuf + 4, "ftyp", 4) ||
                     !memcmp(inbuf + 4, "moov", 4) || !memcmp(inbuf + 4, "free", 4) ||
                     !memcmp(inbuf + 4, "mdat", 4) || !memcmp(inbuf + 4, "wide", 4) ||
                     !memcmp(inbuf + 4, "styp", 4) || !memcmp(inbuf + 4, "sidx", 4) ||
                     !memcmp(inbuf + 4, "uuid", 4) || !memcmp(inbuf + 4, "skip", 4));
    if (memory_input) container = is_mp4 = mp4_read_track_buf(inbuf, file_len, want_track, &track);
    if (container) {
        if (!memory_input) {
            if (fin == stdin || !cli_fsize(fin, &file_len)) {
                FILE *spool = tmpfile();
                if (!spool) { ret_status = DECODE_ERROR; goto cleanup; }
                bool ok = fwrite(inbuf, 1, input_fill, spool) == input_fill;
                size_t n;
                while (ok && (n = fread(inbuf, 1, INPUT_BUFFER_SIZE, fin)) != 0)
                    ok = fwrite(inbuf, 1, n, spool) == n;
                ok = ok && !ferror(fin) && fflush(spool) == 0;
                if (fin != stdin) fclose(fin);
                fin = spool;
                if (!ok || !cli_fsize(fin, &file_len)) { ret_status = DECODE_ERROR; goto cleanup; }
            }
            is_mp4 = mp4_read_track_file(fin, file_len, want_track, &track);
        }
        if (!is_mp4) {
            log_msgf(log_cb, user_data, 0, "Invalid MP4 input\n");
            ret_status = DECODE_ERROR;
            goto cleanup;
        }
    } else {
        file_len = input_fill;
    }

    if (is_mp4 && track.sample_error) {
        log_msgf(log_cb, user_data, 0, "%s: failed to collect complete MP4 sample table\n", infile);
        ret_status = DECODE_ERROR;
        goto cleanup;
    }

    if (is_mp4 && (!track.asc_buf || track.num_samples == 0)) {
        log_msgf(log_cb, user_data, 0, "%s: no supported AAC audio track found in MP4 file\n", infile);
        ret_status = DECODE_ERROR;
        goto cleanup;
    }

    if (adts_outfile && !is_mp4) {
        log_msgf(log_cb, user_data, 0, "%s: --adts needs an MP4 input\n", infile);
        ret_status = DECODE_ERROR;
        goto cleanup;
    }

    if (adts_outfile && is_mp4) {
        if (cli_same_file(infile, adts_outfile)) {
            log_msgf(log_cb, user_data, 0, "Input and output must be different files\n");
            ret_status = DECODE_ERROR; goto cleanup;
        }
        if (!overwrite && cli_file_exists(adts_outfile)) {
            log_msgf(log_cb, user_data, 0, "Output file %s already exists (use --overwrite)\n", adts_outfile);
            ret_status = DECODE_ERROR;
            goto cleanup;
        }
        AscInfo asc;
        adts_params adts_p;
        asc_codec_parse(track.asc_buf, track.asc_len, &asc);
        if (asc.frame_length_flag) {
            log_msgf(log_cb, user_data, 0, "%s: ADTS cannot represent 960-sample frames\n", infile);
            ret_status = DECODE_ERROR;
            goto cleanup;
        }
        if (adts_params_from_asc(&asc, true, &adts_p)) {
            log_msgf(log_cb, user_data, 0, "%s: core sample rate %u Hz has no ADTS sampling frequency index\n",
                    infile, (unsigned)asc.sample_rate);
            ret_status = DECODE_ERROR;
            goto cleanup;
        }
        /* A layout in a program config element has to go into the frames themselves. */
        uint8_t adts_pce[512];
        uint32_t adts_pce_len = adts_p.channel_cfg ? 0 : asc_pce_element(track.asc_buf, &asc.pce, adts_pce, sizeof(adts_pce));
        FILE *fadts = cli_fopen(adts_outfile, "wb");
        if (!fadts) {
            log_msgf(log_cb, user_data, 0, "Error opening ADTS output file %s\n", adts_outfile);
            ret_status = DECODE_ERROR;
            goto cleanup;
        }
        for (uint32_t s = 0; s < track.num_samples; s++) {
            uint64_t offset = track.samples[s].offset;
            uint32_t size = track.samples[s].size;
            if (size > ADTS_MAX_FRAME - ADTS_HEADER_SIZE - adts_pce_len) {
                log_msgf(log_cb, user_data, 0, "%s: sample %u (%u bytes) exceeds the ADTS payload limit\n",
                        infile, (unsigned)s, (unsigned)size);
                ret_status = DECODE_ERROR;
                break;
            }
            if (offset > 0 && offset <= file_len && size <= file_len - offset) {
                uint8_t adts_hdr[ADTS_HEADER_SIZE];
                uint32_t pce_bytes = 0;
                if (memory_input) {
                    pce_bytes = adts_pce_bytes(adts_pce_len, s, inbuf + offset, size);
                    adts_write_header(&adts_p, size + pce_bytes, adts_hdr);
                    fwrite(adts_hdr, 1, sizeof(adts_hdr), fadts);
                    if (pce_bytes) fwrite(adts_pce, 1, pce_bytes, fadts);
                    if (fwrite(inbuf + offset, 1, size, fadts) != size) { ret_status = DECODE_ERROR; break; }
                    continue;
                }
                if (!cli_fseek(fin, offset)) { ret_status = DECODE_ERROR; break; }
                uint32_t remaining = size;
                /* The first bytes tell whether the block already opens with a PCE. */
                size_t first = remaining < INPUT_BUFFER_SIZE ? remaining : INPUT_BUFFER_SIZE;
                if (fread(inbuf, 1, first, fin) != first) { ret_status = DECODE_ERROR; break; }
                pce_bytes = adts_pce_bytes(adts_pce_len, s, inbuf, (uint32_t)first);
                adts_write_header(&adts_p, size + pce_bytes, adts_hdr);
                fwrite(adts_hdr, 1, sizeof(adts_hdr), fadts);
                if (pce_bytes) fwrite(adts_pce, 1, pce_bytes, fadts);
                if (fwrite(inbuf, 1, first, fadts) != first) { ret_status = DECODE_ERROR; break; }
                remaining -= (uint32_t)first;
                while (remaining) {
                    size_t n = remaining < INPUT_BUFFER_SIZE ? remaining : INPUT_BUFFER_SIZE;
                    if (fread(inbuf, 1, n, fin) != n || fwrite(inbuf, 1, n, fadts) != n) {
                        ret_status = DECODE_ERROR; break;
                    }
                    remaining -= (uint32_t)n;
                }
                if (ret_status) break;
            }
        }
        if (!cli_finish_output(&fadts)) ret_status = DECODE_ERROR;
        if (ret_status) goto cleanup;
        if (!quiet && log_cb) log_msgf(log_cb, user_data, 1, "Extracted %u raw ADTS frames to %s\n", track.num_samples, adts_outfile);
        else if (!quiet) printf("Extracted %u raw ADTS frames to %s\n", track.num_samples, adts_outfile);
        ret_status = DECODE_SUCCESS;
        goto cleanup;
    }

    faad_config cfg;
    faad_config_init(&cfg, sizeof(cfg));
    cfg.stream_format = is_mp4 ? FAAD_STREAM_RAW : FAAD_STREAM_ADTS;
    cfg.output_format = is_float ? FAAD_OUTPUT_FLOAT : bit_depth == 32 ? FAAD_OUTPUT_24IN32 : bit_depth == 24 ? FAAD_OUTPUT_24BIT : FAAD_OUTPUT_16BIT;
    cfg.downmix_mode = downmix;
    cfg.drc_cut = drc_cut;
    cfg.drc_boost = drc_boost;
    cfg.drc_target_level = drc_target;

    st = faad_decoder_open(&cfg, is_mp4 ? track.asc_buf : NULL, is_mp4 ? track.asc_len : 0, &dec);
    if (st != FAAD_OK) {
        faad_library_info lib = { .struct_size = sizeof(lib) };
        AscInfo asc_info = { 0 };
        if (st == FAAD_ERR_UNSUPPORTED && is_mp4)
            asc_codec_parse(track.asc_buf, track.asc_len, &asc_info);
        if (asc_info.frame_length_flag && faad_get_library_info(&lib) == FAAD_OK &&
            !(lib.flags & FAAD_LIB_DAB))
            log_msgf(log_cb, user_data, 0, "Failed to open FAAD decoder: the stream uses 960-sample frames (Digital Audio Broadcasting), which this build lacks (needs -Ddecoder-dab=true)\n");
        else if (asc_info.num_channels && faad_get_library_info(&lib) == FAAD_OK &&
            asc_info.num_channels > lib.max_channels)
            log_msgf(log_cb, user_data, 0, "Failed to open FAAD decoder: the stream has %u channels, but this build decodes at most %u (rebuild with -Dmax-channels=%u)\n",
                     (unsigned)asc_info.num_channels, lib.max_channels, (unsigned)asc_info.num_channels);
        else
            log_msgf(log_cb, user_data, 0, "Failed to open FAAD decoder: %s\n", faad_strerror(st));
        ret_status = DECODE_ERROR;
        goto cleanup;
    }

    if (!info_only) {
        if (!write_stdout && !outfile) {
            log_msgf(log_cb, user_data, 0, "No output filename specified\n");
            ret_status = DECODE_ERROR;
            goto cleanup;
        }
        if (!write_stdout && cli_same_file(infile, outfile)) {
            log_msgf(log_cb, user_data, 0, "Input and output must be different files\n");
            ret_status = DECODE_ERROR; goto cleanup;
        }
        if (!write_stdout && !overwrite && cli_file_exists(outfile)) {
            log_msgf(log_cb, user_data, 0, "Output file %s already exists (use --overwrite)\n", outfile);
            ret_status = DECODE_ERROR;
            goto cleanup;
        }
        if (!pcm_writer_open(&writer, outfile, write_stdout, raw_format, bit_depth, is_float, is_mp4, gapless, track.delay, track.padding)) {
            log_msgf(log_cb, user_data, 0, "Error opening output file %s\n", outfile ? outfile : "stdout");
            ret_status = DECODE_ERROR;
            goto cleanup;
        }
        if (write_stdout) quiet = true;
    }

    _Alignas(float) uint8_t outbuf[65536];
    uint32_t sample_rate = 44100;
    uint32_t num_channels = 2;
    enum faad_object_type obj_type = FAAD_OBJ_LC;
    uint64_t frames_decoded = 0;
    double duration_sec = 0.0;
    faad_stream_info sinfo = { .struct_size = sizeof(sinfo) };

    uint32_t start_frame = 0;
    if (is_mp4 && jump_seconds > 0.0) {
        faad_stream_info sinfo_jump = { .struct_size = sizeof(faad_stream_info) };
        uint32_t sr = 44100;
        uint32_t fl = 1024;
        if (faad_decoder_get_info(dec, &sinfo_jump) == FAAD_OK) {
            if (sinfo_jump.sample_rate > 0) sr = sinfo_jump.sample_rate;
            if (sinfo_jump.frame_samples > 0) fl = sinfo_jump.frame_samples;
        }
        start_frame = (uint32_t)((jump_seconds * (double)sr) / (double)fl);
    }

    uint64_t total_track_samples = is_mp4 ? track.num_samples * 1024 : 0;
    uint32_t total_track_frames = is_mp4 ? track.num_samples : 0;

    faad_stream_info init_sinfo = { .struct_size = sizeof(init_sinfo) };
    if (faad_decoder_get_info(dec, &init_sinfo) == FAAD_OK) {
        if (init_sinfo.sample_rate > 0) sample_rate = init_sinfo.sample_rate;
        if (init_sinfo.channels > 0) num_channels = init_sinfo.channels;
        obj_type = init_sinfo.object_type;
    }

    /* Without the layer the decoder plays the core alone, at the wrong rate or channel count. */
    faad_library_info dec_lib = { .struct_size = sizeof(dec_lib) };
    if (faad_get_library_info(&dec_lib) == FAAD_OK) {
        if ((obj_type == FAAD_OBJ_HE_AAC_V1 || obj_type == FAAD_OBJ_HE_AAC_V2) && !(dec_lib.flags & FAAD_LIB_SBR))
            log_msgf(log_cb, user_data, 0, "Warning: this stream uses SBR, which this build lacks (needs -Ddecoder-sbr=true); decoding the core only, at half the sample rate\n");
        else if (obj_type == FAAD_OBJ_HE_AAC_V2 && !(dec_lib.flags & FAAD_LIB_PS))
            log_msgf(log_cb, user_data, 0, "Warning: this stream uses Parametric Stereo, which this build lacks (needs -Ddecoder-ps=true); decoding mono\n");
        if ((drc_cut || drc_boost || drc_target) && !(dec_lib.flags & FAAD_LIB_DRC))
            log_msgf(log_cb, user_data, 0, "Warning: the DRC options have no effect, this build lacks dynamic range control (needs -Ddecoder-drc=true)\n");
    }

    double est_duration_sec = 0.0;
    if (is_mp4 && total_track_samples > 0 && sample_rate > 0) {
        est_duration_sec = (double)total_track_samples / (double)sample_rate;
    }

    double est_avg_bitrate_kbps = (file_len * 8.0) / (est_duration_sec > 0.0 ? est_duration_sec * 1000.0 : 1.0);

    uint16_t out_channels = (downmix == FAAD_DOWNMIX_MONO) ? 1 :
                            (downmix == FAAD_DOWNMIX_STEREO && num_channels > 2) ? 2 : num_channels;

    if (session_start_cb) {
        decode_session_info_t sess_info = {
            .input_filename = infile,
            .output_filename = outfile,
            .sample_rate = sample_rate,
            .num_channels = out_channels,
            .total_track_samples = total_track_samples,
            .total_track_frames = total_track_frames,
            .duration_sec = est_duration_sec,
            .avg_bitrate_kbps = est_avg_bitrate_kbps,
            .is_mp4 = is_mp4,
            .raw_format = raw_format,
            .bit_depth = bit_depth,
            .is_float = is_float,
            .object_type = obj_type,
            .downmix = downmix,
            .profile_name = object_type_name(obj_type)
        };
        session_start_cb(&sess_info, user_data);
    }

    double start_time = get_wall_time_sec();
    uint64_t current_decoded_samples = 0;
    uint32_t max_frame_bytes = 0;

    if (is_mp4) {
        uint64_t cache_offset = UINT64_MAX;
        size_t cache_bytes = 0;
        for (uint32_t s = 0; s < track.num_samples; s++) {
            uint64_t offset = track.samples[s].offset;
            uint32_t size = track.samples[s].size;
            if (offset == 0 || offset > file_len || size > file_len - offset) continue;

            uint32_t bytes_consumed = 0;
            uint32_t bytes_written = 0;

            uint32_t flags;
            const uint8_t *frame = inbuf + (memory_input ? offset : 0);
            if (!memory_input) {
                if (size > UINT32_MAX / 8) { ret_status = DECODE_ERROR; goto cleanup; }
                if (offset < cache_offset || offset - cache_offset > cache_bytes ||
                    size > cache_bytes - (size_t)(offset - cache_offset)) {
                    if (size > input_capacity) {
                        uint8_t *larger = realloc(inbuf, size);
                        if (!larger) { ret_status = DECODE_ERROR; goto cleanup; }
                        inbuf = larger;
                        input_capacity = size;
                    }
                    if (cli_ftell(fin) != offset && !cli_fseek(fin, offset)) { ret_status = DECODE_ERROR; goto cleanup; }
                    cache_bytes = file_len - offset < input_capacity ? (size_t)(file_len - offset) : input_capacity;
                    if (fread(inbuf, 1, cache_bytes, fin) != cache_bytes) { ret_status = DECODE_ERROR; goto cleanup; }
                    cache_offset = offset;
                }
                frame = inbuf + (size_t)(offset - cache_offset);
            }
            st = faad_decode_frame(dec, frame, size,
                                   &bytes_consumed, outbuf, sizeof(outbuf), &bytes_written, &flags);

            if (st == FAAD_OK && strict_mode && (flags & (FAAD_FRAME_CONCEALED | FAAD_FRAME_DEGRADED)))
                st = FAAD_ERR_DECODE_FAILED;
            if (st != FAAD_OK) {
                if (strict_mode) {
                    print_strict_error(infile, offset, s, st);
                    ret_status = DECODE_ERROR;
                    goto cleanup;
                }
            } else if (bytes_written > 0) {
                if (s < start_frame) {
                    update_stream_info(dec, &sinfo, flags, &sample_rate, &num_channels, &obj_type);
                    continue;
                }
                if (info_only) {
                    update_stream_info(dec, &sinfo, flags, &sample_rate, &num_channels, &obj_type);
                } else {
                    if (!pcm_writer_write_frame(&writer, dec, &sinfo, &track, flags, outbuf, bytes_written,
                                                &sample_rate, &num_channels, &obj_type)) {
                        log_msgf(log_cb, user_data, 0, "Error processing frame or allocating memory\n");
                        ret_status = DECODE_ERROR;
                        goto cleanup;
                    }
                }
                if (size > max_frame_bytes) max_frame_bytes = size;
                uint32_t frame_samples = bytes_written / ((is_float ? 4 : bit_depth / 8) * num_channels);
                duration_sec += (double)frame_samples / sample_rate;
                current_decoded_samples += frame_samples;
                frames_decoded++;

                if (progress_cb && !quiet && !info_only) {
                    double time_used = get_wall_time_sec() - start_time;
                    progress_info_t prog = build_progress_info(current_decoded_samples, total_track_samples,
                                                                 sample_rate, num_channels, (uint32_t)frames_decoded,
                                                                 total_track_frames, writer.total_pcm_bytes, time_used,
                                                                 false);
                    if (!progress_cb(&prog, user_data)) {
                        ret_status = DECODE_CANCELLED;
                        goto cleanup;
                    }
                }
            }
        }
    } else {
        uint64_t offset = 0;
        bool need_input = false;
        while (true) {
            if (!input_eof && (input_pos == input_fill || need_input)) {
                memmove(inbuf, inbuf + input_pos, input_fill - input_pos);
                input_fill -= input_pos;
                input_pos = 0;
                int n;
                do {
#ifdef _WIN32
                    n = _read(_fileno(fin), inbuf + input_fill, INPUT_BUFFER_SIZE - input_fill);
#else
                    n = (int)read(fileno(fin), inbuf + input_fill, INPUT_BUFFER_SIZE - input_fill);
#endif
                } while (n < 0 && errno == EINTR);
                if (n < 0) { log_msgf(log_cb, user_data, 0, "Error reading input\n"); ret_status = DECODE_ERROR; goto cleanup; }
                need_input = false;
                input_fill += n;
                file_len += n;
                input_eof = n == 0;
            }
            if (input_pos == input_fill) break;
            uint32_t bytes_consumed = 0;
            uint32_t bytes_written = 0;

            uint32_t flags;
            st = faad_decode_frame(dec, inbuf + input_pos, (uint32_t)(input_fill - input_pos),
                                   &bytes_consumed, outbuf, sizeof(outbuf), &bytes_written, &flags);

            if (st == FAAD_ERR_NEED_MORE_DATA && !input_eof) {
                need_input = true;
                continue;
            }

            if (st == FAAD_OK && strict_mode && (flags & (FAAD_FRAME_CONCEALED | FAAD_FRAME_DEGRADED)))
                st = FAAD_ERR_DECODE_FAILED;
            if (st != FAAD_OK) {
                if (strict_mode) {
                    print_strict_error(infile, offset, frames_decoded, st);
                    ret_status = DECODE_ERROR;
                    goto cleanup;
                }
                if (st == FAAD_ERR_NEED_MORE_DATA || bytes_consumed == 0) {
                    break;
                }
                input_pos += bytes_consumed;
                offset += bytes_consumed;
                continue;
            }

            if (!bytes_written) {
                input_pos += bytes_consumed;
                offset += bytes_consumed;
                continue;
            }
            update_stream_info(dec, &sinfo, flags, &sample_rate, &num_channels, &obj_type);
            if (bytes_consumed > max_frame_bytes) max_frame_bytes = bytes_consumed;
            uint32_t frame_samples = bytes_written / ((is_float ? 4 : bit_depth / 8) * num_channels);
            double frame_duration = (double)frame_samples / sample_rate;
            if (!info_only && duration_sec + frame_duration > jump_seconds) {
                if (!pcm_writer_write_frame(&writer, dec, &sinfo, &track, flags, outbuf, bytes_written,
                                            &sample_rate, &num_channels, &obj_type)) {
                    log_msgf(log_cb, user_data, 0, "Error processing frame or allocating memory\n");
                    ret_status = DECODE_ERROR;
                    goto cleanup;
                }
            }
            duration_sec += frame_duration;
            current_decoded_samples += frame_samples;
            frames_decoded++;
            input_pos += bytes_consumed;
            offset += bytes_consumed;

            if (progress_cb && !quiet && !info_only) {
                double time_used = get_wall_time_sec() - start_time;
                progress_info_t prog = build_progress_info(current_decoded_samples, total_track_samples,
                                                             sample_rate, num_channels, (uint32_t)frames_decoded,
                                                             total_track_frames, writer.total_pcm_bytes, time_used,
                                                             false);
                if (!progress_cb(&prog, user_data)) {
                    ret_status = DECODE_CANCELLED;
                    goto cleanup;
                }
            }
        }
    }

    if (!info_only) {
        if (!pcm_writer_finish(&writer, sample_rate, num_channels)) {
            log_msgf(log_cb, user_data, 0, "Error finalizing PCM output\n");
            ret_status = DECODE_ERROR;
            goto cleanup;
        }
    }

    double total_time_used = get_wall_time_sec() - start_time;
    if (progress_cb && !quiet && !info_only) {
        progress_info_t prog = build_progress_info(current_decoded_samples, total_track_samples,
                                                     sample_rate, num_channels, (uint32_t)frames_decoded,
                                                     total_track_frames, writer.total_pcm_bytes, total_time_used,
                                                     true);
        progress_cb(&prog, user_data);
    }

    double avg_bitrate_kbps = (file_len * 8.0) / (duration_sec > 0 ? duration_sec * 1000.0 : 1.0);
    uint32_t max_kbps = (uint32_t)(((uint64_t)max_frame_bytes * 8 * sample_rate / (1024 * 1000.0)) + 0.5);
    const char *brand = track.major_brand[0] ? track.major_brand : "M4A";

    if (json_info) {
        printf("{\n");
        printf("  \"file\": ");
        json_str(infile);
        printf(",\n  \"container\": \"%s\",\n", is_mp4 ? "MP4 / M4A" : "ADTS Bitstream");
        if (is_mp4) {
            printf("  \"major_brand\": ");
            json_str(brand);
            printf(",\n");
        }
        printf("  \"duration_seconds\": %.2f,\n", duration_sec);
        printf("  \"audio\": {\n");
        printf("    \"profile\": \"%s\",\n", object_type_name(obj_type));
        printf("    \"channels\": %u,\n", num_channels);
        printf("    \"sample_rate_hz\": %u,\n", sample_rate);
        printf("    \"bitrate_avg_kbps\": %.1f,\n", avg_bitrate_kbps);
        printf("    \"total_frames\": %" PRIu64 "\n", frames_decoded);
        printf("  },\n");
        printf("  \"gapless\": { \"encoder_delay\": %u, \"trailing_padding\": %u },\n", track.delay, track.padding);
        printf("  \"cover_art_bytes\": %u,\n", track.cover_bytes);
        printf("  \"metadata\": {");
        for (uint32_t i = 0; i < track.num_tags; i++) {
            printf(i ? ",\n    " : "\n    ");
            json_str(track.tags[i].name);
            printf(": ");
            json_str(track.tags[i].value);
        }
        printf("%s}\n}\n", track.num_tags ? "\n  " : "");
    } else if (info_only) {
        printf("File:        %s\n", infile);
        printf("Container:   %s%s%s\n", is_mp4 ? "MP4 / M4A (Major Brand: " : "ADTS Bitstream", is_mp4 ? brand : "", is_mp4 ? ")" : "");
        printf("Duration:    %02d:%02d:%02d.%02d (%.2f seconds)\n\n",
               (int)duration_sec / 3600, ((int)duration_sec % 3600) / 60, (int)duration_sec % 60, (int)(duration_sec * 100) % 100, duration_sec);
        if (is_mp4 && track.num_tracks > 1) {
            printf("Tracks:      %u (decoding track %u)\n", track.num_tracks, track.track_id);
            for (uint32_t i = 0; i < track.num_listed; i++) {
                const MP4TrackSummary *t = &track.summaries[i];
                char cc[5] = { (char)(t->fourcc >> 24), (char)(t->fourcc >> 16), (char)(t->fourcc >> 8), (char)t->fourcc, 0 };
                for (int k = 0; k < 4; k++) if ((unsigned char)cc[k] < 0x20 || (unsigned char)cc[k] > 0x7e) cc[k] = '?';
                printf("  Track %u: %s %s%s\n", t->id, t->kind == 'a' ? "audio" : t->kind == 'v' ? "video" : "other",
                       t->fourcc ? cc : "-", t->decodable ? " (AAC, decodable)" : "");
            }
            if (track.num_tracks > track.num_listed) printf("  and %u more\n", track.num_tracks - track.num_listed);
            printf("\n");
        }
        printf("Audio Stream:\n");
        printf("  Profile:   %s\n", object_type_name(obj_type));
        printf("  Channels:  %u (%s)\n", num_channels, (num_channels == 1) ? "Mono" : ((num_channels == 2) ? "Stereo" : "Multichannel"));
        printf("  Sample Rate: %.1f kHz\n", sample_rate / 1000.0f);
        printf("  Bitrate:   %.1f kbps (Avg)\n", avg_bitrate_kbps);
        printf("  Frames:    %" PRIu64 " frames\n", frames_decoded);
        if (track.delay || track.padding) {
            printf("\nGapless:\n");
            printf("  Encoder Delay:    %u samples\n", track.delay);
            printf("  Trailing Padding: %u samples\n", track.padding);
        }
        if (track.num_tags || track.cover_bytes) {
            printf("\nMetadata (Tags):\n");
            for (uint32_t i = 0; i < track.num_tags; i++)
                printf("  %s: %s\n", track.tags[i].name, track.tags[i].value);
            if (track.cover_bytes) printf("  Cover Art: present (%u bytes)\n", track.cover_bytes);
        }
    } else {
        if (summary_cb) {
            decode_summary_t summary = {
                .frame_count = frames_decoded,
                .sample_count = current_decoded_samples,
                .total_pcm_bytes = writer.total_pcm_bytes,
                .max_bitrate_kbps = max_kbps,
                .max_frame_bytes = max_frame_bytes,
                .duration_sec = duration_sec,
                .time_elapsed_sec = total_time_used,
                .speed_factor = calc_speed(current_decoded_samples, sample_rate, total_time_used),
                .avg_bitrate_kbps = avg_bitrate_kbps,
                .num_channels = (uint16_t)num_channels,
                .sample_rate = sample_rate,
                .bit_depth = bit_depth,
                .is_float = is_float,
                .is_mp4 = is_mp4
            };
            summary_cb(&summary, user_data);
        }
    }

cleanup:
    if (fin && fin != stdin) fclose(fin);
    pcm_writer_close(&writer);
    if (dec) faad_decoder_close(&dec);
    if (inbuf) free(inbuf);
    if (is_mp4) mp4_free_track(&track);

    return ret_status;
}
