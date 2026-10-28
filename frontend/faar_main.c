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
    {"-o, --output <path>", "Write output to a path or an existing directory; use - for stdout",
     "If omitted, output goes to stdout. Stdout contains raw PCM only; progress and messages are written to stderr. When the path is an existing directory, every positional argument is an input and each result is written there under the input's file name (batch mode)."},
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

/* Set once in main from the linked library. */
static const char *g_lib_version;

static void print_faar_help(int mode) {
    char version[128];
    show_help_usage("faar", mode,
                    cli_version_string(version, sizeof(version), g_lib_version),
                    "faar [options] <input> [output]\n       faar [options] -o <dir> <input>...", help_groups);
    printf(
        "\nExamples:\n"
        "  faar -r 48000 input_96k.wav output_48k.wav\n"
        "  faar -r 48000 --quality high -d tpdf -f s16le input.wav output.wav\n"
        "  faar -r 48000 -o converted/ a.wav b.wav c.wav\n"
        "  cat input.raw | faar --raw -r 48000 -c 2 -f f32le - - | aplay -f FLOAT_LE -r 48000 -c 2\n"
    );
}

typedef struct {
    uint32_t target_rate, raw_in_rate;
    faar_quality quality;
    uint16_t raw_channels;
    faar_pcm_format_t fmt;
    uint32_t dither;
    uint32_t decimate_factor;
    uint8_t verbose;
    bool ignore_wav_length, force_raw_input, overwrite;
} job_t;

/* Output path for one input of a batch: the input's file name inside dir. */
static char *batch_output_path(const char *dir, const char *input) {
    const char *name = input;
    for (const char *c = input; *c; ++c)
        if (*c == '/' || *c == '\\') name = c + 1;
    size_t dlen = strlen(dir);
    bool slash = dlen && (dir[dlen - 1] == '/' || dir[dlen - 1] == '\\');
    char *out = malloc(dlen + 1 + strlen(name) + 1);
    if (!out) return NULL;
    sprintf(out, "%s%s%s", dir, slash ? "" : "/", name);
    return out;
}

/* Convert one input to one output path (NULL or "-" is stdout). Returns zero
   on success. */
static int convert_file(const job_t *job, const char *input_path,
                        const char *output_path, faar_design_cache *cache,
                        faar_engine_stats_t *engine_stats) {
    uint32_t target_rate = job->target_rate;
    uint32_t raw_in_rate = job->raw_in_rate;
    faar_quality quality = job->quality;
    uint16_t channels = job->raw_channels;
    faar_pcm_format_t fmt = job->fmt;
    uint32_t dither = job->dither;
    uint32_t decimate_factor = job->decimate_factor;
    uint8_t verbose = job->verbose;
    bool ignore_wav_length = job->ignore_wav_length;
    bool force_raw_input = job->force_raw_input;
    bool overwrite = job->overwrite;

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

    bool write_wav_out = fout != stdout &&
                         (is_wav_input || path_has_wav_suffix(output_path));

    faar_engine_options_t engine_options = {
        .output_rate = target_rate,
        .quality = quality,
        .format = fmt,
        .dither = dither,
        .decimate_factor = decimate_factor,
        .verbosity = verbose,
        .write_wav = write_wav_out,
        .designs = cache,
    };
    int engine_result = faar_engine_run(infile, fout, &engine_options,
                                        engine_stats);
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
                (unsigned long long)engine_stats->input_frames,
                (unsigned long long)engine_stats->output_frames,
                (double)engine_stats->output_frames / target_rate);
    }


    return 0;
}

int main(int argc, char **argv) {
    faar_library_info libinfo = { .struct_size = sizeof(libinfo) };
    if (faar_get_library_info(&libinfo) != FAAR_OK) {
        fprintf(stderr, "Wrong libfaar version!\n");
        return 1;
    }
    g_lib_version = libinfo.version;

    uint32_t target_rate = 0;
    uint32_t raw_in_rate = 44100;
    faar_quality quality = FAAR_QUALITY_GOOD;
    uint16_t channels = 2;
    faar_pcm_format_t fmt = FAAR_PCM_F32LE;
    uint32_t dither = 0;
    const char **inputs = NULL;
    size_t n_inputs = 0;
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
                else if (strcmp(optarg, "none") == 0) dither = 0;
                else { fprintf(stderr, "Error: Invalid dither mode '%s' (choose none, tpdf, or shaped)\n", optarg); return 1; }
                break;
            case 1001: allow_extreme_rates = true; break;
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

    inputs = calloc((size_t)argc, sizeof(*inputs));
    if (!inputs) {
        fprintf(stderr, "Error: Out of memory\n");
        return 1;
    }
    while (optind < argc) inputs[n_inputs++] = argv[optind++];

    /* An output directory switches to batch mode: every positional argument
       is an input, written to that directory under its own file name. */
    bool batch = output_path && strcmp(output_path, "-") != 0 && cli_is_directory(output_path);
    if (!batch) {
        if (n_inputs == 2 && !output_path) {
            output_path = inputs[1];
            n_inputs = 1;
        } else if (n_inputs > 1) {
            fprintf(stderr, "Error: Unexpected argument '%s'\n", inputs[1]);
            fprintf(stderr, "Try '%s --help' for usage.\n", argv[0]);
            free(inputs);
            return 1;
        }
    }

    if (n_inputs == 0 || target_rate == 0) {
        print_faar_help('?');
        free(inputs);
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

    for (size_t i = 0; batch && i < n_inputs; ++i) {
        if (strcmp(inputs[i], "-") == 0) {
            fprintf(stderr, "Error: Batch mode cannot read stdin\n");
            free(inputs);
            return 1;
        }
    }

    if (verbose > 0) {
        char version[128];
        fprintf(stderr, "Freeware Advanced Audio Resampler\nFAAR %s\n\n",
                cli_version_string(version, sizeof(version), g_lib_version));
    }

    const job_t job = {
        .target_rate = target_rate, .raw_in_rate = raw_in_rate,
        .quality = quality, .raw_channels = channels, .fmt = fmt,
        .dither = dither, .decimate_factor = decimate_factor, .verbose = verbose,
        .ignore_wav_length = ignore_wav_length,
        .force_raw_input = force_raw_input, .overwrite = overwrite,
    };
    faar_design_cache *cache = batch && n_inputs > 1 ? faar_design_cache_new() : NULL;
    faar_engine_stats_t stats;
    int failed = 0;
    for (size_t i = 0; i < n_inputs; ++i) {
        char *target = batch ? batch_output_path(output_path, inputs[i]) : NULL;
        if (batch && !target) {
            fprintf(stderr, "Error: Out of memory\n");
            failed = 1;
            break;
        }
        if (verbose > 0 && batch)
            fprintf(stderr, "%s -> %s\n", inputs[i], target);
        if (convert_file(&job, inputs[i], batch ? target : output_path, cache, &stats))
            failed = 1;
        free(target);
    }
    faar_design_cache_free(cache);
    free(inputs);
    return failed;
}
