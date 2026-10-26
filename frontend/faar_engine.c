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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <faar.h>
#include "faar_engine.h"
#include "input.h"
#include "progress.h"

static double wall_seconds(void) {
    struct timespec ts;
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC)
        return 0.0;
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void render_progress(progress_throttle_t *throttle, double start,
                            uint64_t input_frames, uint64_t total_frames,
                            uint32_t in_rate, uint16_t channels,
                            uint64_t bytes_written, bool final) {
    double elapsed = wall_seconds() - start;
    progress_info_t info = {0};
    info.current_input_samples = input_frames * channels;
    info.total_input_samples = total_frames * channels;
    info.sample_rate = in_rate;
    info.num_channels = channels;
    info.current_frame = input_frames > UINT32_MAX ? UINT32_MAX : (uint32_t)input_frames;
    info.total_frames = total_frames > UINT32_MAX ? 0 : (uint32_t)total_frames;
    info.total_bytes_written = bytes_written;
    info.time_elapsed_sec = elapsed;
    info.speed_factor = elapsed > 0.0 ? ((double)input_frames / in_rate) / elapsed : 0.0;
    info.eta_sec = total_frames > input_frames && info.speed_factor > 0.0
        ? ((double)(total_frames - input_frames) / in_rate) / info.speed_factor : 0.0;
    info.is_final = final;
    if (final || progress_throttle_tick(throttle, &info, 0.25))
        progress_render_cli(&info);
}

static int write_output_frames(FILE *fout, const float *samples, uint8_t *raw_out_buf,
                               size_t frames, uint16_t channels, faar_pcm_format_t fmt,
                               size_t bytes_per_sample, uint64_t *total_bytes) {
    size_t sample_count = frames * channels;

    /* Convert float output PCM ([-32768, +32768]) to the requested format. */
    for (size_t i = 0; i < sample_count; i++) {
        float val = samples[i];

        if (fmt == FAAR_PCM_F32LE) {
            float norm_val = val / 32768.0f;
            if (norm_val > 1.0f) norm_val = 1.0f;
            if (norm_val < -1.0f) norm_val = -1.0f;
            uint32_t bits;
            memcpy(&bits, &norm_val, sizeof(bits));
            raw_out_buf[4 * i + 0] = (uint8_t)bits;
            raw_out_buf[4 * i + 1] = (uint8_t)(bits >> 8);
            raw_out_buf[4 * i + 2] = (uint8_t)(bits >> 16);
            raw_out_buf[4 * i + 3] = (uint8_t)(bits >> 24);
        } else if (fmt == FAAR_PCM_S16LE) {
            int32_t v = (int32_t)(val + (val >= 0.0f ? 0.5f : -0.5f));
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            uint16_t bits = (uint16_t)(int16_t)v;
            raw_out_buf[2 * i + 0] = (uint8_t)bits;
            raw_out_buf[2 * i + 1] = (uint8_t)(bits >> 8);
        } else if (fmt == FAAR_PCM_S24LE) {
            int32_t v = (int32_t)(val * 256.0f + (val >= 0.0f ? 0.5f : -0.5f));
            if (v > 8388607) v = 8388607;
            if (v < -8388608) v = -8388608;
            uint32_t bits = (uint32_t)v;
            raw_out_buf[i * 3 + 0] = (uint8_t)bits;
            raw_out_buf[i * 3 + 1] = (uint8_t)(bits >> 8);
            raw_out_buf[i * 3 + 2] = (uint8_t)(bits >> 16);
        }
    }

    size_t bytes_to_write = frames * channels * bytes_per_sample;
    if (bytes_to_write && fwrite(raw_out_buf, 1, bytes_to_write, fout) != bytes_to_write) {
        return -1;
    }
    *total_bytes += bytes_to_write;
    return 0;
}


