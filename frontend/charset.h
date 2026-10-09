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

#ifndef CHARSET_H
#define CHARSET_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize character handling and UTF-8 console output where applicable */
void cli_init_console(void);

/* Ensure string is valid UTF-8, converting from system encoding if needed */
char *utf8_ensure(const char *str);

/* Trim surrounding whitespace, ASCII quotes, and UTF-8 Unicode curly quotes */
char *trim_quotes_and_spaces(char *s);

/* Split a --tag argument at its first '=' or ',', trim both halves, and check
   them. Returns NULL, or an error message (newline-terminated) for the CLI
   to print. arg is modified; name and value point into it. */
const char *parse_tag_arg(char *arg, char **name, char **value);

/* Parse "n" or "n/total" (track and disc numbers); total is left alone when
   the argument has none. Returns false when n is missing. */
bool parse_index_arg(const char *arg, uint16_t *n, uint16_t *total);

/* Parse a genre argument (number or string name) into ID3v1 genre_id and genre_name */
bool parse_genre(const char *arg, uint16_t *genre_id, const char **genre_name);

#ifdef _WIN32
#include <windows.h>
/* Convert UTF-16 wchar_t string to heap-allocated UTF-8 string */
char *win32_utf16_to_utf8(const wchar_t *wstr);

/* Convert UTF-8 string to heap-allocated UTF-16 wchar_t string */
wchar_t *win32_utf8_to_utf16(const char *utf8_str);

#endif

/* Format a CLI version with the build's short Git revision when available. */
const char *cli_version_string(char *buf, size_t buf_size, const char *version);

/* Print project attribution and the shared LGPL-2.1-or-later notice. */
void cli_print_patent_notice(FILE *stream);
void cli_print_lgpl_notice(FILE *stream, const char *subject);

#ifdef __cplusplus
}
#endif

#endif /* CHARSET_H */
