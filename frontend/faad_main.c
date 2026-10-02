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
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef HAVE_GETOPT_H
# include <getopt.h>
#else
# include "getopt.h"
# include "getopt.c"
#endif

#include "faad.h"
#include "charset.h"
#include "endian.h"
#include "asc_codec.h"
#include "mp4read.h"

typedef struct {
    uint8_t *data;
    uint32_t size;
    uint32_t head;
    uint32_t tail;
    uint32_t fill;
} PCMFifo;

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

static void fifo_init(PCMFifo *f, uint32_t capacity)
{
    f->data = (uint8_t *)malloc(capacity > 0 ? capacity : 65536);
    f->size = capacity > 0 ? capacity : 65536;
    f->head = 0;
    f->tail = 0;
    f->fill = 0;
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

static void fifo_push(PCMFifo *f, const uint8_t *src, uint32_t len)
{
    if (len == 0) return;
    if (f->fill + len > f->size) {
        uint32_t new_size = f->size * 2;
        while (new_size < f->fill + len) new_size *= 2;
        uint8_t *new_data = (uint8_t *)malloc(new_size);
        if (f->fill > 0) {
            if (f->tail < f->head) {
                memcpy(new_data, f->data + f->tail, f->fill);
            } else {
                uint32_t first = f->size - f->tail;
                memcpy(new_data, f->data + f->tail, first);
                memcpy(new_data + first, f->data, f->head);
            }
        }
        free(f->data);
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
}

static uint32_t fifo_pop(PCMFifo *f, uint8_t *dst, uint32_t len)
{
    if (len > f->fill) len = f->fill;
    if (len == 0) return 0;
    uint32_t first = f->size - f->tail;
    if (len <= first) {
        memcpy(dst, f->data + f->tail, len);
        f->tail = (f->tail + len) % f->size;
    } else {
        memcpy(dst, f->data + f->tail, first);
        memcpy(dst + first, f->data, len - first);
        f->tail = len - first;
    }
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

/* libfaad emits host-order samples; WAV wants little-endian. */
static void pcm_to_little_endian(uint8_t *buf, uint32_t bytes, uint32_t sample_bytes)
{
#if WORDS_BIGENDIAN
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
#else
    (void)buf; (void)bytes; (void)sample_bytes;
#endif
}

/* dwChannelMask for the WAV order libfaad outputs (FL FR FC LFE BL BR SL SR). */
static uint32_t wav_channel_mask(uint16_t num_channels)
{
    switch (num_channels) {
    case 3: return 0x007;  /* FL FR FC */
    case 4: return 0x107;  /* FL FR FC BC */
    case 5: return 0x037;  /* FL FR FC BL BR */
    case 6: return 0x03F;  /* FL FR FC LFE BL BR */
    case 8: return 0x63F;  /* FL FR FC LFE BL BR SL SR */
    default: return 0;
    }
}

/* Plain PCM/float up to two channels; WAVE_FORMAT_EXTENSIBLE with a
 * channel mask above that, so players place the surround channels. */
static void write_wav_header(FILE *f, uint32_t sample_rate, uint16_t num_channels, uint32_t total_pcm_bytes, uint16_t bits_per_sample, bool is_float)
{
    /* stdout cannot be patched afterwards: declare an unknown length (all ones)
     * the way streaming writers do, so readers consume to EOF */
    bool stream = (f == stdout);
    if (!stream) fseek(f, 0, SEEK_SET);
    bool extensible = num_channels > 2;
    uint32_t fmt_size = extensible ? 40 : 16;
    uint32_t file_size = htole32(stream ? UINT32_MAX : 4 + 8 + fmt_size + 8 + total_pcm_bytes);
    uint16_t bytes_per_sample = bits_per_sample / 8;
    uint32_t byte_rate = htole32(sample_rate * num_channels * bytes_per_sample);
    uint16_t block_align = htole16(num_channels * bytes_per_sample);
    uint32_t sr_le = htole32(sample_rate);
    uint16_t ch_le = htole16(num_channels);
    uint16_t bps_le = htole16(bits_per_sample);

    fwrite("RIFF", 1, 4, f);
    fwrite(&file_size, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);

    uint32_t fmt_chunk_size = htole32(fmt_size);
    uint16_t audio_format = htole16(extensible ? 0xFFFE : (is_float ? 3 : 1)); /* 1 = PCM, 3 = IEEE Float */
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
        uint32_t mask = htole32(wav_channel_mask(num_channels));
        /* KSDATAFORMAT_SUBTYPE_PCM / _IEEE_FLOAT: {0000000X-0000-0010-8000-00AA00389B71} */
        uint8_t guid[16] = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
                             0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 };
        if (is_float) guid[0] = 0x03;
        fwrite(&cb_size, 2, 1, f);
        fwrite(&valid_bits, 2, 1, f);
        fwrite(&mask, 4, 1, f);
        fwrite(guid, 1, 16, f);
    }

    uint32_t pcm_bytes_le = htole32(stream ? UINT32_MAX : total_pcm_bytes);
    fwrite("data", 1, 4, f);
    fwrite(&pcm_bytes_le, 4, 1, f);
}

static bool output_exists(const char *path)
{
    FILE *f = cli_fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

static void print_usage(const char *prog)
{
    faad_library_info info;
    info.struct_size = sizeof(info);
    if (faad_get_library_info(&info) != FAAD_OK) {
        info.version = "3.0.0";
    }

    char version_buf[128];
    printf("FAAD %s\n",
           cli_version_string(version_buf, sizeof(version_buf), info.version));
    printf("Usage: %s [options] <infile.aac|infile.m4a>\n\n", prog);
    printf("I/O & Format Options:\n");
    printf("  -o, --output <file>    Set output filename (default: infile.wav, or stdout for stdin input)\n");
    printf("  -w, --stdout           Write output PCM to stdout\n");
    printf("  -f, --format <type>    Output container format: wav (default), raw\n");
    printf("  -b, --bits <depth>     Sample depth: 16 (default), 24, 32f (32-bit float)\n");
    printf("  -a, --adts <file>      Extract raw ADTS stream from MP4 without decoding\n\n");
    printf("Processing Options:\n");
    printf("  -d, --downmix [mode]   Downmix audio (mono/1 or stereo/2, default: mono)\n");
    printf("  -j, --jump <seconds>   Start decoding from specified timestamp\n");
    printf("  -g, --no-gapless       Disable automatic gapless trim/padding handling\n\n");
    printf("Information & General:\n");
    printf("  -i, --info             Display bitstream & container metadata, then exit\n");
    printf("      --json             Output bitstream info in JSON format\n");
    printf("  -q, --quiet            Quiet mode (suppress decoding progress)\n");
    printf("      --overwrite        Overwrite an existing output file\n");
    printf("      --strict           Strict mode (noisily error and report debug details on failure)\n");
    printf("      --license          Display copyright and license information\n");
    printf("  -h, --help             Display this help text\n");
}

enum {
    OPT_NO_GAPLESS = 300,
    OPT_JSON,
    OPT_STRICT,
    OPT_OVERWRITE,
    OPT_LICENSE
};

static void print_strict_error(const char *filename, uint64_t offset, uint32_t frame_idx, faad_status st)
{
    fprintf(stderr, "%s:0x%04llx: frame %u: error %d (%s)\n",
            filename ? filename : "input", (unsigned long long)offset, frame_idx, st, faad_strerror(st));
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    int wargc = 0;
    wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    char **allocated_argv = NULL;
    if (wargv && wargc > 0) {
        allocated_argv = (char **)calloc((size_t)wargc, sizeof(char *));
        if (allocated_argv) {
            for (int i = 0; i < wargc; i++)
                allocated_argv[i] = win32_utf16_to_utf8(wargv[i]);
            argv = allocated_argv;
            argc = wargc;
        }
    }
#endif

    const char *infile = NULL;
    const char *outfile = NULL;
    const char *adts_outfile = NULL;
    bool write_stdout = false;
    bool raw_format = false;
    uint32_t bit_depth = 16;
    bool is_float = false;
    enum faad_downmix_mode downmix = FAAD_DOWNMIX_NONE;
    bool gapless = true;
    bool info_only = false;
    bool json_info = false;
    bool quiet = false;
    bool strict_mode = false;
    bool overwrite = false;
    double jump_seconds = 0.0;

    static struct option long_options[] = {
        {"output", required_argument, 0, 'o'},
        {"stdout", no_argument, 0, 'w'},
        {"format", required_argument, 0, 'f'},
        {"bits", required_argument, 0, 'b'},
        {"adts", required_argument, 0, 'a'},
        {"downmix", optional_argument, 0, 'd'},
        {"jump", required_argument, 0, 'j'},
        {"no-gapless", no_argument, 0, 'g'},
        {"info", no_argument, 0, 'i'},
        {"json", no_argument, 0, OPT_JSON},
        {"quiet", no_argument, 0, 'q'},
        {"strict", no_argument, 0, OPT_STRICT},
        {"overwrite", no_argument, 0, OPT_OVERWRITE},
        {"license", no_argument, 0, OPT_LICENSE},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "o:wf:b:a:d::j:giqh", long_options, &option_index)) != -1) {
        switch (opt) {
        case 'o': outfile = optarg; break;
        case 'w': write_stdout = true; break;
        case 'f':
            if (strcmp(optarg, "raw") == 0) raw_format = true;
            else if (strcmp(optarg, "wav") != 0) { fprintf(stderr, "Unknown format '%s' (wav or raw)\n", optarg); return 1; }
            break;
        case 'b':
            if (strcmp(optarg, "1") == 0 || strcmp(optarg, "16") == 0) { bit_depth = 16; is_float = false; }
            else if (strcmp(optarg, "2") == 0 || strcmp(optarg, "24") == 0) { bit_depth = 24; is_float = false; }
            else if (strcmp(optarg, "3") == 0 || strcmp(optarg, "4") == 0 || strcmp(optarg, "32f") == 0 || strcmp(optarg, "32") == 0) { bit_depth = 32; is_float = true; }
            else { fprintf(stderr, "Unknown bit depth '%s' (16, 24 or 32f)\n", optarg); return 1; }
            break;
        case 'a': adts_outfile = optarg; break;
        case 'd': {
            /* the mode is optional: -d, -dstereo, --downmix=2, or -d stereo */
            const char *mode = optarg;
            if (!mode && optind < argc && (!strcmp(argv[optind], "mono") || !strcmp(argv[optind], "stereo") ||
                                           !strcmp(argv[optind], "1") || !strcmp(argv[optind], "2")))
                mode = argv[optind++];
            if (!mode || !strcmp(mode, "mono") || !strcmp(mode, "1")) downmix = FAAD_DOWNMIX_MONO;
            else if (!strcmp(mode, "stereo") || !strcmp(mode, "2")) downmix = FAAD_DOWNMIX_STEREO;
            else { fprintf(stderr, "Unknown downmix mode '%s' (mono/1 or stereo/2)\n", mode); return 1; }
            break;
        }
        case 'j': {
            char *end;
            jump_seconds = strtod(optarg, &end);
            if (end == optarg || *end || jump_seconds < 0.0) { fprintf(stderr, "Invalid jump time '%s'\n", optarg); return 1; }
            break;
        }
        case 'g': gapless = false; break;
        case OPT_NO_GAPLESS: gapless = false; break;
        case 'i': info_only = true; break;
        case OPT_JSON: json_info = true; info_only = true; break;
        case 'q': quiet = true; break;
        case OPT_STRICT: strict_mode = true; break;
        case OPT_OVERWRITE: overwrite = true; break;
        case OPT_LICENSE: {
            faad_library_info info;
            info.struct_size = sizeof(info);
            if (faad_get_library_info(&info) == FAAD_OK && info.copyright)
                fprintf(stderr, "%s", info.copyright);
            cli_print_lgpl_notice(stderr, "library");
            return 0;
        }
        case 'h': print_usage(argv[0]); return 0;
        default: print_usage(argv[0]); return 1;
        }
    }

    if (optind < argc) {
        infile = argv[optind];
    }

    if (!infile) {
        print_usage(argv[0]);
        return 1;
    }

    FILE *fin = NULL;
    uint8_t *inbuf = NULL;
    long file_len = 0;

    if (strcmp(infile, "-") == 0) {
        fin = stdin;
        if (!outfile && !write_stdout && !info_only && !adts_outfile) {
            write_stdout = true;
        }
        size_t cap = 65536;
        size_t len = 0;
        inbuf = (uint8_t *)malloc(cap);
        if (!inbuf) return 1;
        while (!feof(fin)) {
            if (len + 16384 > cap) {
                cap *= 2;
                uint8_t *new_buf = (uint8_t *)realloc(inbuf, cap);
                if (!new_buf) { free(inbuf); return 1; }
                inbuf = new_buf;
            }
            size_t n = fread(inbuf + len, 1, 16384, fin);
            if (n == 0) break;
            len += n;
        }
        file_len = (long)len;
    } else {
        fin = cli_fopen(infile, "rb");
        if (!fin) {
            fprintf(stderr, "Error opening input file %s\n", infile);
            return 1;
        }

        fseek(fin, 0, SEEK_END);
        file_len = ftell(fin);
        fseek(fin, 0, SEEK_SET);

        inbuf = (uint8_t *)malloc(file_len > 0 ? file_len : 1);
        if (!inbuf) {
            fclose(fin);
            return 1;
        }

        if (fread(inbuf, 1, file_len, fin) != (size_t)file_len) {
            fprintf(stderr, "Error reading input file\n");
            free(inbuf);
            fclose(fin);
            return 1;
        }
        fclose(fin);
    }

    MP4Track track;
    memset(&track, 0, sizeof(track));
    bool is_mp4 = mp4_read_track_buf(inbuf, file_len, &track);
    if (is_mp4 && (!track.asc_buf || track.num_samples == 0)) {
        fprintf(stderr, "%s: no supported AAC audio track found in MP4 file\n", infile);
        free(inbuf);
        mp4_free_track(&track);
        return 1;
    }

    if (adts_outfile && !is_mp4) {
        fprintf(stderr, "%s: --adts needs an MP4 input\n", infile);
        free(inbuf);
        return 1;
    }

    if (adts_outfile && is_mp4) {
        if (!overwrite && output_exists(adts_outfile)) {
            fprintf(stderr, "Output file %s already exists (use --overwrite)\n", adts_outfile);
            free(inbuf);
            mp4_free_track(&track);
            return 1;
        }
        FILE *fadts = cli_fopen(adts_outfile, "wb");
        if (!fadts) {
            fprintf(stderr, "Error opening ADTS output file %s\n", adts_outfile);
            free(inbuf);
            mp4_free_track(&track);
            return 1;
        }
        /* ADTS carries the core layer: for HE the SBR/PS data rides
         * implicitly inside each frame, so the header names the core
         * object type and the core sampling rate. */
        AscInfo asc;
        asc_codec_parse(track.asc_buf, track.asc_len, &asc);
        uint8_t profile = (asc.object_type >= 1 && asc.object_type <= 4) ? (uint8_t)(asc.object_type - 1) : 1;
        int sr_idx = asc_codec_sr_idx(asc.sample_rate);
        if (sr_idx < 0) {
            fprintf(stderr, "%s: core sample rate %u Hz has no ADTS sampling frequency index\n",
                    infile, (unsigned)asc.sample_rate);
            fclose(fadts);
            remove(adts_outfile);
            free(inbuf);
            mp4_free_track(&track);
            return 1;
        }
        uint8_t ch = asc.num_channels & 7;
        for (uint32_t s = 0; s < track.num_samples; s++) {
            uint64_t offset = track.samples[s].offset;
            uint32_t size = track.samples[s].size;
            uint32_t frame_len = size + 7;
            if (offset > 0 && offset + size <= (uint64_t)file_len && frame_len <= 0x1FFF) {
                uint8_t adts_hdr[7] = { 0xFF, 0xF1, 0x00, 0x00, 0x00, 0x1F, 0xFC };
                adts_hdr[2] = (uint8_t)((profile << 6) | ((sr_idx & 0x0F) << 2) | ((ch >> 2) & 1));
                adts_hdr[3] = (uint8_t)(((ch & 3) << 6) | ((frame_len >> 11) & 0x03));
                adts_hdr[4] = (uint8_t)((frame_len >> 3) & 0xFF);
                adts_hdr[5] = (uint8_t)(((frame_len & 0x07) << 5) | 0x1F);
                fwrite(adts_hdr, 1, 7, fadts);
                fwrite(inbuf + offset, 1, size, fadts);
            }
        }
        fclose(fadts);
        if (!quiet) printf("Extracted %u raw ADTS frames to %s\n", track.num_samples, adts_outfile);
        free(inbuf);
        mp4_free_track(&track);
        return 0;
    }

    faad_config cfg;
    faad_config_init(&cfg, sizeof(cfg));
    cfg.stream_format = is_mp4 ? FAAD_STREAM_RAW : FAAD_STREAM_ADTS;
    cfg.output_format = is_float ? FAAD_OUTPUT_FLOAT : bit_depth == 24 ? FAAD_OUTPUT_24BIT : FAAD_OUTPUT_16BIT;
    cfg.downmix_mode = downmix;

    faad_decoder *dec = NULL;
    faad_status st = faad_decoder_open(&cfg, is_mp4 ? track.asc_buf : NULL, is_mp4 ? track.asc_len : 0, &dec);
    if (st != FAAD_OK) {
        fprintf(stderr, "Failed to open FAAD decoder: %s\n", faad_strerror(st));
        free(inbuf);
        if (is_mp4) mp4_free_track(&track);
        return 1;
    }

    FILE *fout = NULL;
    bool header_pending = false;
    uint16_t header_channels = 2; /* the count the header was first written with; its size depends on it */
    if (!info_only) {
        if (write_stdout) {
            fout = stdout;
            quiet = true;
            header_pending = !raw_format;
        } else {
            if (!outfile) {
                char *out_path = (char *)malloc(strlen(infile) + 8);
                strcpy(out_path, infile);
                char *dot = strrchr(out_path, '.');
                if (dot) strcpy(dot, raw_format ? ".raw" : ".wav");
                else strcat(out_path, raw_format ? ".raw" : ".wav");
                outfile = out_path;
            }
            if (!overwrite && output_exists(outfile)) {
                fprintf(stderr, "Output file %s already exists (use --overwrite)\n", outfile);
                faad_decoder_close(&dec);
                free(inbuf);
                if (is_mp4) mp4_free_track(&track);
                return 1;
            }
            fout = cli_fopen(outfile, "wb");
            if (!fout) {
                fprintf(stderr, "Error opening output file %s\n", outfile);
                faad_decoder_close(&dec);
                free(inbuf);
                if (is_mp4) mp4_free_track(&track);
                return 1;
            }
            /* The header waits for the first decoded frame: an ADTS stream's
             * channel count (and with it the header's size) isn't known before. */
            header_pending = !raw_format;
        }
    }

    uint8_t outbuf[65536];
    uint32_t total_pcm_bytes = 0;
    uint32_t sample_rate = 44100;
    uint32_t num_channels = 2;
    enum faad_object_type obj_type = FAAD_OBJ_LC;
    uint32_t frames_decoded = 0;

    uint32_t start_frame = 0;
    if (jump_seconds > 0.0) {
        faad_stream_info sinfo = { .struct_size = sizeof(faad_stream_info) };
        uint32_t sr = 44100;
        uint32_t fl = 1024;
        if (faad_decoder_get_info(dec, &sinfo) == FAAD_OK) {
            if (sinfo.sample_rate > 0) sr = sinfo.sample_rate;
            if (sinfo.object_type == FAAD_OBJ_HE_AAC_V1 || sinfo.object_type == FAAD_OBJ_HE_AAC_V2) {
                fl = 2048;
            }
        }
        start_frame = (uint32_t)((jump_seconds * (double)sr) / (double)fl);
    }

    /* Container priming uses track ticks, while the SBR filter delay uses
     * core samples. Convert each to output samples before combining them. */
    uint32_t samples_to_skip = (is_mp4 && gapless) ? track.delay : 0;
    uint32_t padding_samples = (is_mp4 && gapless) ? track.padding : 0;
    bool gapless_scaled = false;
    PCMFifo fifo;
    fifo_init(&fifo, 262144);

    if (is_mp4) {
        for (uint32_t s = start_frame; s < track.num_samples; s++) {
            uint64_t offset = track.samples[s].offset;
            uint32_t size = track.samples[s].size;
            if (offset == 0 || offset + size > (uint64_t)file_len) continue;

            uint32_t bytes_consumed = 0;
            uint32_t bytes_written = 0;

            faad_frame_info finfo;
            st = faad_decode_frame(dec, inbuf + offset, size,
                                   &bytes_consumed, outbuf, sizeof(outbuf), &bytes_written, &finfo);

            if (st != FAAD_OK) {
                if (strict_mode) {
                    print_strict_error(infile, offset, s, st);
                    faad_decoder_close(&dec);
                    free(inbuf);
                    mp4_free_track(&track);
                    return 1;
                }
            } else if (bytes_written > 0) {
                /* Per-frame info, not the stream info: SBR may be signalled
                 * implicitly and only known once the payload is decoded. */
                sample_rate = finfo.sample_rate;
                num_channels = finfo.channels;
                if (header_pending) {
                    write_wav_header(fout, sample_rate, (uint16_t)num_channels, 0, bit_depth, is_float);
                    header_channels = (uint16_t)num_channels;
                    header_pending = false;
                }
                obj_type = finfo.ps_active ? FAAD_OBJ_HE_AAC_V2 : finfo.sbr_active ? FAAD_OBJ_HE_AAC_V1 : FAAD_OBJ_LC;

                uint32_t dec_bytes_per_sample = is_float ? 4 : bit_depth / 8;
                uint32_t dec_bytes_per_frame_sample = num_channels * dec_bytes_per_sample;
                pcm_to_little_endian(outbuf, bytes_written, dec_bytes_per_sample);
                uint32_t frame_samples = bytes_written / dec_bytes_per_frame_sample;
                if (!gapless_scaled) {
                    faad_stream_info sinfo = { .struct_size = sizeof(faad_stream_info) };
                    faad_decoder_get_info(dec, &sinfo);
                    uint32_t core_rate = finfo.sbr_active && finfo.samples_per_ch == 2048
                        ? finfo.sample_rate / 2 : finfo.sample_rate;
                    uint32_t timescale = track.timescale ? track.timescale : core_rate;
                    uint32_t delay = gapless ? (uint32_t)((uint64_t)sinfo.delay_samples
                        * finfo.sample_rate / core_rate) : 0;
                    samples_to_skip = (uint32_t)((uint64_t)samples_to_skip * finfo.sample_rate / timescale) + delay;
                    padding_samples = (uint32_t)((uint64_t)padding_samples * finfo.sample_rate / timescale);
                    padding_samples = padding_samples > delay ? padding_samples - delay : 0;
                    gapless_scaled = true;
                }

                uint8_t *write_ptr = outbuf;
                uint32_t samples_to_write = frame_samples;

                if (samples_to_skip > 0) {
                    if (samples_to_skip >= samples_to_write) {
                        samples_to_skip -= samples_to_write;
                        samples_to_write = 0;
                    } else {
                        write_ptr += samples_to_skip * dec_bytes_per_frame_sample;
                        samples_to_write -= samples_to_skip;
                        samples_to_skip = 0;
                    }
                }

                if (fout && samples_to_write > 0) {
                    fifo_push(&fifo, write_ptr, samples_to_write * dec_bytes_per_frame_sample);

                    uint32_t padding_bytes = padding_samples * num_channels * (bit_depth / 8);
                    if (fifo.fill > padding_bytes) {
                        uint32_t can_pop = fifo.fill - padding_bytes;
                        uint8_t pop_buf[4096];
                        while (can_pop > 0) {
                            uint32_t chunk = can_pop < sizeof(pop_buf) ? can_pop : sizeof(pop_buf);
                            uint32_t popped = fifo_pop(&fifo, pop_buf, chunk);
                            if (popped == 0) break;
                            fwrite(pop_buf, 1, popped, fout);
                            total_pcm_bytes += popped;
                            can_pop -= popped;
                        }
                    }
                }
                frames_decoded++;
            }
        }
    } else {
        uint32_t offset = 0;
        while (offset < (uint32_t)file_len) {
            uint32_t bytes_consumed = 0;
            uint32_t bytes_written = 0;

            faad_frame_info finfo;
            st = faad_decode_frame(dec, inbuf + offset, file_len - offset,
                                   &bytes_consumed, outbuf, sizeof(outbuf), &bytes_written, &finfo);

            if (st != FAAD_OK) {
                if (st == FAAD_ERR_NEED_MORE_DATA || bytes_consumed == 0) {
                    break;
                }
                if (strict_mode) {
                    print_strict_error(infile, offset, frames_decoded, st);
                    faad_decoder_close(&dec);
                    free(inbuf);
                    return 1;
                }
                offset += bytes_consumed; /* resync distance on SYNC_LOST */
                continue;
            }

            sample_rate = finfo.sample_rate;
            num_channels = finfo.channels;
            if (header_pending) {
                write_wav_header(fout, sample_rate, (uint16_t)num_channels, 0, bit_depth, is_float);
                header_channels = (uint16_t)num_channels;
                header_pending = false;
            }
            obj_type = finfo.ps_active ? FAAD_OBJ_HE_AAC_V2 : finfo.sbr_active ? FAAD_OBJ_HE_AAC_V1 : FAAD_OBJ_LC;

            if (fout && bytes_written > 0) {
                pcm_to_little_endian(outbuf, bytes_written, is_float ? 4 : bit_depth / 8);
                fifo_push(&fifo, outbuf, bytes_written);

                uint8_t pop_buf[4096];
                while (fifo.fill > 0) {
                    uint32_t chunk = fifo.fill < sizeof(pop_buf) ? fifo.fill : sizeof(pop_buf);
                    uint32_t popped = fifo_pop(&fifo, pop_buf, chunk);
                    if (popped == 0) break;
                    fwrite(pop_buf, 1, popped, fout);
                    total_pcm_bytes += popped;
                }
            }

            frames_decoded++;
            offset += bytes_consumed;
        }
    }

    if (fout) {
        if (is_mp4 && gapless && padding_samples > 0) {
            uint32_t padding_bytes = padding_samples * num_channels * (bit_depth / 8);
            if (fifo.fill > padding_bytes) {
                fifo_truncate_tail(&fifo, padding_bytes);
            } else {
                fifo.fill = 0;
            }
        }
        uint8_t pop_buf[4096];
        while (fifo.fill > 0) {
            uint32_t chunk = fifo.fill < sizeof(pop_buf) ? fifo.fill : sizeof(pop_buf);
            uint32_t popped = fifo_pop(&fifo, pop_buf, chunk);
            if (popped == 0) break;
            fwrite(pop_buf, 1, popped, fout);
            total_pcm_bytes += popped;
        }
    }
    fifo_free(&fifo);

    double duration_sec = (double)(frames_decoded * (obj_type == FAAD_OBJ_HE_AAC_V1 ? 2048 : 1024)) / (sample_rate ? sample_rate : 44100);
    double avg_bitrate_kbps = (file_len * 8.0) / (duration_sec > 0 ? duration_sec * 1000.0 : 1.0);

    const char *brand = track.major_brand[0] ? track.major_brand : "M4A";
    if (json_info) {
        printf("{\n");
        printf("  \"file\": ");
        json_str(infile);
        printf(",\n  \"container\": \"%s\",\n", is_mp4 ? "MP4 / M4A" : "ADTS Bitstream");
        if (is_mp4) printf("  \"major_brand\": \"%s\",\n", brand);
        printf("  \"duration_seconds\": %.2f,\n", duration_sec);
        printf("  \"audio\": {\n");
        printf("    \"profile\": \"%s\",\n", (obj_type == FAAD_OBJ_HE_AAC_V1) ? "HE-AAC v1 (AAC-LC + SBR)" : "AAC-LC");
        printf("    \"channels\": %u,\n", num_channels);
        printf("    \"sample_rate_hz\": %u,\n", sample_rate);
        printf("    \"bitrate_avg_kbps\": %.1f,\n", avg_bitrate_kbps);
        printf("    \"total_frames\": %u\n", frames_decoded);
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
        printf("Audio Stream:\n");
        printf("  Profile:   %s\n", (obj_type == FAAD_OBJ_HE_AAC_V1) ? "HE-AAC v1 (AAC-LC + SBR)" : "AAC-LC");
        printf("  Channels:  %u (%s)\n", num_channels, (num_channels == 1) ? "Mono" : ((num_channels == 2) ? "Stereo" : "Multichannel"));
        printf("  Sample Rate: %.1f kHz\n", sample_rate / 1000.0f);
        printf("  Bitrate:   %.1f kbps (Avg)\n", avg_bitrate_kbps);
        printf("  Frames:    %u frames\n", frames_decoded);
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
    } else if (fout) {
        if (!raw_format && fout != stdout) {
            if (!header_pending) num_channels = header_channels;
            write_wav_header(fout, sample_rate, (uint16_t)num_channels, total_pcm_bytes, bit_depth, is_float);
            fclose(fout);
        }
        if (!quiet) {
            printf("Decoded %u frames (%u bytes, %d-bit %s) to %s\n",
                   frames_decoded, total_pcm_bytes, bit_depth, is_float ? "float" : "PCM", outfile ? outfile : "stdout");
        }
    }

    faad_decoder_close(&dec);
    free(inbuf);
    if (is_mp4) mp4_free_track(&track);

#ifdef _WIN32
    if (allocated_argv) {
        for (int i = 0; i < argc; i++) {
            if (allocated_argv[i]) free(allocated_argv[i]);
        }
        free(allocated_argv);
    }
#endif

    return 0;
}
