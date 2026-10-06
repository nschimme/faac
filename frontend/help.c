/*
 * FAAC - Freeware Advanced Audio Coder
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
#include <ctype.h>

#include <faac.h>
#include "encode_engine.h"
#include "help.h"

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

static int get_global_max_opt_len(const help_group_t *groups)
{
    int max_len = 15; /* for --help-advanced etc. */
    for (int cnt = 0; groups[cnt].id; cnt++)
    {
        if (groups[cnt].help)
        {
            int l = get_max_opt_len_for_help(groups[cnt].help);
            if (l > max_len)
                max_len = l;
        }
    }
    return max_len;
}

static void print_help_items(const help_t *h, int l, int opt_col, int term_width)
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

void show_help(const char *prog_name, int mode, const char *lib_version, const help_group_t *groups)
{
    int cnt;
    int term_width = get_terminal_width();
    int max_opt_len = groups ? get_global_max_opt_len(groups) : 20;
    int opt_col = 4 + max_opt_len + 2;

    if (opt_col > term_width - 15)
        opt_col = term_width - 15;
    if (opt_col < 20)
        opt_col = 20;

    const char *name = (prog_name && *prog_name) ? prog_name : "faac";

    char upper_name[64];
    size_t i = 0;
    for (; name[i] && i < sizeof(upper_name) - 1; i++)
    {
        upper_name[i] = (char)toupper((unsigned char)name[i]);
    }
    upper_name[i] = '\0';

    char ver_buf[128];
    printf("%s %s\n", upper_name, faac_version_string(ver_buf, sizeof(ver_buf), lib_version));
    printf("Usage: %s [options] infile\n\n", name);

    if (!groups)
        return;

    switch (mode)
    {
    case '?':
    case 'h':
    case 'H':
        printf("Help options:\n");
        {
            char short_help_msg[128];
            char desc_help_msg[128];
            char license_help_msg[128];
            snprintf(short_help_msg, sizeof(short_help_msg), "Short help on using %s", upper_name);
            snprintf(desc_help_msg, sizeof(desc_help_msg), "Description of all options for %s.", upper_name);
            snprintf(license_help_msg, sizeof(license_help_msg), "License terms for %s.", upper_name);

            const help_t general_help[] = {
                {"-h", short_help_msg, NULL},
                {"-H", desc_help_msg, NULL},
                {"--license", license_help_msg, NULL},
                {NULL, NULL, NULL}
            };

            for (cnt = 0; general_help[cnt].opt; cnt++)
            {
                printf("    %s", general_help[cnt].opt);
                int len = 4 + (int)strlen(general_help[cnt].opt);
                for (int j = len; j < opt_col; j++)
                    putchar(' ');
                print_wrapped_text(general_help[cnt].shorthelp, opt_col, opt_col, term_width);
            }
            for (cnt = 0; groups[cnt].id; cnt++)
            {
                printf("    %s", groups[cnt].option);
                int len = 4 + (int)strlen(groups[cnt].option);
                for (int j = len; j < opt_col; j++)
                    putchar(' ');
                print_wrapped_text(groups[cnt].name, opt_col, opt_col, term_width);
            }
        }
        printf("\n");
        if (mode == 'h')
        {
            for (cnt = 0; cnt < 2 && groups[cnt].id; cnt++)
            {
                printf("%s:\n", groups[cnt].name);
                print_help_items(groups[cnt].help, 0, opt_col, term_width);
            }
        }
        if (mode == 'H')
        {
            for (cnt = 0; groups[cnt].id; cnt++)
            {
                printf("%s:\n", groups[cnt].name);
                print_help_items(groups[cnt].help, 1, opt_col, term_width);
            }
        }
        break;
    default:
        for (cnt = 0; groups[cnt].id; cnt++)
            if (groups[cnt].id == mode)
            {
                printf("%s:\n", groups[cnt].name);
                print_help_items(groups[cnt].help, 1, opt_col, term_width);
                break;
            }
        break;
    }
}