static int write_wav_header(FILE *f, uint32_t rate, uint16_t channels, faar_pcm_format_t fmt, uint64_t data_size) {
    uint16_t bits = (fmt == FAAR_PCM_F32LE) ? 32 : (fmt == FAAR_PCM_S24LE ? 24 : 16);
    uint16_t audio_fmt = (fmt == FAAR_PCM_F32LE) ? 3 : 1;
    uint32_t block_align = channels * (bits / 8);
    uint32_t byte_rate = rate * block_align;
    bool is_rf64 = data_size >= (0xFFFFFFFFULL - 72);

    if (is_rf64) {
        uint8_t hdr[80] = {0};
        uint64_t riff_size = data_size + 72;

        memcpy(hdr, "RF64", 4);
        memset(hdr + 4, 0xFF, 4);
        memcpy(hdr + 8, "WAVE", 4);

        memcpy(hdr + 12, "ds64", 4);
        hdr[16] = 28; /* ds64 chunk size */
        hdr[20] = riff_size & 0xFF; hdr[21] = (riff_size >> 8) & 0xFF;
        hdr[22] = (riff_size >> 16) & 0xFF; hdr[23] = (riff_size >> 24) & 0xFF;
        hdr[24] = (riff_size >> 32) & 0xFF; hdr[25] = (riff_size >> 40) & 0xFF;
        hdr[26] = (riff_size >> 48) & 0xFF; hdr[27] = (riff_size >> 56) & 0xFF;

        hdr[28] = data_size & 0xFF; hdr[29] = (data_size >> 8) & 0xFF;
        hdr[30] = (data_size >> 16) & 0xFF; hdr[31] = (data_size >> 24) & 0xFF;
        hdr[32] = (data_size >> 32) & 0xFF; hdr[33] = (data_size >> 40) & 0xFF;
        hdr[34] = (data_size >> 48) & 0xFF; hdr[35] = (data_size >> 56) & 0xFF;

        uint64_t sample_count = data_size / block_align;
        hdr[36] = sample_count & 0xFF; hdr[37] = (sample_count >> 8) & 0xFF;
        hdr[38] = (sample_count >> 16) & 0xFF; hdr[39] = (sample_count >> 24) & 0xFF;
        hdr[40] = (sample_count >> 32) & 0xFF; hdr[41] = (sample_count >> 40) & 0xFF;
        hdr[42] = (sample_count >> 48) & 0xFF; hdr[43] = (sample_count >> 56) & 0xFF;

        memcpy(hdr + 48, "fmt ", 4);
        hdr[52] = 16;
        hdr[56] = audio_fmt & 0xFF; hdr[57] = (audio_fmt >> 8) & 0xFF;
        hdr[58] = channels & 0xFF; hdr[59] = (channels >> 8) & 0xFF;
        hdr[60] = rate & 0xFF; hdr[61] = (rate >> 8) & 0xFF;
        hdr[62] = (rate >> 16) & 0xFF; hdr[63] = (rate >> 24) & 0xFF;
        hdr[64] = byte_rate & 0xFF; hdr[65] = (byte_rate >> 8) & 0xFF;
        hdr[66] = (byte_rate >> 16) & 0xFF; hdr[67] = (byte_rate >> 24) & 0xFF;
        hdr[68] = block_align & 0xFF; hdr[69] = (block_align >> 8) & 0xFF;
        hdr[70] = bits & 0xFF; hdr[71] = (bits >> 8) & 0xFF;

        memcpy(hdr + 72, "data", 4);
        memset(hdr + 76, 0xFF, 4);

        return fwrite(hdr, 1, sizeof(hdr), f) == sizeof(hdr) ? 0 : -1;
    } else {
        /* Keep the same 80-byte reservation as RF64. JUNK is ignored by WAV
           readers and prevents large outputs from overwriting their first
           36 data bytes when the final header is installed. */
        uint8_t hdr[80] = {0};
        uint32_t riff_size = (uint32_t)(data_size + 72);

        memcpy(hdr, "RIFF", 4);
        hdr[4] = riff_size & 0xFF; hdr[5] = (riff_size >> 8) & 0xFF;
        hdr[6] = (riff_size >> 16) & 0xFF; hdr[7] = (riff_size >> 24) & 0xFF;
        memcpy(hdr + 8, "WAVEJUNK", 8);
        hdr[16] = 28;
        memcpy(hdr + 48, "fmt ", 4);
        hdr[52] = 16;
        hdr[56] = audio_fmt & 0xFF; hdr[57] = (audio_fmt >> 8) & 0xFF;
        hdr[58] = channels & 0xFF; hdr[59] = (channels >> 8) & 0xFF;
        hdr[60] = rate & 0xFF; hdr[61] = (rate >> 8) & 0xFF;
        hdr[62] = (rate >> 16) & 0xFF; hdr[63] = (rate >> 24) & 0xFF;
        hdr[64] = byte_rate & 0xFF; hdr[65] = (byte_rate >> 8) & 0xFF;
        hdr[66] = (byte_rate >> 16) & 0xFF; hdr[67] = (byte_rate >> 24) & 0xFF;
        hdr[68] = block_align & 0xFF; hdr[69] = (block_align >> 8) & 0xFF;
        hdr[70] = bits & 0xFF; hdr[71] = (bits >> 8) & 0xFF;
        memcpy(hdr + 72, "data", 4);
        hdr[76] = (uint32_t)data_size & 0xFF; hdr[77] = ((uint32_t)data_size >> 8) & 0xFF;
        hdr[78] = ((uint32_t)data_size >> 16) & 0xFF; hdr[79] = ((uint32_t)data_size >> 24) & 0xFF;

        return fwrite(hdr, 1, sizeof(hdr), f) == sizeof(hdr) ? 0 : -1;
    }
}

