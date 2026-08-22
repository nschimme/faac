/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2001 Menno Bakker
 * Copyright (C) 2002-2026 FAAC Team
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
#include <time.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <sys/time.h>
#endif

#include "encode_engine.h"
#include "input.h"
#include "mp4write.h"
#include "charset.h"

void init_encode_options(encode_options_t *opts)
{
    if (!opts)
        return;

    memset(opts, 0, sizeof(*opts));
    opts->mpeg_version = FAAC_MPEG4;
    opts->object_type = FAAC_OBJ_AUTO;
    opts->joint_mode = FAAC_JOINT_MIXED;
    opts->stream_format = FAAC_STREAM_ADTS;
    opts->shortctl = FAAC_SHORTCTL_NORMAL;
    opts->use_tns = false;
    opts->use_lfe = -1;
    opts->pns_level = -1;
    opts->quant_quality = DEFAULT_QUANT_QUALITY;
    opts->bit_rate = 0;
    opts->center_channel = 3;
    opts->lfe_channel = 4;
    opts->raw_bits = 16;
    opts->raw_rate = 44100;
    opts->raw_endian = 1;
    opts->verbose = 1;
}

void parse_quality_or_bitrate(const char *text, bool is_bitrate_mode,
                               encode_options_t *opts)
{
    int val = text ? atoi(text) : 0;

    if (is_bitrate_mode)
    {
        opts->bit_rate = (val > 0) ? (uint32_t)(val * 1000) : DEFAULT_ABR_KBPS * 1000;
        opts->quant_quality = 0;
    }
    else
    {
        opts->quant_quality = (val > 0) ? (uint32_t)val : DEFAULT_QUANT_QUALITY;
        opts->bit_rate = 0;
    }
}

static double calc_speed(uint64_t current_sample, unsigned int sample_rate, double time_used)
{
    if (time_used <= 0.0 || sample_rate == 0)
        return 0.0;

    return ((double)current_sample / (double)sample_rate) / time_used;
}

