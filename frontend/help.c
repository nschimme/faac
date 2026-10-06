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
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <faac.h>
#include "encode_engine.h"
#include "help.h"

#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "2.2.0"
#endif

typedef struct {
    const char *opt;
    const char *shorthelp;
    const char *longhelp;
} help_t;

static const char *usage = "Usage: faac [options] infile\n\n";

static help_t help_qual[] = {
    {NULL,
     "Rate control: -q (VBR), -b (ABR, default 128), -b --cbr (CBR); --cap-rate bounds any frame in any mode.",
     "VBR holds a quality and lets the bitrate follow the material; ABR holds an average over the file; CBR holds it in every frame. -q with --cap-rate is capped VBR: constant quality, no frame above the cap. -q and -b are exclusive."},
    {"-q <quality>",
     "Set encoding quality (1-5000, default 100). (VBR)",
     "Constant quality, 1..5000, default 100; higher is better and costs more bits. The bitrate follows the material and moves between releases as the encoder is tuned. With --object-type auto, -q up to 75 uses HE-AAC v1; from 76 it is AAC-LC at a much higher bitrate. Use -b for a predictable size."},
    {"-b <bitrate>",
     "Set average bitrate to <bitrate> kbps. (ABR)",
     "Approximate over the file; max. ~500 kbps (stereo)."},
    {"--cbr",
     "Hold -b as a constant bitrate. (CBR)",
     "A bit reservoir keeps every frame inside the decoder's input buffer and stuffs any frame that would otherwise overflow it, so the rate lands exactly; ADTS declares the buffer fullness, MP4 the buffer size. For constant-rate channels and matched-rate tests; on a file or a packet network the stuffing is bytes for nothing."},
    {"-c <freq>",
     "Cut the audio off above <freq> Hz.",
     "Left out, the encoder chooses the cutoff from the bitrate, between 14 and 19 kHz on most stereo settings; VBR codes up to 19 kHz. It is printed at the start. Set it as high as half the sample rate to keep everything; the bitrate rises with it. HE-AAC sets its own and ignores this. The actual frequency is adjusted to a band edge."},
    {"--cap-rate <bitrate>",
     "Cap any single frame at <bitrate> kbps.",
     "For packet-oriented transports that cannot fragment a frame, where an oversized frame is dropped rather than split, and for bounding VBR (-q). Must be >= the -b bitrate. Best-effort: quality is backed off until the frame fits, so pathological input can still exceed the cap."},
    {NULL, NULL, NULL}
};

static help_t help_io[] = {
    {"-o <filename>",
     "Set output file name (only for one input file)",
     "Format is auto-detected from extension (.aac/.adts -> ADTS, .m4a/.mp4/.m4b -> MP4; default: MP4)."},
    {"-a",
     "Use ADTS stream output format.",
     "Generate ADTS transport stream output."},
    {"-",
     "Use stdin/stdout",
     "If you simply use a hyphen/minus sign instead of a filename, FAAC can encode directly from stdin, thus enabling piping from other applications and utilities. The same works for stdout as well, so FAAC can pipe its output to other apps such as a server."},
    {"-v <verbose>",
     "Set verbosity level (-v0 is quiet mode).",
     NULL},
    {"-r",
     "Use RAW AAC output file.",
     "Generate raw AAC bitstream (i.e. without any headers). Not advised: raw AAC files are practically useless."},
    {"-P",
     "Raw PCM input mode (default 44100 Hz, 16-bit, stereo).",
     "Raw PCM input mode (default: off, i.e. expecting a WAV header; necessary for input files or bitstreams without a header; using only -P assumes the default values for -R, -B and -C in the input file)."},
    {"-R <samplerate>",
     "Raw PCM input rate.",
     "Raw PCM input sample rate in Hz (default: 44100 Hz, max. 96 kHz)"},
    {"-B <samplebits>",
     "Raw PCM input sample size (8, 16 (default), 24 or 32 bits).",
     "Raw PCM input sample size (default: 16, also possible 8, 24, 32 bit fixed or float input)."},
    {"-C <channels>",
     "Raw PCM input channels.",
     "Raw PCM input channels (default: 2, max. 8)."},
    {"-X",
     "Swap raw PCM input byte order.",
     "Raw PCM input is read big-endian by default; -X reads it little-endian."},
    {"-I <C[,LFE]>",
     "Input channel config, default is 3,4 (Center third, LFE fourth)",
     "Input multichannel configuration (default: 3,4 which means Center is third and LFE is fourth like in 5.1 WAV, so you only have to specify a different position of these two mono channels in your multichannel input files if they haven't been reordered already)."},
    {"--ignorelength",
     "Ignore wav length from header (useful with files over 4 GB)",
     NULL},
    {"--overwrite",
     "Overwrite existing output file",
     NULL},
    {NULL, NULL, NULL}
};

