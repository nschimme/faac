/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2001 Menno Bakker
 * Copyright (C) 2002-2017 Krzysztof Nikiel
 * Copyright (C) 2004 Dan Villiom P. Christiansen
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

#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#include <locale.h>
#endif

#if defined(__APPLE__) || defined(__NetBSD__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__bsdi__)
#define __unix__
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#ifdef HAVE_GETOPT_H
# include <getopt.h>
#else
# include "getopt.h"
# include "getopt.c"
#endif

#ifndef _WIN32
# include <sys/ioctl.h>
# include <unistd.h>
#endif

#include <faac.h>
#include "output.h"
#include "charset.h"
#include "encode_engine.h"
#include "help.h"

#ifdef _WIN32
# undef stderr
# define stderr stdout
#endif

#define MAX_COVER_ART_SIZE ((size_t)32 * 1024 * 1024)

enum flags
{
    SHORTCTL_FLAG = 300,
    MPEGVERS_FLAG,
    ARTIST_FLAG,
    ARTIST_SORT_FLAG,
    TITLE_FLAG,
    GENRE_FLAG,
    ALBUM_FLAG,
    ALBUM_SORT_FLAG,
    ALBUM_ARTIST_FLAG,
    ALBUM_ARTIST_SORT_FLAG,
    TRACK_FLAG,
    DISC_FLAG,
    YEAR_FLAG,
    COVER_ART_FLAG,
    COMMENT_FLAG,
    WRITER_FLAG,
    WRITER_SORT_FLAG,
    TAG_FLAG,
    CREATION_TIME_FLAG,
    OPT_JOINT = 330,
    OPT_PNS_DISABLE,
    OBJTYPE_FLAG,
    CAP_RATE_FLAG,
    CBR_FLAG,
    OPT_TNS_DISABLE,
    OPT_OVERWRITE,
    OPT_COMPILATION,
    OPT_IGNORE_LENGTH,
    LANG_FLAG
};

#ifndef _WIN32
volatile int running = 1;
static void signal_handler(int signal)
{
    (void)signal;
    running = 0;
}
#endif

static bool cli_progress_callback(const progress_info_t *info, void *user_data)
{
    encode_options_t *opts = (encode_options_t *)user_data;
    if (opts && opts->verbose == 0)
    {
#ifndef _WIN32
        return running != 0;
#else
        return true;
#endif
    }

    /* Cancellation (running != 0) is checked below on every call regardless
       of this throttle: the caller invokes progress_cb every frame, but
       redrawing the status line that often just spams the terminal. */
    static progress_throttle_t s_throttle = { .last_fired_sec = -1.0 };
    if (info->is_final || progress_throttle_tick(&s_throttle, info, 0.033))
    {
        if (info->total_frames > 0)
        {
            int percent = (int)(info->current_frame * 100 / info->total_frames);
            double played_sec = (double)info->current_input_samples / (double)(info->sample_rate ? info->sample_rate : 1);
            double bitrate_kbps = played_sec > 0.0 ? ((double)info->total_bytes_written * 8.0 / 1000.0) / played_sec : 0.0;
            fprintf(stderr, "\r%7u/%-7u (%3d%%) |  %5.1f  | %6.1f/%-6.1f | %7.2fx | %.1f ",
                    info->current_frame, info->total_frames, percent,
                    bitrate_kbps,
                    info->time_elapsed_sec, info->time_elapsed_sec + info->eta_sec,
                    info->speed_factor, info->eta_sec);
        }
        else
        {
            fprintf(stderr, "\r %7u | %7.1f | %7.2fx ",
                    info->current_frame, info->time_elapsed_sec, info->speed_factor);
        }
        fflush(stderr);
    }
#ifndef _WIN32
    return running != 0;
#else
    return true;
#endif
}

static void cli_log_callback(int level, const char *message, void *user_data)
{
    encode_options_t *opts = (encode_options_t *)user_data;
    if (opts && (int)opts->verbose >= level)
    {
        fprintf(stderr, "%s", message);
    }
}