#define DESIGN_CACHE_MAX 16

struct faar_design_cache {
    size_t count;
    struct {
        uint32_t in_rate;
        uint32_t channels;
        faar_design *design;
    } entry[DESIGN_CACHE_MAX];
};

faar_design_cache *faar_design_cache_new(void) {
    return calloc(1, sizeof(faar_design_cache));
}

void faar_design_cache_free(faar_design_cache *cache) {
    if (!cache) return;
    for (size_t i = 0; i < cache->count; ++i)
        faar_design_close(&cache->entry[i].design);
    free(cache);
}

/* Open a stream for config, sharing a cached design when the same input rate
   and channel count was seen before. The other settings are fixed for a run. */
static faar_status open_stream(faar_design_cache *cache, const faar_config *config,
                               faar_resampler **r) {
    if (!cache) return faar_resampler_open(config, r);
    for (size_t i = 0; i < cache->count; ++i)
        if (cache->entry[i].in_rate == config->in_rate &&
            cache->entry[i].channels == config->channels)
            return faar_resampler_open_from_design(cache->entry[i].design, r);
    if (cache->count == DESIGN_CACHE_MAX) return faar_resampler_open(config, r);
    faar_design *design = NULL;
    faar_status status = faar_design_open(config, &design);
    if (status != FAAR_OK) return status;
    cache->entry[cache->count].in_rate = config->in_rate;
    cache->entry[cache->count].channels = config->channels;
    cache->entry[cache->count++].design = design;
    return faar_resampler_open_from_design(design, r);
}