static help_t help_mp4[] = {
    {"--tag <tagname=tagvalue>",
     "Add named tag (iTunes '----')",
     "Separated by = or ,. Enclose in quotes if spaces are present."},
    {"--artist <name>",
     "Set artist name", NULL},
    {"--artistsort <name>",
     "Set artist sort order", NULL},
    {"--composer <name>",
     "Set composer name", NULL},
    {"--composersort <name>",
     "Set composer sort order", NULL},
    {"--title <name>",
     "Set title/track name", NULL},
    {"--genre <number|name>",
     "Set genre number or name", NULL},
    {"--album <name>",
     "Set album/performer", NULL},
    {"--albumartist <name>",
     "Set album artist", NULL},
    {"--albumartistsort <name>",
     "Set album artist sort order", NULL},
    {"--albumsort <name>",
     "Set album sort order", NULL},
    {"--compilation",
     "Mark as compilation", NULL},
    {"--track <number/total>",
     "Set track number", NULL},
    {"--disc <number/total>",
     "Set disc number", NULL},
    {"--year <number>",
     "Set year", NULL},
    {"--cover-art <filename>",
     "Read cover art from <filename>",
     "Supported image formats are GIF, JPEG, and PNG."},
    {"--comment <string>",
     "Set comment", NULL},
    {"--lang <code3>",
     "Set ISO 639-2/T 3-letter language code (e.g. eng, ger)", NULL},
    {"--creation-time <value>",
     "Set creation/modification time (auto, now, or timestamp)", NULL},
    {NULL, NULL, NULL}
};

static help_t help_advanced[] = {
    {"--no-tns",
     "Disable coding of TNS, temporal noise shaping (default: on).", NULL},
    {"--no-pns",
     "Disable coding of PNS, perceptual noise substitution (default: on).", NULL},
    {"--joint 0",
     "Disable joint stereo coding.", NULL},
    {"--joint 1",
     "Use Mid/Side coding.", NULL},
    {"--joint 2",
     "Use Intensity Stereo coding.", NULL},
    {"--joint 3",
     "Use Mixed Mode (dynamic M/S and IS) coding (default).", NULL},
    {"--mpeg-vers X",
     "Force AAC MPEG version, X can be 2 or 4", NULL},
    {"--object-type X",
     "Force AAC object type: lc, he-aac-v1, or auto (default)", NULL},
    {"--shortctl X",
     "Enforce block type (0 = both (default); 1 = no short; 2 = no long).", NULL},
    {NULL, NULL, NULL}
};

static struct {
    int id;
    const char *name;
    const char *option;
    help_t *help;
} g_help[] = {
    {HELP_QUAL, "Quality-related options", "--help-qual", help_qual},
    {HELP_IO, "Input/output options", "--help-io", help_io},
    {HELP_MP4, "MP4 specific options", "--help-mp4", help_mp4},
    {HELP_ADVANCED, "Advanced options, only for testing purposes", "--help-advanced", help_advanced},
    {0, NULL, NULL, NULL}
};

static int get_terminal_width(void)
{
    int width = 80;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(hConsole, &csbi))
    {
        width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    }
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
    {
        width = ws.ws_col;
    }
#endif
    if (width < 40)
        width = 40;
    return width;
}

static void print_wrapped_text(const char *text, int start_col, int indent, int term_width)
{
    if (!text || !*text)
        return;

    int current_col = start_col;
    const char *p = text;

    while (*p)
    {
        if (current_col == indent)
        {
            while (*p == ' ')
                p++;
            if (!*p)
                break;
        }

        const char *word_start = p;
        while (*p && *p != ' ' && *p != '\n')
            p++;
        int word_len = (int)(p - word_start);

        if (current_col > indent && (current_col + 1 + word_len > term_width))
        {
            printf("\n");
            for (int i = 0; i < indent; i++)
                putchar(' ');
            current_col = indent;
            while (*word_start == ' ')
                word_start++;
        }

        if (current_col > indent)
        {
            putchar(' ');
            current_col++;
        }
        else if (current_col == 0)
        {
            for (int i = 0; i < indent; i++)
                putchar(' ');
            current_col = indent;
        }

        fwrite(word_start, 1, word_len, stdout);
        current_col += word_len;

        if (*p == '\n')
        {
            printf("\n");
            current_col = 0;
            p++;
        }
        else if (*p == ' ')
        {
            p++;
        }
    }

    if (current_col > 0)
        printf("\n");
}

