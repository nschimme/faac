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
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#endif

#ifdef HAVE_GETOPT_H
# include <getopt.h>
#else
# include "getopt.h"
# include "getopt.c"
#endif

#include "faad3.h"
#include "charset.h"
#include "cli_io.h"
#include "cli_parse.h"
#include "decode_engine.h"
#include "help.h"

enum {
    OPT_NO_GAPLESS = 300,
    OPT_JSON,
    OPT_TRACK,
    OPT_STRICT,
    OPT_OVERWRITE,
    OPT_LICENSE,
    OPT_DRC_CUT,
    OPT_DRC_BOOST,
    OPT_DRC_TARGET,
    HELP_IO,
    HELP_PROCESSING,
    HELP_INFO
};

static const help_t help_io[] = {
    {"-o, --output <file>",
     "Set output filename, - for stdout (default: infile.wav, or stdout for stdin input)",
     "The output extension does not select the encoding format; use --format for WAV or raw PCM. When input is stdin and no output is named, decoded WAV is written to stdout."},
    {"-w, --stdout",
     "Write output PCM to stdout",
     "This is shorthand for selecting stdout as the destination. Progress is suppressed automatically when PCM goes to stdout; diagnostics remain on stderr."},
    {"-f, --format <type>",
     "Output container format: wav (default, automatic RF64 for large files), raw",
     "Raw output contains samples only, with no header. Seekable WAV output switches to RF64 when it would exceed the classic RIFF size limit."},
    {"-b, --bits <depth>",
     "Sample depth: 16 (default), 24, 32, 32f (32-bit float)",
     "Integer depths are signed PCM; 32 is 24-bit integer audio stored in 32-bit samples. 32f writes IEEE 32-bit floating point samples."},
    {"-a, --adts <file>",
     "Extract raw ADTS stream from MP4 without decoding",
     "The selected AAC track's packets are copied and ADTS headers are reconstructed from its codec configuration. This option does not decode audio and requires MP4 input with an AAC track."},
    {NULL, NULL, NULL}
};

static const help_t help_processing[] = {
    {"--drc-cut N, --drc-boost N", "Dynamic Range Control (DRC) strength 0..127 (default: 0)",
     "These are separate controls for attenuating loud passages and boosting quiet passages when the stream carries DRC metadata. Both default to zero, so DRC is off unless requested."},
    {"--drc-target N", "Dynamic Range Control target: 0 off, 40..127 quarter-dB below FS (96 = -24 dBFS)",
     "The target controls level normalization when DRC metadata is present. Zero leaves normalization off; accepted nonzero targets are 40 through 127."},
    {"-d, --downmix [mode]",
     "Downmix audio (mono/1 or stereo/2, default: mono)",
     "Choose one or two output channels. Without a mode, mono is selected. This affects decoded PCM, not ADTS extraction."},
    {"-j, --jump <seconds>",
     "Start decoding from specified timestamp",
     "Only applies to MP4 input. A timestamp past the end produces an empty output; ADTS input starts at the beginning."},
    {"-g, --no-gapless",
     "Disable automatic gapless trim/padding handling",
     "By default, MP4 encoder delay and end padding are applied so decoded output matches the intended audible duration. This option keeps those samples."},
    {NULL, NULL, NULL}
};

static const help_t help_info[] = {
    {"-i, --info",
     "Display bitstream & container metadata, then exit",
     "For MP4, reports available track and codec information. Use --track to choose an AAC track when the file contains more than one."},
    {"--json",
     "Output bitstream info in JSON format",
     "Emits the information report as JSON for scripts. This is an inspection mode and does not write decoded PCM."},
    {"--track <id>",
     "MP4: decode the AAC track with this ID (default: the first AAC track); -i lists the tracks",
     "Track IDs are the IDs reported by the MP4 container, not zero-based positions. The option also selects the track used by inspection and ADTS extraction."},
    {"-v <verbose>",
     "Set verbosity level: -v0 is quiet; -v1 (default) shows progress; -v2 adds a detailed summary.",
     "Progress and diagnostic text are written to stderr so decoded audio can be piped through stdout."},
    {"-q, --quiet",
     "Same as -v0", "Suppresses the progress display and non-error informational output."},
    {"--overwrite",
     "Overwrite an existing output file",
     "Without this option, FAAD refuses to replace an existing output. Input and output paths that refer to the same file are always rejected."},
    {"--strict",
     "Strict mode (noisily error and report debug details on failure)",
     "Treats concealed or degraded frames as decode failures, reports the input offset and frame number, and exits with status 1. Without strict mode, recoverable audio is kept and decoding continues where framing permits."},
    {NULL, NULL, NULL}
};

