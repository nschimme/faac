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
#include <io.h>
#include <fcntl.h>
#endif

#ifdef HAVE_GETOPT_H
# include <getopt.h>
#else
# include "getopt.h"
# include "getopt.c"
#endif

#include "faad.h"
#include "adts.h"
#include "charset.h"
#include "cli_io.h"
#include "endian.h"
#include "asc_codec.h"
#include "mp4read.h"
#include "help.h"

/* The ADTS and frame loops index the input with 32 bits. */
#define FAAD_INPUT_MAX ((size_t)0xFFFFFFF0u)

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
    if (f->fill + len > f->size) {
        uint32_t new_size = f->size > 0 ? f->size * 2 : 65536;
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

/* libfaad emits host-order samples; WAV wants little-endian. */
static void pcm_to_little_endian(uint8_t *buf, uint32_t bytes, uint32_t sample_bytes, bool int24_in_32)
{
    if (int24_in_32) {
        /* WAV integer PCM is left-aligned, inverse to the FAAC WAV reader. */
        for (uint32_t i = 0; i + 4 <= bytes; i += 4) {
            uint32_t v;
            memcpy(&v, buf + i, 4);
            v <<= 8;
            memcpy(buf + i, &v, 4);
        }
    }
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

enum {
    OPT_NO_GAPLESS = 300,
    OPT_JSON,
    OPT_TRACK,
    OPT_STRICT,
    OPT_OVERWRITE,
    OPT_LICENSE,
    HELP_IO,
    HELP_PROCESSING,
    HELP_INFO
};

static const help_t help_io[] = {
    {"-o, --output <file>",
     "Set output filename (default: infile.wav, or stdout for stdin input)", NULL},
    {"-w, --stdout",
     "Write output PCM to stdout", NULL},
    {"-f, --format <type>",
     "Output container format: wav (default), raw", NULL},
    {"-b, --bits <depth>",
     "Sample depth: 16 (default), 24, 32, 32f (32-bit float)", NULL},
    {"-a, --adts <file>",
     "Extract raw ADTS stream from MP4 without decoding", NULL},
    {NULL, NULL, NULL}
};

static const help_t help_processing[] = {
    {"-d, --downmix [mode]",
     "Downmix audio (mono/1 or stereo/2, default: mono)", NULL},
    {"-j, --jump <seconds>",
     "Start decoding from specified timestamp", NULL},
    {"-g, --no-gapless",
     "Disable automatic gapless trim/padding handling", NULL},
    {NULL, NULL, NULL}
};

static const help_t help_info[] = {
    {"-i, --info",
     "Display bitstream & container metadata, then exit", NULL},
    {"--json",
     "Output bitstream info in JSON format", NULL},
    {"--track <id>",
     "MP4: decode the AAC track with this ID (default: the first AAC track); -i lists the tracks", NULL},
    {"-q, --quiet",
     "Quiet mode (suppress decoding progress)", NULL},
    {"--overwrite",
     "Overwrite an existing output file", NULL},
    {"--strict",
     "Strict mode (noisily error and report debug details on failure)", NULL},
    {NULL, NULL, NULL}
};

static const help_group_t g_help[] = {
    {HELP_IO, "I/O and format options", "--help-io", help_io, 1},
    {HELP_PROCESSING, "Processing options", "--help-processing", help_processing, 1},
    {HELP_INFO, "Information and general options", "--help-info", help_info, 1},
    {0, NULL, NULL, NULL, 0}
};

static void print_usage(int mode)
{
    faad_library_info info;
    info.struct_size = sizeof(info);
    if (faad_get_library_info(&info) != FAAD_OK) {
        info.version = "3.0.0";
    }

    char version_buf[128];
    show_help_usage("faad", mode,
                    cli_version_string(version_buf, sizeof(version_buf), info.version),
                    "faad [options] <infile.aac|infile.m4a>", g_help);
}

static void print_strict_error(const char *filename, uint64_t offset, uint32_t frame_idx, faad_status st)
{
    fprintf(stderr, "%s:0x%04llx: frame %u: error %d (%s)\n",
            filename ? filename : "input", (unsigned long long)offset, frame_idx, st, faad_strerror(st));
}

int main(int argc, char **argv)
{
    cli_init_console();
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
    uint32_t want_track = 0;
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
        {"track", required_argument, 0, OPT_TRACK},
        {"quiet", no_argument, 0, 'q'},
        {"strict", no_argument, 0, OPT_STRICT},
        {"overwrite", no_argument, 0, OPT_OVERWRITE},
        {"license", no_argument, 0, OPT_LICENSE},
        {"help", no_argument, 0, 'h'},
        {"help-io", no_argument, 0, HELP_IO},
        {"help-processing", no_argument, 0, HELP_PROCESSING},
        {"help-info", no_argument, 0, HELP_INFO},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "o:wf:b:a:d::j:giqhH", long_options, &option_index)) != -1) {
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
            else if (strcmp(optarg, "3") == 0 || strcmp(optarg, "32") == 0) { bit_depth = 32; is_float = false; }
            else if (strcmp(optarg, "4") == 0 || strcmp(optarg, "32f") == 0) { bit_depth = 32; is_float = true; }
            else { fprintf(stderr, "Unknown bit depth '%s' (16, 24, 32 or 32f)\n", optarg); return 1; }
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
        case OPT_TRACK: {
            char *end;
            unsigned long v = strtoul(optarg, &end, 10);
            if (*optarg == '\0' || *end != '\0' || v == 0 || v > UINT32_MAX) {
                fprintf(stderr, "Invalid --track %s (a track ID, 1 or more)\n", optarg);
                return 1;
            }
            want_track = (uint32_t)v;
            break;
        }
        case 'q': quiet = true; break;
        case OPT_STRICT: strict_mode = true; break;
        case OPT_OVERWRITE: overwrite = true; break;
        case OPT_LICENSE: {
            faad_library_info info;
            info.struct_size = sizeof(info);
            if (faad_get_library_info(&info) == FAAD_OK && info.copyright)
                fprintf(stderr, "%s", info.copyright);
            cli_print_patent_notice(stderr);
            cli_print_lgpl_notice(stderr, "library");
            return 0;
        }
        case 'h':
        case 'H':
        case HELP_IO:
        case HELP_PROCESSING:
        case HELP_INFO: print_usage(opt); return 0;
        default: print_usage('h'); return 1;
        }
    }

    if (optind < argc) {
        infile = argv[optind];
    }

    if (!infile) {
        print_usage('h');
        return 1;
    }

    FILE *fin = NULL;
    uint8_t *inbuf = NULL;
    size_t file_len = 0;

    if (strcmp(infile, "-") == 0) {
#ifdef _WIN32
        /* Text-mode translation changes AAC bytes and treats Ctrl-Z as EOF. */
        _setmode(_fileno(stdin), _O_BINARY);
#endif
        fin = stdin;
        if (!outfile && !write_stdout && !info_only && !adts_outfile) {
            write_stdout = true;
        }
        if (!cli_read_all(fin, FAAD_INPUT_MAX, &inbuf, &file_len)) {
            fprintf(stderr, "Error reading input\n");
            return 1;
        }
    } else {
        fin = cli_fopen(infile, "rb");
        if (!fin) {
            fprintf(stderr, "Error opening input file %s\n", infile);
            return 1;
        }

        bool read_ok = cli_read_all(fin, FAAD_INPUT_MAX, &inbuf, &file_len);
        fclose(fin);
        if (!read_ok) {
            fprintf(stderr, "Error reading input file\n");
            return 1;
        }
    }

    MP4Track track;
    memset(&track, 0, sizeof(track));
    bool is_mp4 = mp4_read_track_buf(inbuf, file_len, want_track, &track);
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
        if (!overwrite && cli_file_exists(adts_outfile)) {
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
        adts_params adts;
        asc_codec_parse(track.asc_buf, track.asc_len, &asc);
        if (adts_params_from_asc(&asc, true, &adts)) {
            fprintf(stderr, "%s: core sample rate %u Hz has no ADTS sampling frequency index\n",
                    infile, (unsigned)asc.sample_rate);
            fclose(fadts);
            remove(adts_outfile);
            free(inbuf);
            mp4_free_track(&track);
            return 1;
        }
        for (uint32_t s = 0; s < track.num_samples; s++) {
            uint64_t offset = track.samples[s].offset;
            uint32_t size = track.samples[s].size;
            uint32_t frame_len = size + ADTS_HEADER_SIZE;
            if (offset > 0 && offset + size <= (uint64_t)file_len && frame_len <= ADTS_MAX_FRAME) {
                uint8_t adts_hdr[ADTS_HEADER_SIZE];
                adts_write_header(&adts, size, adts_hdr);
                fwrite(adts_hdr, 1, sizeof(adts_hdr), fadts);
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
    cfg.output_format = is_float ? FAAD_OUTPUT_FLOAT : bit_depth == 32 ? FAAD_OUTPUT_32BIT : bit_depth == 24 ? FAAD_OUTPUT_24BIT : FAAD_OUTPUT_16BIT;
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
#ifdef _WIN32
            /* PCM samples can contain newline bytes; preserve them verbatim. */
            _setmode(_fileno(stdout), _O_BINARY);
#endif
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
            if (!overwrite && cli_file_exists(outfile)) {
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

    char *io_buf = NULL;
    if (fout && fout != stdout) {
        io_buf = (char *)malloc(65536);
        if (io_buf) {
            if (setvbuf(fout, io_buf, _IOFBF, 65536) != 0) {
                free(io_buf);
                io_buf = NULL;
            }
        }
    }

    _Alignas(float) uint8_t outbuf[65536];
    uint32_t total_pcm_bytes = 0;
    uint32_t sample_rate = 44100;
    uint32_t num_channels = 2;
    enum faad_object_type obj_type = FAAD_OBJ_LC;
    uint32_t frames_decoded = 0;
    faad_stream_info sinfo = { .struct_size = sizeof(sinfo) };

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

    /* Convert container priming from track ticks to output samples; decoder
     * metadata already supplies its additional delay in output samples. */
    uint32_t samples_to_skip = (is_mp4 && gapless) ? track.delay : 0;
    uint32_t padding_samples = (is_mp4 && gapless) ? track.padding : 0;
    bool gapless_scaled = false;
    uint32_t padding_bytes = 0;
    bool fifo_initialized = false;
    bool fallback_mode = false;
    PCMFifo fifo = {0};

    if (is_mp4) {
        for (uint32_t s = start_frame; s < track.num_samples; s++) {
            uint64_t offset = track.samples[s].offset;
            uint32_t size = track.samples[s].size;
            if (offset == 0 || offset + size > (uint64_t)file_len) continue;

            uint32_t bytes_consumed = 0;
            uint32_t bytes_written = 0;

            uint32_t flags;
            st = faad_decode_frame(dec, inbuf + offset, size,
                                   &bytes_consumed, outbuf, sizeof(outbuf), &bytes_written, &flags);

            if (st == FAAD_OK && strict_mode && (flags & (FAAD_FRAME_CONCEALED | FAAD_FRAME_DEGRADED)))
                st = FAAD_ERR_DECODE_FAILED;
            if (st != FAAD_OK) {
                if (strict_mode) {
                    print_strict_error(infile, offset, s, st);
                    faad_decoder_close(&dec);
                    free(inbuf);
                    mp4_free_track(&track);
                    if (fout && fout != stdout) fclose(fout);
                    if (io_buf) free(io_buf);
                    return 1;
                }
            } else if (bytes_written > 0) {
                if (flags & FAAD_FRAME_FORMAT_CHANGED) {
                    faad_decoder_get_info(dec, &sinfo);
                    if (frames_decoded > 0 && (sinfo.sample_rate != sample_rate || sinfo.channels != num_channels)) {
                        if (is_mp4 && !fallback_mode) {
                            fallback_mode = true;
                            if (!fifo_initialized) {
                                if (!fifo_init(&fifo, 262144)) {
                                    fprintf(stderr, "Error allocating PCM buffer\n");
                                    faad_decoder_close(&dec);
                                    free(inbuf);
                                    mp4_free_track(&track);
                                    if (fout && fout != stdout) fclose(fout);
                                    if (io_buf) free(io_buf);
                                    fifo_free(&fifo);
                                    return 1;
                                }
                                fifo_initialized = true;
                            } else if (fifo.size < 262144) {
                                uint8_t *new_data = (uint8_t *)malloc(262144);
                                if (!new_data) {
                                    fprintf(stderr, "Error allocating PCM buffer\n");
                                    faad_decoder_close(&dec);
                                    free(inbuf);
                                    mp4_free_track(&track);
                                    if (fout && fout != stdout) fclose(fout);
                                    if (io_buf) free(io_buf);
                                    fifo_free(&fifo);
                                    return 1;
                                }
                                if (fifo.fill > 0) {
                                    if (fifo.tail < fifo.head) {
                                        memcpy(new_data, fifo.data + fifo.tail, fifo.fill);
                                    } else {
                                        uint32_t first = fifo.size - fifo.tail;
                                        memcpy(new_data, fifo.data + fifo.tail, first);
                                        memcpy(new_data + first, fifo.data, fifo.head);
                                    }
                                }
                                free(fifo.data);
                                fifo.data = new_data;
                                fifo.size = 262144;
                                fifo.tail = 0;
                                fifo.head = fifo.fill;
                            }
                        }
                    }
                    sample_rate = sinfo.sample_rate;
                    num_channels = sinfo.channels;
                }
                if (header_pending) {
                    write_wav_header(fout, sample_rate, (uint16_t)num_channels, 0, bit_depth, is_float);
                    header_channels = (uint16_t)num_channels;
                    header_pending = false;
                }
                obj_type = (flags & FAAD_FRAME_PS) ? FAAD_OBJ_HE_AAC_V2 : (flags & FAAD_FRAME_SBR) ? FAAD_OBJ_HE_AAC_V1 : FAAD_OBJ_LC;

                uint32_t dec_bytes_per_sample = is_float ? 4 : bit_depth / 8;
                uint32_t dec_bytes_per_frame_sample = num_channels * dec_bytes_per_sample;
                pcm_to_little_endian(outbuf, bytes_written, dec_bytes_per_sample, !raw_format && !is_float && bit_depth == 32);
                uint32_t frame_samples = bytes_written / dec_bytes_per_frame_sample;
                if (!gapless_scaled) {
                    uint32_t core_rate = (flags & FAAD_FRAME_SBR) && sinfo.frame_samples == 2048
                        ? sinfo.sample_rate / 2 : sinfo.sample_rate;
                    uint32_t timescale = track.timescale ? track.timescale : core_rate;
                    uint32_t delay = gapless ? sinfo.decoder_delay : 0;
                    samples_to_skip = (uint32_t)((uint64_t)samples_to_skip * sinfo.sample_rate / timescale) + delay;
                    padding_samples = (uint32_t)((uint64_t)padding_samples * sinfo.sample_rate / timescale);
                    padding_samples = padding_samples > delay ? padding_samples - delay : 0;
                    gapless_scaled = true;

                    uint64_t pad_bytes_64 = (uint64_t)padding_samples * dec_bytes_per_frame_sample;
                    if (pad_bytes_64 > 64 * 1024 * 1024) {
                        fprintf(stderr, "Error: gapless trailing padding exceeds maximum allowed buffer size\n");
                        faad_decoder_close(&dec);
                        free(inbuf);
                        mp4_free_track(&track);
                        if (fout && fout != stdout) fclose(fout);
                        if (io_buf) free(io_buf);
                        fifo_free(&fifo);
                        return 1;
                    }
                    padding_bytes = (uint32_t)pad_bytes_64;

                    if (padding_bytes > 0 && !fallback_mode) {
                        if (!fifo_init(&fifo, padding_bytes)) {
                            fprintf(stderr, "Error allocating gapless padding buffer\n");
                            faad_decoder_close(&dec);
                            free(inbuf);
                            mp4_free_track(&track);
                            if (fout && fout != stdout) fclose(fout);
                            if (io_buf) free(io_buf);
                            fifo_free(&fifo);
                            return 1;
                        }
                        fifo_initialized = true;
                    }
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
                    uint32_t frame_bytes = samples_to_write * dec_bytes_per_frame_sample;

                    if (fallback_mode) {
                        padding_bytes = padding_samples * num_channels * (bit_depth / 8);
                    }

                    if (padding_bytes == 0 && !fallback_mode) {
                        fwrite(write_ptr, 1, frame_bytes, fout);
                        total_pcm_bytes += frame_bytes;
                    } else if (!fallback_mode) {
                        uint32_t total_avail = fifo.fill + frame_bytes;
                        if (total_avail > padding_bytes) {
                            uint32_t safe_bytes = total_avail - padding_bytes;
                            if (fifo.fill > 0) {
                                uint32_t from_ring = (fifo.fill < safe_bytes) ? fifo.fill : safe_bytes;
                                total_pcm_bytes += fifo_write(fout, &fifo, from_ring);
                                safe_bytes -= from_ring;
                            }
                            if (safe_bytes > 0) {
                                fwrite(write_ptr, 1, safe_bytes, fout);
                                total_pcm_bytes += safe_bytes;
                                write_ptr += safe_bytes;
                                frame_bytes -= safe_bytes;
                            }
                        }
                        if (frame_bytes > 0) {
                            if (!fifo_push(&fifo, write_ptr, frame_bytes)) {
                                fprintf(stderr, "Error allocating PCM buffer\n");
                                faad_decoder_close(&dec);
                                free(inbuf);
                                mp4_free_track(&track);
                                if (fout && fout != stdout) fclose(fout);
                                if (io_buf) free(io_buf);
                                fifo_free(&fifo);
                                return 1;
                            }
                        }
                    } else {
                        if (!write_stdout && frame_bytes > fifo.size - fifo.fill &&
                            fifo.fill > padding_bytes) {
                            total_pcm_bytes += fifo_write(fout, &fifo, fifo.fill - padding_bytes);
                        }
                        if (!fifo_push(&fifo, write_ptr, frame_bytes)) {
                            fprintf(stderr, "Error allocating PCM buffer\n");
                            faad_decoder_close(&dec);
                            free(inbuf);
                            mp4_free_track(&track);
                            if (fout && fout != stdout) fclose(fout);
                            if (io_buf) free(io_buf);
                            fifo_free(&fifo);
                            return 1;
                        }

                        if (fifo.fill > padding_bytes) {
                            uint32_t can_pop = fifo.fill - padding_bytes;
                            if (write_stdout) {
                                uint8_t pop_buf[4096];
                                while (can_pop > 0) {
                                    uint32_t chunk = can_pop < sizeof(pop_buf) ? can_pop : sizeof(pop_buf);
                                    uint32_t popped = fifo_pop(&fifo, pop_buf, chunk);
                                    if (popped == 0) break;
                                    fwrite(pop_buf, 1, popped, fout);
                                    total_pcm_bytes += popped;
                                    can_pop -= popped;
                                }
                            } else if (can_pop >= 64u * 1024u) {
                                total_pcm_bytes += fifo_write(fout, &fifo, can_pop);
                            }
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

            uint32_t flags;
            st = faad_decode_frame(dec, inbuf + offset, file_len - offset,
                                   &bytes_consumed, outbuf, sizeof(outbuf), &bytes_written, &flags);

            if (st == FAAD_OK && strict_mode && (flags & (FAAD_FRAME_CONCEALED | FAAD_FRAME_DEGRADED)))
                st = FAAD_ERR_DECODE_FAILED;
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

            if (!bytes_written) {
                offset += bytes_consumed;
                continue;
            }
            if (flags & FAAD_FRAME_FORMAT_CHANGED) {
                faad_decoder_get_info(dec, &sinfo);
                sample_rate = sinfo.sample_rate;
                num_channels = sinfo.channels;
            }
            if (header_pending) {
                write_wav_header(fout, sample_rate, (uint16_t)num_channels, 0, bit_depth, is_float);
                header_channels = (uint16_t)num_channels;
                header_pending = false;
            }
            obj_type = (flags & FAAD_FRAME_PS) ? FAAD_OBJ_HE_AAC_V2 : (flags & FAAD_FRAME_SBR) ? FAAD_OBJ_HE_AAC_V1 : FAAD_OBJ_LC;

            if (fout && bytes_written > 0) {
                pcm_to_little_endian(outbuf, bytes_written, is_float ? 4 : bit_depth / 8, !raw_format && !is_float && bit_depth == 32);
                fwrite(outbuf, 1, bytes_written, fout);
                total_pcm_bytes += bytes_written;
            }

            frames_decoded++;
            offset += bytes_consumed;
        }
    }

    if (fout && is_mp4) {
        if (gapless && padding_samples > 0) {
            uint32_t padding_bytes = padding_samples * num_channels * (bit_depth / 8);
            if (fifo.fill > padding_bytes) {
                fifo_truncate_tail(&fifo, padding_bytes);
            } else {
                fifo.fill = 0;
            }
        }
        if (fallback_mode) {
            if (!write_stdout) {
                total_pcm_bytes += fifo_write(fout, &fifo, fifo.fill);
            } else {
                uint8_t pop_buf[4096];
                while (fifo.fill > 0) {
                    uint32_t chunk = fifo.fill < sizeof(pop_buf) ? fifo.fill : sizeof(pop_buf);
                    uint32_t popped = fifo_pop(&fifo, pop_buf, chunk);
                    if (popped == 0) break;
                    fwrite(pop_buf, 1, popped, fout);
                    total_pcm_bytes += popped;
                }
            }
        }
    }
    fifo_free(&fifo);

    double duration_sec = (double)(frames_decoded * (obj_type == FAAD_OBJ_HE_AAC_V1 ? 2048 : 1024)) / (sample_rate ? sample_rate : 44100);
    double avg_bitrate_kbps = (file_len * 8.0) / (duration_sec > 0 ? duration_sec * 1000.0 : 1.0);

    if (fout && fout != stdout) {
        if (!raw_format) {
            if (!header_pending) num_channels = header_channels;
            write_wav_header(fout, sample_rate, (uint16_t)num_channels, total_pcm_bytes, bit_depth, is_float);
        }
        fclose(fout);
        fout = NULL;
    }
    if (io_buf) {
        free(io_buf);
        io_buf = NULL;
    }

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
    } else {
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