int faar_engine_run(pcmfile_t *infile, FILE *fout,
                    const faar_engine_options_t *options,
                    faar_engine_stats_t *stats) {
    if (!infile || !fout || !options || options->output_rate == 0 ||
        infile->channels == 0 || infile->samplerate == 0) {
        fprintf(stderr, "Error: Invalid FAAR engine configuration\n");
        return 1;
    }

    uint32_t in_rate = infile->samplerate;
    uint16_t channels = infile->channels;
    uint32_t target_rate = options->output_rate;
    faar_pcm_format_t fmt = options->format;
    size_t out_bytes_per_sample = fmt == FAAR_PCM_F32LE ? 4 :
                                  fmt == FAAR_PCM_S24LE ? 3 : 2;
    const size_t chunk_frames = 2048;
    faar_config config;
    faar_config_init(&config, sizeof(config));
    config.in_rate = in_rate;
    config.out_rate = target_rate;
    config.channels = channels;
    config.quality = options->quality;
    config.decimate_factor = options->decimate_factor;
    config.max_input_frames = (uint32_t)chunk_frames;
    if (options->dither &&
        (fmt == FAAR_PCM_S16LE || fmt == FAAR_PCM_S24LE)) {
        config.flags = options->dither |
            (fmt == FAAR_PCM_S16LE ? FAAR_DITHER_16 : FAAR_DITHER_24);
    }
    faar_resampler *r = NULL;
    faar_status api_status = open_stream(options->designs, &config, &r);
    if (api_status != FAAR_OK) {
        fprintf(stderr, "Error: Couldn't create resampler instance for %u Hz -> %u Hz: %s\n",
                in_rate, target_rate, faar_strerror(api_status));
        return 1;
    }

    int result = 1;
    float *float_in_buf = NULL;
    float *float_out_buf = NULL;
    uint8_t *raw_out_buf = NULL;
    uint64_t total_out_data_bytes = 0;
    size_t out_cap = chunk_frames * 4;
    uint64_t input_frames_total = 0;
    uint64_t known_total_frames = infile->bounded_data && infile->samples > 0
        ? (uint64_t)infile->samples : 0;
    progress_throttle_t progress_throttle;
    progress_throttle_reset(&progress_throttle);
    double progress_start = wall_seconds();

    float_in_buf = malloc(chunk_frames * channels * sizeof(float));
    float_out_buf = malloc(out_cap * channels * sizeof(float));
    raw_out_buf = malloc(out_cap * channels * out_bytes_per_sample);
    if (!float_in_buf || !float_out_buf || !raw_out_buf) {
        fprintf(stderr, "Error: Out of memory while allocating resampler buffers\n");
        goto cleanup;
    }

    if (options->verbosity > 0)
        fprintf(stderr, "Resampling input: %u Hz -> %u Hz (%u channels)\n",
                in_rate, target_rate, channels);

    if (options->write_wav) {
        uint8_t header_placeholder[80] = {0};
        if (fwrite(header_placeholder, 1, sizeof(header_placeholder), fout) != sizeof(header_placeholder)) {
            fprintf(stderr, "Error: Couldn't write WAV header to output\n");
            goto cleanup;
        }
    }

    for (;;) {
        size_t samples_read = wav_read_float32(infile, float_in_buf,
                                               chunk_frames * channels, NULL);
        const char *read_error = wav_read_error(infile, samples_read);
        if (read_error) {
            fprintf(stderr, "Error: %s\n", read_error);
            goto cleanup;
        }
        if (samples_read == 0)
            break;
        if (samples_read % channels != 0) {
            fprintf(stderr, "Error: Input ends with an incomplete audio frame\n");
            goto cleanup;
        }

        size_t frames_read = samples_read / channels;
        input_frames_total += frames_read;
        size_t produced = 0;
        api_status = faar_resampler_process(r, float_in_buf, frames_read,
                                            float_out_buf, out_cap, &produced);
        if (api_status != FAAR_OK) { fprintf(stderr, "Error: Resampling failed: %s\n", faar_strerror(api_status)); goto cleanup; }
        for (;;) {
            if (write_output_frames(fout, float_out_buf, raw_out_buf, produced,
                                    channels, fmt, out_bytes_per_sample,
                                    &total_out_data_bytes) != 0) {
                fprintf(stderr, "Error: Couldn't write converted audio to output\n");
                goto cleanup;
            }
            api_status = faar_resampler_process(r, NULL, 0, float_out_buf, out_cap, &produced);
            if (api_status != FAAR_OK) { fprintf(stderr, "Error: Resampling failed: %s\n", faar_strerror(api_status)); goto cleanup; }
            if (produced == 0)
                break;
        }
        if (options->verbosity > 0)
            render_progress(&progress_throttle, progress_start, input_frames_total,
                            known_total_frames, in_rate, channels,
                            total_out_data_bytes, false);
    }

    for (;;) {
        size_t flushed = 0;
        api_status = faar_resampler_finalize(r, float_out_buf, out_cap, &flushed);
        if (api_status != FAAR_OK) { fprintf(stderr, "Error: Resampler finalize failed: %s\n", faar_strerror(api_status)); goto cleanup; }
        if (flushed == 0)
            break;
        if (write_output_frames(fout, float_out_buf, raw_out_buf, flushed,
                                channels, fmt, out_bytes_per_sample,
                                &total_out_data_bytes) != 0) {
            fprintf(stderr, "Error: Couldn't write final audio to output\n");
            goto cleanup;
        }
    }

    if (options->verbosity > 0)
        render_progress(&progress_throttle, progress_start, input_frames_total,
                        known_total_frames, in_rate, channels,
                        total_out_data_bytes, true);

    if (options->write_wav) {
        if (fseek(fout, 0, SEEK_SET) != 0) {
            fprintf(stderr, "Error: Couldn't seek to WAV header in output\n");
            goto cleanup;
        }
        if (write_wav_header(fout, target_rate, channels, fmt,
                             total_out_data_bytes) != 0) {
            fprintf(stderr, "Error: Couldn't write WAV header to output\n");
            goto cleanup;
        }
    }

    if (stats) {
        stats->input_frames = input_frames_total;
        stats->output_frames = total_out_data_bytes /
                               (channels * out_bytes_per_sample);
        stats->elapsed_seconds = wall_seconds() - progress_start;
    }
    result = 0;

cleanup:
    free(float_in_buf);
    free(float_out_buf);
    free(raw_out_buf);
    faar_resampler_close(&r);
    return result;
}
