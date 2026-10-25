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
#include <ctype.h>
#ifdef HAVE_GETOPT_H
# include <getopt.h>
#else
# include "getopt.h"
# include "getopt.c"
#endif
#include "input.h"
#include "cli_io.h"
#include "charset.h"
#include "help.h"
#include "cli_parse.h"
#include "faar_engine.h"

static const help_t core_help[] = {
    {"-r, --rate <Hz>", "Target output sample rate (required)",
     "Choose the output rate in hertz. The CLI accepts 7350 through 192000 Hz; --allow-extreme-rates extends the upper limit to 384000 Hz. The library must also have a resampling plan for the selected rate pair."},
    {"-R, --in-rate <Hz>", "Raw input sample rate [default: 44100 Hz]",
     "Sets the sample rate for raw PCM input. WAV input always uses the rate stored in its header."},
    {"--quality <preset>", "Filter profile: fast (lower cost), good (default), or high (stronger rejection)",
     "Fast uses less processing at the cost of a narrower passband and weaker alias rejection. Good is the default profile with a wide passband and strong alias suppression. High keeps Good's passband while further reducing aliasing, at greater CPU and setup-memory cost. Float builds support High with compensated float arithmetic, which uses more CPU; filter coefficients and FFT twiddles are still prepared with the existing double-precision calculations."},
    {"--boundary <mode>", "Stream endpoint behavior: hold (default) or zero",
     "Hold extends the first and last input sample beyond the stream; zero extends with silence. This affects the beginning and end of each input stream."},
    {"-c, --channels <num>", "Raw input channel count [default: 2]",
     "Sets the number of interleaved channels in raw input. The supported range is 1 through 8, subject to the configured build limit. WAV input uses its header channel count."},
    {"-f, --format <fmt>", "PCM format: f32le (default), s16le, or s24le",
     "Sets the raw input sample format and output sample format. WAV input format is read from its header; this option still selects the output format. All raw formats are little-endian."},
    {"-d, --dither <type>", "Integer output dither: none (default), tpdf, or shaped",
     "Selects dither for 16-bit or 24-bit integer output. TPDF adds triangular dither; shaped applies high-pass noise shaping. Dither has no effect on float output."},
    {"-D[<factor>], --decimate[=<factor>]", "Use a multistage filter for downsampling",
     "Forces a cascade of 2:1 decimation stages. The optional factor defaults to 2; attach it to the option, for example -D4 or --decimate=4. Large downsampling ratios may select a cascade automatically."},
    {"--allow-extreme-rates", "Permit output rates above 192000 Hz",
     "Raises the CLI output-rate ceiling from 192000 to 384000 Hz. The selected input/output rate pair must still be supported by the library."},
    {"-I, --ignorelength", "Read WAV audio to EOF, ignoring its data length",
     "Use this when the WAV data chunk length is missing or incorrect. This changes how the WAV data chunk is read; it does not change raw PCM handling."},
    {"--raw", "Read the input as raw PCM",
     "Disables WAV detection and interprets the input using --in-rate, --channels, and --format. Use this for raw PCM on stdin so format detection does not consume stream data."},
    {"-v <level>", "Verbosity: 0 quiet, 1 progress, 2 with summary",
     "Level 0 suppresses progress and informational messages. Level 1 (default) shows progress. Level 2 also prints the completed input and output frame counts. Errors are always shown."},
    {"-q, --quiet", "Suppress progress and informational messages",
     "Equivalent to -v0. Errors are still written to stderr."},
    {"--overwrite", "Replace an existing output file",
     "Without this option, FAAR refuses to replace an existing output. Input and output paths that refer to the same file are always rejected."},
    {"--license", "Display copyright and license information",
     "Prints copyright, patent, and LGPL information, then exits."},
    {"-o, --output <path>", "Write output to a path; use - for stdout",
     "If omitted, output goes to stdout. Stdout contains raw PCM only; progress and messages are written to stderr."},
    {"<input> [output]", "Read a WAV or raw PCM file; output defaults to stdout",
     "Use - as the input path for stdin and as the output path for stdout. WAV input is detected from its header unless --raw is given. A named output is WAV when the input is WAV or its name ends in .wav (case-insensitive); otherwise it is raw PCM."},
    {"-h, --help", "Show short help and exit",
     "Prints the option summary and examples. Use -H for detailed descriptions of all options."},
    {"-H", "Show detailed help and exit",
     "Prints the option summary with additional descriptions and examples."},
    {NULL, NULL, NULL}
};

static const help_group_t help_groups[] = {
    {1, "Resampler options", "--help", core_help, 1},
    {0, NULL, NULL, NULL, 0}
};

static bool path_has_wav_suffix(const char *path) {
    size_t len = path ? strlen(path) : 0;
    return len >= 4 && path[len - 4] == '.' &&
           tolower((unsigned char)path[len - 3]) == 'w' &&
           tolower((unsigned char)path[len - 2]) == 'a' &&
           tolower((unsigned char)path[len - 1]) == 'v';
}