static const help_group_t g_help[] = {
    {HELP_IO, "I/O and format options", "--help-io", help_io, 1},
    {HELP_PROCESSING, "Processing options", "--help-processing", help_processing, 1},
    {HELP_INFO, "Information and general options", "--help-info", help_info, 1},
    {0, NULL, NULL, NULL, 0}
};

#ifndef _WIN32
volatile int running = 1;
static void signal_handler(int signal)
{
    (void)signal;
    running = 0;
}
#endif

/* Set once in main from the linked library. */
static const char *g_lib_version;

static void print_usage(int mode)
{
    char version_buf[128];
    show_help_usage("faad", mode,
                    cli_version_string(version_buf, sizeof(version_buf), g_lib_version),
                    "faad [options] <infile.aac|infile.m4a>", g_help);
}

static bool cli_progress_callback(const progress_info_t *info, void *user_data)
{
    decode_options_t *opts = (decode_options_t *)user_data;
    if (opts && opts->verbose == 0)
    {
#ifndef _WIN32
        return running != 0;
#else
        return true;
#endif
    }

    static progress_throttle_t s_throttle = { .last_fired_sec = -1.0 };
    if (info->is_final || progress_throttle_tick(&s_throttle, info, 0.033))
        progress_render_cli(info);

#ifndef _WIN32
    return running != 0;
#else
    return true;
#endif
}

static void cli_log_callback(int level, const char *message, void *user_data)
{
    decode_options_t *opts = (decode_options_t *)user_data;
    if (opts && (int)opts->verbose >= level)
    {
        fprintf(stderr, "%s", message);
    }
}

static void cli_session_start_callback(const decode_session_info_t *info, void *user_data)
{
    decode_options_t *opts = (decode_options_t *)user_data;
    if (!opts || opts->verbose < 1 || opts->write_stdout || opts->info_only)
        return;

    fprintf(stderr, "Container:   %s\n", info->is_mp4 ? "MPEG-4 File Format (MP4)" : "ADTS Transport Stream");
    if (info->profile_name && info->profile_name[0]) {
        fprintf(stderr, "Audio:       %s, %.1f kHz, %u channel%s\n",
                info->profile_name, info->sample_rate / 1000.0f,
                info->num_channels, info->num_channels == 1 ? " (Mono)" : info->num_channels == 2 ? "s (Stereo)" : "s");
    }
    if (info->duration_sec > 0.0) {
        int dur_m = (int)info->duration_sec / 60;
        double dur_s = info->duration_sec - (dur_m * 60);
        if (info->avg_bitrate_kbps > 0.0) {
            fprintf(stderr, "Duration:    %02d:%05.2f (%.2f sec), ~%.1f kbps\n",
                    dur_m, dur_s, info->duration_sec, info->avg_bitrate_kbps);
        } else {
            fprintf(stderr, "Duration:    %02d:%05.2f (%.2f sec)\n",
                    dur_m, dur_s, info->duration_sec);
        }
    }

    const char *out_fmt = info->raw_format ? "raw PCM" : "WAV";
    fprintf(stderr, "Decoding %s to %s (%u-bit %s%s)\n",
            info->input_filename,
            info->output_filename ? info->output_filename : "stdout",
            info->bit_depth, info->is_float ? "float " : "", out_fmt);

    if (info->total_track_frames > 0)
    {
        fprintf(stderr, "         frame         | bitrate | elapsed/estim | play/CPU | ETA\n");
    }
    else
    {
        fprintf(stderr, "  frame  | elapsed | play/CPU\n");
    }
}