static void cli_session_start_callback(const encode_session_info_t *info, void *user_data)
{
    encode_options_t *opts = (encode_options_t *)user_data;
    if (!opts || opts->verbose < 1)
        return;

    if (info->bit_rate)
    {
        fprintf(stderr, "Initial quantization quality: %u\n", info->quant_quality);
        fprintf(stderr, "%s bitrate: %u kbps/channel\n",
                info->rate_control == FAAC_RC_CBR ? "Constant" : "Average",
                (info->bit_rate + 500) / 1000);
    }
    else
    {
        fprintf(stderr, "Quantization quality: %u\n", info->quant_quality);
    }
    if (opts->max_bit_rate)
        fprintf(stderr, "Peak bitrate: %u kbps\n", (opts->max_bit_rate + 500) / 1000);
    fprintf(stderr, "Bandwidth: %u Hz\n", info->bandwidth);

    const char *jm_str = "";
    switch (opts->joint_mode)
    {
    case FAAC_JOINT_MS: jm_str = " + M/S"; break;
    case FAAC_JOINT_IS: jm_str = " + IS"; break;
    case FAAC_JOINT_MIXED: jm_str = " + Mixed"; break;
    default: break;
    }

    fprintf(stderr, "Object type: %s (MPEG-%d)%s%s%s\n",
            (info->object_type == FAAC_OBJ_HE_AAC_V1) ? "HE-AAC v1" : "Low Complexity",
            (info->mpeg_version == FAAC_MPEG4) ? 4 : 2,
            opts->use_tns ? " + TNS" : "",
            jm_str,
            info->use_pns ? " + PNS" : "");

    const char *fmt_str = "Unknown";
    if (info->container_mp4)
    {
        fmt_str = "MPEG-4 File Format (MP4)";
    }
    else
    {
        switch (info->stream_format)
        {
        case FAAC_STREAM_RAW: fmt_str = "Headerless AAC (RAW)"; break;
        case FAAC_STREAM_ADTS: fmt_str = "Transport Stream (ADTS)"; break;
        default: break;
        }
    }
    fprintf(stderr, "Container format: %s\n", fmt_str);

    fprintf(stderr, "Encoding %s to %s\n", info->input_filename, info->output_filename);
    if (info->total_input_samples != 0)
    {
        fprintf(stderr, "         frame         | bitrate | elapsed/estim | play/CPU | ETA\n");
    }
    else
    {
        fprintf(stderr, "  frame  | elapsed | play/CPU\n");
    }
}

static void cli_summary_callback(const encode_summary_t *summary, void *user_data)
{
    encode_options_t *opts = (encode_options_t *)user_data;
    if (!opts || opts->verbose < 2)
        return;

    fprintf(stderr, "\n--- Encode Summary ---\n");
    fprintf(stderr, " Frames & Samples    : Frames = %u | Samples = %" PRIu64 "\n",
            summary->frame_count, summary->sample_count);
    if (summary->is_mp4)
    {
        fprintf(stderr, " Bitrate & Frame Size: Avg = %u | Max = %u | Max Frame Size = %u\n",
                summary->avg_bitrate, summary->max_bitrate, summary->max_frame_size);
    }
    else
    {
        fprintf(stderr, " Bitrate & Frame Size: Avg = %u kbps | Max Frame Size = %u bytes\n",
                summary->avg_bitrate, summary->max_frame_size);
    }

    long input_size = get_file_size(opts->input_filename);
    long output_size = get_file_size(opts->output_filename);
    if (input_size > 0 && output_size > 0)
    {
        fprintf(stderr, " File Size           : Input = %.2f MB | Output = %.2f MB | Ratio = %.1f:1\n",
                input_size / (1024.0 * 1024.0), output_size / (1024.0 * 1024.0),
                (double)input_size / (double)output_size);
    }
    fprintf(stderr, "----------------------\n");
}