static void print_faar_help(int mode) {
    char version[128];
    show_help_usage("faar", mode,
                    cli_version_string(version, sizeof(version), "3.0.0"),
                    "faar [options] <input> [output]", help_groups);
    printf(
        "\nExamples:\n"
        "  faar -r 48000 input_96k.wav output_48k.wav\n"
        "  faar -r 48000 --quality high -d tpdf -f s16le input.wav output.wav\n"
        "  cat input.raw | faar --raw -r 48000 -c 2 -f f32le - - | aplay -f FLOAT_LE -r 48000 -c 2\n"
    );
}

int main(int argc, char **argv) {
    uint32_t target_rate = 0;
    uint32_t raw_in_rate = 44100;
    faar_quality quality = FAAR_QUALITY_GOOD;
    uint16_t channels = 2;
    faar_pcm_format_t fmt = FAAR_PCM_F32LE;
    faar_dither dither = FAAR_DITHER_OFF;
    faar_boundary boundary = FAAR_BOUNDARY_HOLD;
    const char *input_path = NULL;
    const char *output_path = NULL;

    uint8_t verbose = 1;
    bool ignore_wav_length = false;
    bool force_raw_input = false;
    bool allow_extreme_rates = false;
    bool overwrite = false;

    uint32_t decimate_factor = 0;

    static struct option long_options[] = {
        {"rate", required_argument, 0, 'r'},
        {"in-rate", required_argument, 0, 'R'},
        {"quality", required_argument, 0, 1000},
        {"boundary", required_argument, 0, 1002},
        {"channels", required_argument, 0, 'c'},
        {"format", required_argument, 0, 'f'},
        {"dither", required_argument, 0, 'd'},
        {"decimate", optional_argument, 0, 'D'},
        {"allow-extreme-rates", no_argument, 0, 1001},
        {"ignorelength", no_argument, 0, 'I'},
        {"raw", no_argument, 0, 1004},
        {"output", required_argument, 0, 'o'},
        {"quiet", no_argument, 0, 'q'},
        {"overwrite", no_argument, 0, 1005},
        {"license", no_argument, 0, 1003},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "r:R:c:f:d:v:I::D::o:hHq", long_options, NULL)) != -1) {
        switch (opt) {
            case 'r':
                if (!cli_parse_rate(optarg, 7350, 384000, &target_rate)) {
                    fprintf(stderr, "Error: Invalid target sample rate '%s' (expected 7350 to 384000 Hz)\n", optarg);
                    return 1;
                }
                break;
            case 'R': raw_in_rate = (uint32_t)atoi(optarg); break;
            case 1000:
                if (strcmp(optarg, "fast") == 0) quality = FAAR_QUALITY_FAST;
                else if (strcmp(optarg, "high") == 0) quality = FAAR_QUALITY_HIGH;
                else if (strcmp(optarg, "good") == 0) quality = FAAR_QUALITY_GOOD;
                else { fprintf(stderr, "Error: Invalid quality preset '%s' (choose fast, good, or high)\n", optarg); return 1; }
                break;
            case 'c': channels = (uint16_t)atoi(optarg); break;
            case 'f':
                if (strcmp(optarg, "s16le") == 0) fmt = FAAR_PCM_S16LE;
                else if (strcmp(optarg, "s24le") == 0) fmt = FAAR_PCM_S24LE;
                else if (strcmp(optarg, "f32le") == 0) fmt = FAAR_PCM_F32LE;
                else { fprintf(stderr, "Error: Invalid raw PCM format '%s' (choose f32le, s16le, or s24le)\n", optarg); return 1; }
                break;
            case 'd':
                if (strcmp(optarg, "tpdf") == 0) dither = FAAR_DITHER_TPDF;
                else if (strcmp(optarg, "shaped") == 0) dither = FAAR_DITHER_SHAPED;
                else if (strcmp(optarg, "none") == 0) dither = FAAR_DITHER_OFF;
                else { fprintf(stderr, "Error: Invalid dither mode '%s' (choose none, tpdf, or shaped)\n", optarg); return 1; }
                break;
            case 1001: allow_extreme_rates = true; break;
            case 1002:
                if (strcmp(optarg, "hold") == 0) boundary = FAAR_BOUNDARY_HOLD;
                else if (strcmp(optarg, "zero") == 0) boundary = FAAR_BOUNDARY_ZERO;
                else { fprintf(stderr, "Invalid boundary mode: %s\n", optarg); return 1; }
                break;
            case 'v':
                if (strcmp(optarg, "0") == 0) verbose = 0;
                else if (strcmp(optarg, "1") == 0) verbose = 1;
                else if (strcmp(optarg, "2") == 0) verbose = 2;
                else { fprintf(stderr, "Error: Invalid verbosity '%s' (choose 0, 1, or 2)\n", optarg); return 1; }
                break;
            case 'q': verbose = 0; break;
            case 1005: overwrite = true; break;
            case 1003:
                {
                    faar_library_info info = { .struct_size = sizeof(info) };
                    if (faar_get_library_info(&info) == FAAR_OK && info.copyright)
                        fprintf(stdout, "%s\n", info.copyright);
                }
                cli_print_patent_notice(stdout);
                cli_print_lgpl_notice(stdout, "library");
                return 0;
            case 1004: force_raw_input = true; break;
            case 'I': ignore_wav_length = true; break;
            case 'D':
                decimate_factor = (optarg && atoi(optarg) > 0) ? (uint32_t)atoi(optarg) : 2;
                break;
            case 'o': output_path = optarg; break;
            case 'h': print_faar_help('h'); return 0;
            case 'H': print_faar_help('H'); return 0;
            default: print_faar_help('h'); return 1;
        }
    }

    if (optind < argc && !input_path) input_path = argv[optind++];
    if (optind < argc && !output_path) output_path = argv[optind++];

    if (optind < argc) {
        fprintf(stderr, "Error: Unexpected argument '%s'\n", argv[optind]);
        fprintf(stderr, "Try '%s --help' for usage.\n", argv[0]);
        return 1;
    }

    if (!input_path || target_rate == 0) {
        print_faar_help('?');
        return 1;
    }

    uint32_t max_rate = allow_extreme_rates ? 384000 : 192000;
    if (target_rate < 7350 || target_rate > max_rate) {
        if (target_rate > 192000 && !allow_extreme_rates) {
            fprintf(stderr, "Error: Target sample rate %u Hz exceeds standard audio rate limit (192000 Hz).\n"
                            "       Use --allow-extreme-rates to permit rates up to 384000 Hz.\n", target_rate);
        } else {
            fprintf(stderr, "Error: Target sample rate %u Hz is outside valid range (7350 Hz to %u Hz).\n", target_rate, max_rate);
        }
        return 1;
    }

    if (output_path && strcmp(output_path, "-") != 0 && strcmp(input_path, "-") != 0 &&
        cli_same_file(input_path, output_path)) {
        fprintf(stderr, "Error: Input and output refer to the same file\n");
        return 1;
    }

    if (output_path && strcmp(output_path, "-") != 0 && !overwrite &&
        cli_file_exists(output_path)) {
        fprintf(stderr, "Error: Output file %s already exists (use --overwrite)\n", output_path);
        return 1;
    }

    pcmfile_t *infile = force_raw_input ? NULL : wav_open_read(input_path, false);
    bool is_wav_input = (infile != NULL);

    if (is_wav_input) {
        if (ignore_wav_length) infile->bounded_data = false;
        channels = infile->channels;
    } else {
        infile = wav_open_read(input_path, true);
        if (!infile) {
            fprintf(stderr, "Error: Couldn't open input file %s\n", input_path);
            return 1;
        }
        infile->channels = channels;
        infile->samplebytes = (fmt == FAAR_PCM_F32LE) ? 4 : (fmt == FAAR_PCM_S24LE ? 3 : 2);
        infile->samplerate = raw_in_rate;
        infile->isfloat = (fmt == FAAR_PCM_F32LE);
        /* All raw formats exposed by this CLI are little-endian. */
        infile->bigendian = false;
    }

    FILE *fout = NULL;
    if (!output_path || strcmp(output_path, "-") == 0) {
        fout = stdout;
        if (!cli_binary_stream(fout)) {
            fprintf(stderr, "Error: Couldn't set stdout to binary mode\n");
            if (infile) wav_close(infile);
            return 1;
        }
    } else {
        fout = cli_fopen(output_path, "wb");
        if (!fout) {
            fprintf(stderr, "Error: Couldn't open output file %s\n", output_path);
            if (infile) wav_close(infile);
            return 1;
        }
    }

    if (verbose > 0) {
        char version[128];
        fprintf(stderr, "Freeware Advanced Audio Resampler\nFAAR %s\n\n",
                cli_version_string(version, sizeof(version), "3.0.0"));
    }

    bool write_wav_out = fout != stdout &&
                         (is_wav_input || path_has_wav_suffix(output_path));

    faar_engine_options_t engine_options = {
        .output_rate = target_rate,
        .quality = quality,
        .format = fmt,
        .dither = dither,
        .boundary = boundary,
        .decimate_factor = decimate_factor,
        .verbosity = verbose,
        .write_wav = write_wav_out,
    };
    faar_engine_stats_t engine_stats;
    int engine_result = faar_engine_run(infile, fout, &engine_options,
                                        &engine_stats);
    if (infile) wav_close(infile);
    if (engine_result != 0) {
        if (fout != stdout) fclose(fout);
        return engine_result;
    }

    if (!cli_finish_output(&fout)) {
        fprintf(stderr, "Error: Couldn't finish writing output\n");
        return 1;
    }

    if (verbose >= 2) {
        fprintf(stderr, "Completed: %llu input frames -> %llu output frames (%.3f s)\n",
                (unsigned long long)engine_stats.input_frames,
                (unsigned long long)engine_stats.output_frames,
                (double)engine_stats.output_frames / target_rate);
    }


    return 0;
}