static void cli_summary_callback(const decode_summary_t *summary, void *user_data)
{
    decode_options_t *opts = (decode_options_t *)user_data;
    if (!opts || opts->verbose < 2 || opts->write_stdout)
        return;

    fprintf(stderr, "\n--- Decode Summary ---\n");
    fprintf(stderr, " Frames & Samples    : Frames = %" PRIu64 " | Samples = %" PRIu64 "\n",
            summary->frame_count, summary->sample_count);
    fprintf(stderr, " Bitrate & Frame Size: Avg = %u kbps | Max = %u kbps | Max Frame Size = %u bytes\n",
            (uint32_t)(summary->avg_bitrate_kbps + 0.5), summary->max_bitrate_kbps, summary->max_frame_bytes);

    uint64_t input_size = 0;
    FILE *fin_stat = cli_fopen(opts->input_filename, "rb");
    if (fin_stat) {
        cli_fsize(fin_stat, &input_size);
        fclose(fin_stat);
    }
    uint64_t output_size = summary->total_pcm_bytes;
    if (input_size > 0 && output_size > 0)
    {
        fprintf(stderr, " File Size           : Input = %.2f MB | Output = %.2f MB | Ratio = %.1f:1\n",
                input_size / (1024.0 * 1024.0), output_size / (1024.0 * 1024.0),
                (double)output_size / (double)input_size);
    }
    fprintf(stderr, "----------------------\n");
}

