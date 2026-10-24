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

#include "cli_parse.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool cli_parse_uint(const char *text, unsigned long long max, unsigned long long *out)
{
    char *end;
    if (!text || !out || *text < '0' || *text > '9') return false;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno || *end || value > max) return false;
    *out = value;
    return true;
}

bool cli_parse_rate(const char *text, uint32_t min_rate, uint32_t max_rate, uint32_t *out)
{
    unsigned long long value;
    if (!out || min_rate == 0 || max_rate < min_rate ||
        !cli_parse_uint(text, max_rate, &value) || value < min_rate)
        return false;
    *out = (uint32_t)value;
    return true;
}

bool cli_is_aac_sample_rate(uint32_t rate)
{
    static const uint32_t rates[] = {
        96000, 88200, 64000, 48000, 44100, 32000, 24000,
        22050, 16000, 12000, 11025, 8000, 7350
    };
    for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); i++)
        if (rate == rates[i]) return true;
    return false;
}

bool cli_parse_verbosity(const char *text, uint8_t *out)
{
    unsigned long long value;
    if (!out || !cli_parse_uint(text, CLI_VERBOSITY_MAX, &value))
        return false;
    *out = (uint8_t)value;
    return true;
}

bool cli_parse_fps(const char *text, uint32_t *num, uint32_t *den)
{
    char buf[64];
    char *slash;
    unsigned long long n, d = 1;

    if (!text || !num || !den || strlen(text) >= sizeof(buf)) return false;
    strcpy(buf, text);
    slash = strchr(buf, '/');
    if (slash) *slash++ = '\0';
    if (!cli_parse_uint(buf, 1000000, &n) || n == 0) return false;
    if (slash && (!cli_parse_uint(slash, 1000000, &d) || d == 0)) return false;
    *num = (uint32_t)n;
    *den = (uint32_t)d;
    return true;
}

bool cli_parse_chapter_line(const char *line, uint64_t *start_ms, char *title, size_t title_size)
{
    unsigned hh, mm, ss_i;
    int consumed = 0;
    if (!line || !start_ms || !title || title_size == 0
        || sscanf(line, "%u:%u:%u%n", &hh, &mm, &ss_i, &consumed) != 3) return false;
    if (mm > 59 || ss_i > 59) return false;

    /* The fraction is a decimal fraction of a second, one to three digits: ".5" is 500 ms. */
    const char *rest = line + consumed;
    if (*rest != '.') return false;
    rest++;
    unsigned ms = 0, digits = 0;
    while (rest[digits] >= '0' && rest[digits] <= '9') {
        if (digits == 3) return false;
        ms = ms * 10 + (unsigned)(rest[digits] - '0');
        digits++;
    }
    if (digits == 0) return false;
    for (unsigned i = digits; i < 3; i++) ms *= 10;
    rest += digits;

    if (*rest != '\t') return false;
    rest++;

    size_t len = strlen(rest);
    while (len > 0 && (rest[len - 1] == '\n' || rest[len - 1] == '\r')) len--;
    if (len >= title_size) len = title_size - 1;
    memcpy(title, rest, len);
    title[len] = '\0';
    *start_ms = ((uint64_t)hh * 3600 + (uint64_t)mm * 60 + ss_i) * 1000 + ms;
    return true;
}