int main(int argc, char *argv[])
{
    encode_options_t opts;
    init_encode_options(&opts);

    char *aacFileName = NULL;
    bool aacFileNameGiven = false;
    bool stream_flag_given = false;
    bool has_custom_tags = false;
    bool quality_given = false;
    bool bitrate_given = false;
    const char *dieMessage = NULL;
    int ret = 0;

#ifndef _WIN32
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    /* So charset.c's utf8_ensure() can read the real locale codeset via
       nl_langinfo() instead of always seeing the default "C" locale. */
    setlocale(LC_CTYPE, "");
#endif

    faac_library_info libinfo = { .struct_size = sizeof(libinfo) };
    if (faac_get_library_info(&libinfo) != FAAC_OK)
    {
        fprintf(stderr, "Wrong libfaac version!\n");
        return 1;
    }

#ifdef _WIN32
    int wargc = 0;
    wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    char **allocated_argv = NULL;
    if (wargv && wargc > 0)
    {
        allocated_argv = calloc((size_t)wargc, sizeof(char *));
        if (allocated_argv)
        {
            bool conv_ok = true;
            for (int i = 0; i < wargc; i++)
            {
                char *utf8_arg = win32_utf16_to_utf8(wargv[i]);
                if (!utf8_arg)
                {
                    conv_ok = false;
                    break;
                }
                allocated_argv[i] = utf8_arg;
            }

            if (conv_ok)
            {
                argc = wargc;
                argv = allocated_argv;
            }
            else
            {
                for (int i = 0; i < wargc; i++)
                {
                    if (allocated_argv[i])
                        free(allocated_argv[i]);
                }
                free(allocated_argv);
                allocated_argv = NULL;
            }
        }
        LocalFree(wargv);
    }
#endif

    if (argc < 2)
    {
        show_help('?', libinfo.version);
        ret = 1;
        goto cleanup;
    }

    while (1)
    {
        static struct option long_options[] = {
            {"help", 0, 0, 'h'},
            {"help-qual", 0, 0, HELP_QUAL},
            {"help-io", 0, 0, HELP_IO},
            {"help-mp4", 0, 0, HELP_MP4},
            {"help-advanced", 0, 0, HELP_ADVANCED},
            {"raw", 0, 0, 'r'},
            {"joint", required_argument, 0, OPT_JOINT},
            {"no-pns", 0, 0, OPT_PNS_DISABLE},
            {"cutoff", 1, 0, 'c'},
            {"quality", 1, 0, 'q'},
            {"pcmraw", 0, 0, 'P'},
            {"pcmsamplerate", 1, 0, 'R'},
            {"pcmsamplebits", 1, 0, 'B'},
            {"pcmchannels", 1, 0, 'C'},
            {"shortctl", 1, 0, SHORTCTL_FLAG},
            {"no-tns", 0, 0, OPT_TNS_DISABLE},
            {"mpeg-version", 1, 0, MPEGVERS_FLAG},
            {"object-type", 1, 0, OBJTYPE_FLAG},
            {"license", 0, 0, 'L'},
            {"adts", 0, 0, 'a'},
            {"artist", 1, 0, ARTIST_FLAG},
            {"artistsort", 1, 0, ARTIST_SORT_FLAG},
            {"title", 1, 0, TITLE_FLAG},
            {"album", 1, 0, ALBUM_FLAG},
            {"albumartist", 1, 0, ALBUM_ARTIST_FLAG},
            {"albumartistsort", 1, 0, ALBUM_ARTIST_SORT_FLAG},
            {"albumsort", 1, 0, ALBUM_SORT_FLAG},
            {"track", 1, 0, TRACK_FLAG},
            {"disc", 1, 0, DISC_FLAG},
            {"genre", 1, 0, GENRE_FLAG},
            {"year", 1, 0, YEAR_FLAG},
            {"cover-art", 1, 0, COVER_ART_FLAG},
            {"comment", 1, 0, COMMENT_FLAG},
            {"composer", 1, 0, WRITER_FLAG},
            {"composersort", 1, 0, WRITER_SORT_FLAG},
            {"compilation", 0, 0, OPT_COMPILATION},
            {"pcmswapbytes", 0, 0, 'X'},
            {"ignorelength", 0, 0, OPT_IGNORE_LENGTH},
            {"tag", 1, 0, TAG_FLAG},
            {"overwrite", 0, 0, OPT_OVERWRITE},
            {"creation-time", 1, 0, CREATION_TIME_FLAG},
            {"lang", 1, 0, LANG_FLAG},
            {"language", 1, 0, LANG_FLAG},
            {"cap-rate", 1, 0, CAP_RATE_FLAG},
            {"cbr", 0, 0, CBR_FLAG},
            {0, 0, 0, 0}
        };

        int option_index = 0;
        int c = getopt_long(argc, argv, "Hhb:m:o:rnc:q:PR:B:C:I:Xv:La",
                            long_options, &option_index);

        if (c == -1)
            break;

        switch (c)
        {
        case OPT_TNS_DISABLE: opts.use_tns = false; break;
        case OPT_PNS_DISABLE: opts.use_pns = false; break;
        case OPT_OVERWRITE: opts.overwrite = true; break;
        case OPT_COMPILATION: opts.metadata.compilation = true; break;
        case OPT_IGNORE_LENGTH: opts.ignore_wav_length = true; break;
        case 'L':
            if (libinfo.copyright)
                fprintf(stderr, "%s", libinfo.copyright);
            cli_print_patent_notice(stderr);
            cli_print_lgpl_notice(stderr, "library");
            ret = 0;
            goto cleanup;
        case 'X':
            opts.raw_endian = false;
            break;
        case 'a':
            opts.container_mp4 = false;
            opts.stream_format = FAAC_STREAM_ADTS;
            stream_flag_given = true;
            break;
        case 'o':
            aacFileName = strdup(optarg);
            aacFileNameGiven = true;
            break;
        case 'r':
            opts.container_mp4 = false;
            opts.stream_format = FAAC_STREAM_RAW;
            stream_flag_given = true;
            break;
        case 'c':
            opts.bandwidth = atoi(optarg);
            break;
        case 'b':
            parse_quality_or_bitrate(optarg, true, &opts);
            bitrate_given = true;
            break;
        case 'q':
            parse_quality_or_bitrate(optarg, false, &opts);
            quality_given = true;
            break;
        case 'I':
            if (sscanf(optarg, "%hu,%hu", &opts.center_channel, &opts.lfe_channel) < 1)
                dieMessage = "Wrong channel config.\n";
            break;
        case 'P':
            opts.raw_pcm_input = true;
            break;
        case 'R':
            opts.raw_rate = atoi(optarg);
            opts.raw_pcm_input = true;
            break;
        case 'B':
            {
                int bits = atoi(optarg);
                if (bits > 32)
                    bits = 32;
                if (bits < 8)
                    bits = 8;
                opts.raw_bits = (uint8_t)bits;
            }
            opts.raw_pcm_input = true;
            break;
        case 'C':
            opts.raw_channels = (uint16_t)atoi(optarg);
            opts.raw_pcm_input = true;
            break;
        case ARTIST_FLAG:
            opts.metadata.artist = trim_quotes_and_spaces(optarg);
            break;
        case ARTIST_SORT_FLAG:
            opts.metadata.artist_sort = trim_quotes_and_spaces(optarg);
            break;
        case WRITER_FLAG:
            opts.metadata.composer = trim_quotes_and_spaces(optarg);
            break;
        case WRITER_SORT_FLAG:
            opts.metadata.composer_sort = trim_quotes_and_spaces(optarg);
            break;
        case TITLE_FLAG:
            opts.metadata.title = trim_quotes_and_spaces(optarg);
            break;
        case ALBUM_FLAG:
            opts.metadata.album = trim_quotes_and_spaces(optarg);
            break;
        case ALBUM_ARTIST_FLAG:
            opts.metadata.album_artist = trim_quotes_and_spaces(optarg);
            break;
        case ALBUM_ARTIST_SORT_FLAG:
            opts.metadata.album_artist_sort = trim_quotes_and_spaces(optarg);
            break;
        case ALBUM_SORT_FLAG:
            opts.metadata.album_sort = trim_quotes_and_spaces(optarg);
            break;
        case TRACK_FLAG:
            if (sscanf(optarg, "%hu/%hu", &opts.metadata.track, &opts.metadata.ntracks) < 1)
                dieMessage = "Wrong track number.\n";
            break;
        case DISC_FLAG:
            if (sscanf(optarg, "%hu/%hu", &opts.metadata.disc, &opts.metadata.ndiscs) < 1)
                dieMessage = "Wrong disc number.\n";
            break;
        case GENRE_FLAG:
            if (!parse_genre(trim_quotes_and_spaces(optarg), &opts.metadata.genre_id, &opts.metadata.genre))
                dieMessage = "Genre number out of range.\n";
            break;
        case YEAR_FLAG:
            opts.metadata.year = trim_quotes_and_spaces(optarg);
            break;
        case COMMENT_FLAG:
            opts.metadata.comment = trim_quotes_and_spaces(optarg);
            break;
        case MPEGVERS_FLAG:
            switch (atoi(optarg))
            {
            case 2: opts.mpeg_version = FAAC_MPEG2; break;
            case 4: opts.mpeg_version = FAAC_MPEG4; break;
            default: dieMessage = "Unrecognised MPEG version!\n"; break;
            }
            break;
        case OBJTYPE_FLAG:
            if (!strcmp(optarg, "lc"))
                opts.object_type = FAAC_OBJ_LOW;
            else if (!strcmp(optarg, "he-aac-v1"))
                opts.object_type = FAAC_OBJ_HE_AAC_V1;
            else if (!strcmp(optarg, "auto"))
                opts.object_type = FAAC_OBJ_AUTO;
            else
                dieMessage = "Unrecognised object type (use lc, he-aac-v1, or auto)!\n";
            break;
        case SHORTCTL_FLAG:
            opts.shortctl = (enum faac_shortctl_mode)atoi(optarg);
            break;
        case OPT_JOINT:
            opts.joint_mode = (enum faac_joint_mode)atoi(optarg);
            break;
        case TAG_FLAG:
            {
                char *tagname = optarg;
                char *eq = strchr(optarg, '=');
                char *comma = strchr(optarg, ',');
                char *tagval = NULL;

                if (eq && comma)
                    tagval = (eq < comma) ? eq : comma;
                else if (eq)
                    tagval = eq;
                else
                    tagval = comma;

                if (!tagval)
                {
                    dieMessage = "Missing tag value.\n";
                }
                else
                {
                    *tagval++ = '\0';
                    tagname = trim_quotes_and_spaces(tagname);
                    tagval = trim_quotes_and_spaces(tagval);

                    if (*tagname == '\0')
                        dieMessage = "Tag name cannot be empty.\n";
                    else if (*tagval == '\0')
                        dieMessage = "Tag value cannot be empty.\n";
                }
                if (!dieMessage)
                {
                    if (!add_custom_tag_to_options(&opts, tagname, tagval))
                        dieMessage = "Couldn't add tag (out of memory).\n";
                }
                has_custom_tags = true;
            }
            break;
        case COVER_ART_FLAG:
            {
                FILE *f = cli_fopen(optarg, "rb");
                if (f)
                {
                    fseek(f, 0, SEEK_END);
                    long sz = ftell(f);
                    fseek(f, 0, SEEK_SET);
                    clearerr(f);

                    if (sz <= 0 || (size_t)sz > MAX_COVER_ART_SIZE)
                    {
                        dieMessage = "Invalid cover art file size!\n";
                    }
                    else
                    {
                        opts.art_size = (uint64_t)sz;
                        opts.art_data = malloc((size_t)opts.art_size);
                        if (opts.art_data)
                        {
                            if (fread((void *)opts.art_data, 1, (size_t)opts.art_size, f) != (size_t)opts.art_size)
                            {
                                dieMessage = "Error reading cover art file!\n";
                                free((void *)opts.art_data);
                                opts.art_data = NULL;
                                opts.art_size = 0;
                            }
                            else if (opts.art_size < 12 || !check_image_header((const char *)opts.art_data))
                            {
                                dieMessage = "Unsupported cover image file format!\n";
                                free((void *)opts.art_data);
                                opts.art_data = NULL;
                                opts.art_size = 0;
                            }
                        }
                        else
                        {
                            dieMessage = "Out of memory reading cover art file!\n";
                        }
                    }
                    fclose(f);
                }
                else
                {
                    dieMessage = "Error opening cover art file!\n";
                }
            }
            break;
        case CREATION_TIME_FLAG:
            opts.creation_time_str = optarg;
            break;
        case LANG_FLAG:
            opts.metadata.language = optarg;
            break;
        case CBR_FLAG:
            opts.cbr = true;
            break;
        case CAP_RATE_FLAG:
            opts.max_bit_rate = atoi(optarg) * 1000;
            break;
        case 'v':
            opts.verbose = (uint8_t)atoi(optarg);
            break;
        case HELP_QUAL:
        case HELP_IO:
        case HELP_MP4:
        case HELP_ADVANCED:
        case 'H':
        case 'h':
            show_help(c, libinfo.version);
            ret = 1;
            goto cleanup;
        case '?':
        default:
            show_help('?', libinfo.version);
            ret = 1;
            goto cleanup;
        }
    }

    if (optind < argc)
    {
        opts.input_filename = argv[optind];
        if ((argc - optind) > 1 && aacFileNameGiven)
            dieMessage = "Cannot encode several input files to one output file.\n";
    }
    else
    {
        dieMessage = "No input file specified.\n";
    }

    /* The last one would silently win; the user meant one mode. */
    if (!dieMessage && quality_given && bitrate_given)
        dieMessage = "-q and -b are exclusive; use --cap-rate to bound VBR.\n";

    if (dieMessage)
    {
        fprintf(stderr, "%s", dieMessage);
        ret = 1;
        goto cleanup;
    }

    if (!aacFileNameGiven)
    {
        aacFileName = get_output_filename(opts.input_filename, opts.container_mp4);
    }
    else if (!stream_flag_given)
    {
        opts.container_mp4 = detect_container_mp4(aacFileName);
        opts.stream_format = opts.container_mp4 ? FAAC_STREAM_RAW : FAAC_STREAM_ADTS;
    }

    if (opts.container_mp4)
    {
        opts.mpeg_version = FAAC_MPEG4;
    }

    bool has_metadata = opts.metadata.artist || opts.metadata.artist_sort ||
                        opts.metadata.title || opts.metadata.album ||
                        opts.metadata.album_sort || opts.metadata.album_artist ||
                        opts.metadata.album_artist_sort || opts.metadata.composer ||
                        opts.metadata.composer_sort || opts.metadata.year ||
                        opts.metadata.comment || opts.metadata.genre_id ||
                        opts.metadata.genre || opts.metadata.track ||
                        opts.metadata.disc || opts.metadata.compilation ||
                        opts.metadata.language || opts.art_data ||
                        opts.custom_tag_count > 0 || has_custom_tags;

    if (!opts.container_mp4 && has_metadata)
    {
        fprintf(stderr, "Metadata requires MP4 output!\n");
        ret = 1;
        goto cleanup;
    }

    if (opts.verbose > 0 && libinfo.version)
    {
        char ver_buf[128];
        fprintf(stderr, "Freeware Advanced Audio Coder\nFAAC %s\n\n", faac_version_string(ver_buf, sizeof(ver_buf), libinfo.version));
    }

    opts.output_filename = aacFileName;

    encode_callbacks_t cbs = {
        .progress_cb = cli_progress_callback,
        .session_start_cb = cli_session_start_callback,
        .summary_cb = cli_summary_callback,
        .log_cb = cli_log_callback,
        .user_data = &opts
    };

    ret = run_encoding_session_ext(&opts, &cbs);
    if (opts.verbose && opts.verbose < 2)
        fprintf(stderr, "\n");

cleanup:
    if (aacFileName) free(aacFileName);
    free_encode_options(&opts);

#ifdef _WIN32
    if (allocated_argv)
    {
        for (int i = 0; i < argc; i++)
        {
            if (allocated_argv[i])
                free(allocated_argv[i]);
        }
        free(allocated_argv);
    }
#endif

    return ret;
}