int main(int argc, char **argv)
{
    cli_init_console();

    faad_library_info libinfo = { .struct_size = sizeof(libinfo) };
    if (faad_get_library_info(&libinfo) != FAAD_OK) {
        fprintf(stderr, "Wrong libfaad version!\n");
        return 1;
    }
    g_lib_version = libinfo.version;

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

#ifndef _WIN32
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#endif

    decode_options_t opts;
    init_decode_options(&opts);

    char *allocated_outfile = NULL;

    static struct option long_options[] = {
        {"output", required_argument, 0, 'o'},
        {"stdout", no_argument, 0, 'w'},
        {"format", required_argument, 0, 'f'},
        {"bits", required_argument, 0, 'b'},
        {"adts", required_argument, 0, 'a'},
        {"drc-cut", required_argument, 0, OPT_DRC_CUT},
        {"drc-boost", required_argument, 0, OPT_DRC_BOOST},
        {"drc-target", required_argument, 0, OPT_DRC_TARGET},
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
    while ((opt = getopt_long(argc, argv, "o:wf:b:a:d::j:giqv:hH", long_options, &option_index)) != -1) {
        switch (opt) {
        case OPT_DRC_CUT:
        case OPT_DRC_BOOST:
        case OPT_DRC_TARGET: {
            char *end;
            long value = strtol(optarg, &end, 10);
            if (!*optarg || *end || value < 0 || value > 127
                || (opt == OPT_DRC_TARGET && value && value < 40)) {
                fprintf(stderr, "Invalid DRC value '%s'\n", optarg);
                return 1;
            }
            if (opt == OPT_DRC_CUT) opts.drc_cut = (uint8_t)value;
            else if (opt == OPT_DRC_BOOST) opts.drc_boost = (uint8_t)value;
            else opts.drc_target = (uint8_t)value;
            break;
        }
        case 'o': opts.output_filename = optarg; break;
        case 'w': opts.write_stdout = true; break;
        case 'f':
            if (strcmp(optarg, "raw") == 0) opts.raw_format = true;
            else if (strcmp(optarg, "wav") != 0) { fprintf(stderr, "Unknown format '%s' (wav or raw)\n", optarg); return 1; }
            break;
        case 'b':
            if (strcmp(optarg, "1") == 0 || strcmp(optarg, "16") == 0) { opts.bit_depth = 16; opts.is_float = false; }
            else if (strcmp(optarg, "2") == 0 || strcmp(optarg, "24") == 0) { opts.bit_depth = 24; opts.is_float = false; }
            else if (strcmp(optarg, "3") == 0 || strcmp(optarg, "32") == 0) { opts.bit_depth = 32; opts.is_float = false; }
            else if (strcmp(optarg, "4") == 0 || strcmp(optarg, "32f") == 0) { opts.bit_depth = 32; opts.is_float = true; }
            else { fprintf(stderr, "Unknown bit depth '%s' (16, 24, 32 or 32f)\n", optarg); return 1; }
            break;
        case 'a': opts.adts_outfile = optarg; break;
        case 'd': {
            const char *mode = optarg;
            if (!mode && optind < argc && (!strcmp(argv[optind], "mono") || !strcmp(argv[optind], "stereo") ||
                                           !strcmp(argv[optind], "1") || !strcmp(argv[optind], "2")))
                mode = argv[optind++];
            if (!mode || !strcmp(mode, "mono") || !strcmp(mode, "1")) opts.downmix = FAAD_DOWNMIX_MONO;
            else if (!strcmp(mode, "stereo") || !strcmp(mode, "2")) opts.downmix = FAAD_DOWNMIX_STEREO;
            else { fprintf(stderr, "Unknown downmix mode '%s' (mono/1 or stereo/2)\n", mode); return 1; }
            break;
        }
        case 'j': {
            char *end;
            opts.jump_seconds = strtod(optarg, &end);
            if (end == optarg || *end || opts.jump_seconds < 0.0) { fprintf(stderr, "Invalid jump time '%s'\n", optarg); return 1; }
            break;
        }
        case 'g': opts.gapless = false; break;
        case OPT_NO_GAPLESS: opts.gapless = false; break;
        case 'i': opts.info_only = true; break;
        case OPT_JSON: opts.json_info = true; opts.info_only = true; break;
        case OPT_TRACK: {
            unsigned long long v;
            if (!cli_parse_uint(optarg, UINT32_MAX, &v) || v == 0) {
                fprintf(stderr, "Invalid --track %s (a track ID, 1 or more)\n", optarg);
                return 1;
            }
            opts.want_track = (uint32_t)v;
            break;
        }
        case 'q': opts.verbose = 0; break;
        case 'v': {
            uint8_t value;
            if (!cli_parse_verbosity(optarg, &value)) {
                fprintf(stderr, "Invalid verbosity '%s' (expected 0, 1, or 2)\n", optarg);
                return 1;
            }
            opts.verbose = value;
            break;
        }
        case OPT_STRICT: opts.strict_mode = true; break;
        case OPT_OVERWRITE: opts.overwrite = true; break;
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
        default: print_usage('?'); return 1;
        }
    }

    if (optind < argc) {
        opts.input_filename = argv[optind];
    }

    if (!opts.input_filename) {
        print_usage('?');
        return 1;
    }

    if (opts.output_filename && !strcmp(opts.output_filename, "-")) {
        opts.output_filename = NULL;
        opts.write_stdout = true;
    }

    if (!opts.info_only && !opts.write_stdout && !opts.output_filename) {
        char *out_path = cli_default_output_path(opts.input_filename, opts.raw_format ? ".raw" : ".wav");
        if (!out_path) {
            fprintf(stderr, "Out of memory\n");
            return 1;
        }
        opts.output_filename = out_path;
        allocated_outfile = out_path;
    }

    if (opts.verbose > 0 && !opts.write_stdout && !opts.info_only && libinfo.version) {
        char ver_buf[128];
        fprintf(stderr, "Freeware Advanced Audio Decoder\nFAAD %s\n\n",
                cli_version_string(ver_buf, sizeof(ver_buf), libinfo.version));
    }

    decode_callbacks_t cbs = {
        .progress_cb = cli_progress_callback,
        .session_start_cb = cli_session_start_callback,
        .summary_cb = cli_summary_callback,
        .log_cb = cli_log_callback,
        .user_data = &opts
    };

    int ret_status = run_decoding_session_ext(&opts, &cbs);

    if (opts.verbose > 0 && !opts.write_stdout && !opts.info_only) {
        fprintf(stderr, "\n");
    }

    free(allocated_outfile);

#ifdef _WIN32
    if (allocated_argv) {
        for (int i = 0; i < argc; i++) {
            if (allocated_argv[i]) free(allocated_argv[i]);
        }
        free(allocated_argv);
    }
#endif

    return ret_status;
}