static double calc_eta(uint64_t current_sample, uint64_t total_samples, double time_used)
{
    if (current_sample == 0 || time_used <= 0.0 || total_samples < current_sample)
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

static inline bool write_output_bytes(FILE *outfile, const unsigned char *buf, size_t size)
{
    return outfile && (fwrite(buf, 1, size, outfile) == size);
}

static void finalize_mp4(faac_encoder *hEncoder, const encode_options_t *opts)
{
    faac_library_info libinfo = { .struct_size = sizeof(libinfo) };
    faac_get_library_info(&libinfo);

    const uint8_t *asc_data = NULL;
    uint32_t asc_size = 0;
    if (hEncoder)
    {
        faac_encoder_asc(hEncoder, &asc_data, &asc_size);
        mp4_set_decoder_config((unsigned char *)asc_data, asc_size);
    }

    if (libinfo.version)
    {
        size_t ver_len = strlen(libinfo.version) + 6;
        char *version_string = malloc(ver_len);
        if (version_string)
        {
            snprintf(version_string, ver_len, "FAAC %s", libinfo.version);
            mp4_set_encoder(version_string);
            free(version_string);
        }
    }

    uint32_t creation_time = 0;
    if (opts->creation_time_str)
    {
        if (!strcmp(opts->creation_time_str, "auto"))
        {
            if (opts->input_filename && strcmp(opts->input_filename, "-") != 0)
            {
                struct stat st;
                if (stat(opts->input_filename, &st) == 0)
                {
                    creation_time = (uint32_t)st.st_mtime;
                }
                else if (opts->verbose)
                {
                    fprintf(stderr, "couldn't stat() input file %s, defaulting to 0\n", opts->input_filename);
                }
            }
            else if (opts->verbose)
            {
                fprintf(stderr, "cannot use --creation-time auto with stdin, defaulting to 0\n");
            }
        }
        else if (!strcmp(opts->creation_time_str, "now"))
        {
            creation_time = (uint32_t)time(NULL);
        }
        else
        {
            char *endptr;
            errno = 0;
            creation_time = (uint32_t)strtoul(opts->creation_time_str, &endptr, 10);
            if (errno != 0 || *endptr != '\0')
            {
                if (opts->verbose)
                    fprintf(stderr, "invalid creation time %s, defaulting to 0\n", opts->creation_time_str);
                creation_time = 0;
            }
        }
        mp4_set_creation_time(creation_time);
    }
    else
    {
        const char *sde = getenv("SOURCE_DATE_EPOCH");
        if (sde)
        {
            char *endptr;
            errno = 0;
            creation_time = (uint32_t)strtoul(sde, &endptr, 10);
            if (errno != 0 || *endptr != '\0')
            {
                if (opts->verbose)
                    fprintf(stderr, "invalid SOURCE_DATE_EPOCH %s, ignoring\n", sde);
                creation_time = 0;
            }
        }
        mp4_set_creation_time(creation_time);
    }

    if (opts->art_data && opts->art_size > 0)
    {
        mp4_set_cover(opts->art_data, (int)opts->art_size);
    }

    char *allocated_tags[MP4TAG_COUNT] = { 0 };
    int num_allocated = 0;

    const mp4_metadata_t *metadata = &opts->metadata;
#define SETTAG(id, x) \
    do { \
        if (x) { \
            char *utf8_val = utf8_ensure(x); \
            mp4_set_tag(id, utf8_val); \
            if (utf8_val && num_allocated < MP4TAG_COUNT) \
                allocated_tags[num_allocated++] = utf8_val; \
            else if (utf8_val) \
                free(utf8_val); \
        } \
    } while (0)

    SETTAG(MP4TAG_ARTIST, metadata->artist);
    SETTAG(MP4TAG_ARTISTSORT, metadata->artist_sort);
    SETTAG(MP4TAG_COMPOSER, metadata->composer);
    SETTAG(MP4TAG_COMPOSERSORT, metadata->composer_sort);
    SETTAG(MP4TAG_TITLE, metadata->title);
    SETTAG(MP4TAG_ALBUM, metadata->album);
    SETTAG(MP4TAG_ALBUMARTIST, metadata->album_artist);
    SETTAG(MP4TAG_ALBUMARTISTSORT, metadata->album_artist_sort);
    SETTAG(MP4TAG_ALBUMSORT, metadata->album_sort);
    SETTAG(MP4TAG_YEAR, metadata->year);
    SETTAG(MP4TAG_COMMENT, metadata->comment);
#undef SETTAG
    if (metadata->track) mp4_set_track(metadata->track, metadata->ntracks);
    if (metadata->disc) mp4_set_disc(metadata->disc, metadata->ndiscs);
    if (metadata->compilation) mp4_set_compilation(metadata->compilation);
    if (metadata->genre_id) mp4_set_genre(metadata->genre_id);

    if (mp4_finish() != 0)
    {
        if (opts->verbose)
            fprintf(stderr, "mp4_finish() failed: output file may be incomplete\n");
    }

    for (int i = 0; i < num_allocated; i++)
    {
        free(allocated_tags[i]);
    }
}

int run_encoding_session(const encode_options_t *opts,
                          progress_callback_t progress_cb,
                          void *user_data)
{
    if (!opts || !opts->input_filename)
        return 1;

    pcmfile_t *infile = NULL;
    faac_encoder *hEncoder = NULL;
    FILE *outfile = NULL;

    float *pcmbuf = NULL;
    unsigned char *bitbuf = NULL;
    int *chanmap = NULL;

    int ret = 0;
    bool mp4_is_open = false;

    if (opts->raw_pcm_input)
    {
        infile = wav_open_read(opts->input_filename, 1);
        if (infile)
        {
            infile->bigendian = opts->raw_endian;
            infile->channels = opts->raw_channels > 0 ? opts->raw_channels : 2;
            infile->samplebytes = opts->raw_bits / 8;
            infile->samplerate = opts->raw_rate;
            infile->samples /= (infile->channels * infile->samplebytes);
        }
    }
    else
    {
        infile = wav_open_read(opts->input_filename, 0);
    }

    if (!infile)
    {
        if (opts->verbose)
            fprintf(stderr, "Couldn't open input file %s\n", opts->input_filename);
        return 1;
    }

    unsigned int sample_rate = infile->samplerate;
    unsigned int num_channels = infile->channels;

    faac_library_info libinfo = { .struct_size = sizeof(libinfo) };
    faac_get_library_info(&libinfo);
    if (num_channels > libinfo.max_channels)
    {
        if (opts->verbose)
        {
            fprintf(stderr, "Input file %s has %u channels, but this build supports at most %u.\n",
                    opts->input_filename, num_channels, libinfo.max_channels);
        }
        ret = 1;
        goto cleanup;
    }

    faac_params params;
    faac_params_init(&params, sizeof(params));
    params.sample_rate = sample_rate;
    params.num_channels = num_channels;
    params.mpeg_version = opts->mpeg_version;
    params.object_type = opts->object_type;
    params.joint_mode = opts->joint_mode;
    params.use_tns = opts->use_tns;
    params.use_lfe = (opts->use_lfe != -1) ? (opts->use_lfe != 0) : (num_channels >= 6);
    params.short_control = opts->shortctl;
    if (opts->pns_level >= 0)
        params.pns_level = opts->pns_level;

    if (opts->quant_quality > 0 && opts->bit_rate == 0)
    {
        params.quant_quality = opts->quant_quality;
        params.bit_rate = 0;
    }
    else if (opts->bit_rate > 0)
    {
        params.bit_rate = opts->bit_rate / (num_channels ? num_channels : 1);
        params.quant_quality = 0;
    }

    if (opts->max_bit_rate > 0)
        params.max_bit_rate = opts->max_bit_rate;

    params.bandwidth = opts->bandwidth;
    params.output_format = opts->container_mp4 ? FAAC_STREAM_RAW : opts->stream_format;
    params.input_format = FAAC_INPUT_FLOAT;

    if (faac_encoder_open(&params, &hEncoder) != FAAC_OK)
    {
        if (opts->verbose)
            fprintf(stderr, "Couldn't open encoder instance for %s\n", opts->input_filename);
        ret = 1;
        goto cleanup;
    }

    faac_encoder_info info = { .struct_size = sizeof(info) };
    faac_encoder_get_info(hEncoder, &info);

    unsigned long samples_per_frame = (unsigned long)info.frame_samples * num_channels;
    unsigned long max_output_bytes = info.max_output_bytes;
    unsigned int frame_size = samples_per_frame / num_channels;

    pcmbuf = malloc(samples_per_frame * sizeof(float));
    bitbuf = malloc(max_output_bytes * sizeof(unsigned char));

    if (!pcmbuf || !bitbuf)
    {
        if (opts->verbose)
            fprintf(stderr, "Out of memory!\n");
        ret = 1;
        goto cleanup;
    }

    chanmap = mk_chan_map(num_channels, opts->center_channel, opts->lfe_channel);

    if (opts->container_mp4)
    {
        if (opts->output_filename && !strcmp(opts->output_filename, "-"))
        {
            if (opts->verbose)
                fprintf(stderr, "Cannot encode MP4 to stdout\n");
            ret = 1;
            goto cleanup;
        }

        if (mp4_open(opts->output_filename, opts->overwrite) != 0)
        {
            if (opts->verbose)
                fprintf(stderr, "Couldn't create MP4 output file %s\n", opts->output_filename);
            ret = 1;
            goto cleanup;
        }
        mp4_is_open = true;
        mp4_set_format(sample_rate, num_channels, infile->samplebytes * 8);
    }
    else if (opts->output_filename)
    {
        if (!strcmp(opts->output_filename, "-"))
        {
            outfile = stdout;
#ifdef _WIN32
            _setmode(_fileno(stdout), _O_BINARY);
#endif
        }
        else
        {
            outfile = fopen(opts->output_filename, "wb");
            if (!outfile)
            {
                if (opts->verbose)
                    fprintf(stderr, "Couldn't create output file %s\n", opts->output_filename);
                ret = 1;
                goto cleanup;
            }
        }
    }

    uint64_t total_input_samples = (uint64_t)infile->samples;
    uint32_t total_frames = (total_input_samples > 0 && frame_size > 0) ?
        (uint32_t)(((total_input_samples + frame_size - 1) / frame_size) + 1) : 0;

    uint32_t current_frame = 0;
    uint64_t total_bytes_written = 0;
    uint64_t current_input_samples = 0;
    uint64_t encoded_samples = 0;
    int samples_read = 0;

    double start_time = get_wall_time_sec();

    for (;;)
    {
        int bytes_written = 0;

        if (!opts->ignore_wav_length)
        {
            if (current_input_samples < total_input_samples || total_input_samples == 0)
            {
                samples_read = wav_read_float32(infile, pcmbuf, samples_per_frame, chanmap);
            }
            else
            {
                samples_read = 0;
            }

            if (total_input_samples > 0 &&
                current_input_samples + (samples_read / num_channels) > total_input_samples)
            {
                samples_read = (int)((total_input_samples - current_input_samples) * num_channels);
            }
        }
        else
        {
            samples_read = wav_read_float32(infile, pcmbuf, samples_per_frame, chanmap);
        }

        current_input_samples += (samples_read / num_channels);

        uint32_t nbytes = 0;
        faac_status st = faac_encoder_encode(hEncoder,
                                             pcmbuf,
                                             (uint32_t)samples_read,
                                             bitbuf,
                                             (uint32_t)max_output_bytes,
                                             &nbytes);
        bytes_written = (st == FAAC_OK) ? (int)nbytes : -1;

        if (bytes_written > 0)
        {
            current_frame++;
            total_bytes_written += bytes_written;
        }

        if (!samples_read && !bytes_written)
            break;

        if (bytes_written < 0)
        {
            if (opts->verbose)
                fprintf(stderr, "faac_encoder_encode() failed: %s\n", faac_strerror(st));
            ret = 1;
            goto cleanup;
        }

        if (bytes_written > 0)
        {
            uint64_t frame_samples = current_input_samples - encoded_samples;
            if (frame_samples > frame_size)
                frame_samples = frame_size;

            if (opts->container_mp4)
            {
                if (mp4_write_frame(bitbuf, (uint32_t)bytes_written, (uint32_t)frame_samples) != 0)
                {
                    if (opts->verbose)
                        fprintf(stderr, "mp4_write_frame() failed\n");
                    ret = 1;
                    goto cleanup;
                }
            }
            else
            {
                if (!write_output_bytes(outfile, bitbuf, (size_t)bytes_written))
                {
                    if (opts->verbose)
                        fprintf(stderr, "Output write failed\n");
                    ret = 1;
                    goto cleanup;
                }
            }

            encoded_samples += frame_samples;
        }

        if (progress_cb && (current_frame % 32 == 0 || (total_input_samples > 0 && current_input_samples >= total_input_samples)))
        {
            double time_used = get_wall_time_sec() - start_time;
            progress_info_t prog = {
                .current_input_samples = current_input_samples,
                .total_input_samples = total_input_samples,
                .sample_rate = sample_rate,
                .num_channels = num_channels,
                .current_frame = current_frame,
                .total_frames = total_frames,
                .total_bytes_written = total_bytes_written,
                .time_elapsed_sec = time_used,
                .speed_factor = calc_speed(current_input_samples, sample_rate, time_used),
                .eta_sec = calc_eta(current_input_samples, total_input_samples, time_used)
            };

            if (!progress_cb(&prog, user_data))
            {
                ret = ENCODE_CANCELLED;
                goto cleanup;
            }
        }
    }

    /* Flush remaining buffered frames */
    do {
        uint32_t nbytes = 0;
        faac_status st = faac_encoder_encode(hEncoder, NULL, 0, bitbuf, (uint32_t)max_output_bytes, &nbytes);
        int bytes_written = (st == FAAC_OK) ? (int)nbytes : -1;

        if (bytes_written > 0)
        {
            if (opts->container_mp4)
            {
                if (mp4_write_frame(bitbuf, (uint32_t)bytes_written, info.frame_samples) != 0)
                {
                    ret = 1;
                    goto cleanup;
                }
            }
            else
            {
                if (!write_output_bytes(outfile, bitbuf, (size_t)bytes_written))
                {
                    if (opts->verbose)
                        fprintf(stderr, "Output write failed during flush\n");
                    ret = 1;
                    goto cleanup;
                }
            }
        }
        else
        {
            break;
        }
    } while (1);

    if (opts->container_mp4 && mp4_is_open)
    {
        finalize_mp4(hEncoder, opts);
    }

cleanup:
    if (pcmbuf) free(pcmbuf);
    if (bitbuf) free(bitbuf);
    if (chanmap) free(chanmap);

    if (opts->container_mp4 && mp4_is_open)
    {
        mp4_close();
        mp4_is_open = false;
    }

    if (outfile && outfile != stdout)
        fclose(outfile);

    if (hEncoder)
        faac_encoder_close(&hEncoder);

    if (infile)
        wav_close(infile);

    return ret;
}