static int get_max_opt_len_for_help(const help_t *h)
{
    int max_len = 0;
    for (int i = 0; h[i].shorthelp; i++)
    {
        if (h[i].opt)
        {
            int len = (int)strlen(h[i].opt);
            if (len > max_len)
                max_len = len;
        }
    }
    return max_len;
}

static int get_global_max_opt_len(void)
{
    int max_len = 15; /* for --help-advanced etc. */
    for (int cnt = 0; g_help[cnt].id; cnt++)
    {
        int l = get_max_opt_len_for_help(g_help[cnt].help);
        if (l > max_len)
            max_len = l;
    }
    return max_len;
}

static void help0(help_t *h, int l, int opt_col, int term_width)
{
    int cnt;

    for (cnt = 0; h[cnt].shorthelp; cnt++)
    {
        if (h[cnt].opt)
        {
            printf("    %s", h[cnt].opt);
            int opt_len_with_indent = 4 + (int)strlen(h[cnt].opt);

            if (opt_len_with_indent + 2 <= opt_col && opt_col < term_width - 15)
            {
                for (int i = opt_len_with_indent; i < opt_col; i++)
                    putchar(' ');
                print_wrapped_text(h[cnt].shorthelp, opt_col, opt_col, term_width);
            }
            else
            {
                printf("\n");
                print_wrapped_text(h[cnt].shorthelp, 0, opt_col, term_width);
            }
        }
        else
        {
            print_wrapped_text(h[cnt].shorthelp, 0, 4, term_width);
        }

        if (l && h[cnt].longhelp)
        {
            int long_indent = 8;
            print_wrapped_text(h[cnt].longhelp, 0, long_indent, term_width);
        }
    }
    printf("\n");
}

void show_help(int mode, const char *lib_version)
{
    int cnt;
    int term_width = get_terminal_width();
    int max_opt_len = get_global_max_opt_len();
    int opt_col = 4 + max_opt_len + 2;

    if (opt_col > term_width - 15)
        opt_col = term_width - 15;
    if (opt_col < 20)
        opt_col = 20;

    char ver_buf[128];
    printf("FAAC %s\n", faac_version_string(ver_buf, sizeof(ver_buf), lib_version ? lib_version : PACKAGE_VERSION));
    printf("%s", usage);
    switch (mode)
    {
    case '?':
    case 'h':
    case 'H':
        printf("Help options:\n");
        {
            static const help_t general_help[] = {
                {"-h", "Short help on using FAAC", NULL},
                {"-H", "Description of all options for FAAC.", NULL},
                {"--license", "License terms for FAAC.", NULL},
                {NULL, NULL, NULL}
            };
            for (cnt = 0; general_help[cnt].opt; cnt++)
            {
                printf("    %s", general_help[cnt].opt);
                int len = 4 + (int)strlen(general_help[cnt].opt);
                for (int i = len; i < opt_col; i++)
                    putchar(' ');
                print_wrapped_text(general_help[cnt].shorthelp, opt_col, opt_col, term_width);
            }
            for (cnt = 0; g_help[cnt].id; cnt++)
            {
                printf("    %s", g_help[cnt].option);
                int len = 4 + (int)strlen(g_help[cnt].option);
                for (int i = len; i < opt_col; i++)
                    putchar(' ');
                print_wrapped_text(g_help[cnt].name, opt_col, opt_col, term_width);
            }
        }
        printf("\n");
        if (mode == 'h')
        {
            for (cnt = 0; cnt < 2; cnt++)
            {
                printf("%s:\n", g_help[cnt].name);
                help0(g_help[cnt].help, 0, opt_col, term_width);
            }
        }
        if (mode == 'H')
        {
            for (cnt = 0; g_help[cnt].id; cnt++)
            {
                printf("%s:\n", g_help[cnt].name);
                help0(g_help[cnt].help, 1, opt_col, term_width);
            }
        }
        break;
    default:
        for (cnt = 0; g_help[cnt].id; cnt++)
            if (g_help[cnt].id == mode)
            {
                printf("%s:\n", g_help[cnt].name);
                help0(g_help[cnt].help, 1, opt_col, term_width);
                break;
            }
        break;
    }
}
