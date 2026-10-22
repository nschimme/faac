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

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "cli_parse.h"
#include "cli_io.h"

static void test_uint(void)
{
    unsigned long long value = 99;
    assert(cli_parse_uint("0", 10, &value) && value == 0);
    assert(cli_parse_uint("4294967295", UINT32_MAX, &value) && value == UINT32_MAX);
    assert(!cli_parse_uint(NULL, 10, &value));
    assert(!cli_parse_uint("1", 10, NULL));
    assert(!cli_parse_uint("", 10, &value));
    assert(!cli_parse_uint("-1", 10, &value));
    assert(!cli_parse_uint("+1", 10, &value));
    assert(!cli_parse_uint("1x", 10, &value));
    assert(!cli_parse_uint("11", 10, &value));
    assert(!cli_parse_uint("18446744073709551616", ULLONG_MAX, &value));
}

static void test_fps(void)
{
    uint32_t num = 0, den = 0;
    char too_long[65];
    memset(too_long, '1', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = '\0';
    assert(cli_parse_fps("30", &num, &den) && num == 30 && den == 1);
    assert(cli_parse_fps("30000/1001", &num, &den) && num == 30000 && den == 1001);
    assert(!cli_parse_fps(NULL, &num, &den));
    assert(!cli_parse_fps("30", NULL, &den));
    assert(!cli_parse_fps("30", &num, NULL));
    assert(!cli_parse_fps(too_long, &num, &den));
    assert(!cli_parse_fps("0", &num, &den));
    assert(!cli_parse_fps("30/0", &num, &den));
    assert(!cli_parse_fps("30/1000001", &num, &den));
    assert(!cli_parse_fps("30/1/2", &num, &den));
    assert(!cli_parse_fps("30x", &num, &den));
}

static void test_chapter_line(void)
{
    static const struct {
        const char *line;
        uint64_t start_ms;
        const char *title;
    } valid[] = {
        {"00:00:01.5\tHalf\n", 1500, "Half"},
        {"00:00:02.05\tFiftieth\n", 2050, "Fiftieth"},
        {"00:00:03.123\tExact\r\n", 3123, "Exact"},
        {"01:02:03.004\tPadded", 3723004, "Padded"},
        {"00:00:00.0\t", 0, ""},
    };
    static const char *invalid[] = {
        "00:00:01.1234\tX\n",
        "00:00:01\tX\n",
        "00:00:01.\tX\n",
        "00:00:01.-5\tX\n",
        "00:60:01.0\tX\n",
        "00:00:60.0\tX\n",
        "00:00:01.0 X\n",
    };
    char title[32];
    uint64_t start_ms;
    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
        assert(cli_parse_chapter_line(valid[i].line, &start_ms, title, sizeof(title)));
        assert(start_ms == valid[i].start_ms);
        assert(strcmp(title, valid[i].title) == 0);
    }
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
        assert(!cli_parse_chapter_line(invalid[i], &start_ms, title, sizeof(title)));

    assert(cli_parse_chapter_line("00:00:00.0\tLong title", &start_ms, title, 5));
    assert(strcmp(title, "Long") == 0);
    assert(!cli_parse_chapter_line(NULL, &start_ms, title, sizeof(title)));
    assert(!cli_parse_chapter_line("00:00:00.0\tTitle", NULL, title, sizeof(title)));
    assert(!cli_parse_chapter_line("00:00:00.0\tTitle", &start_ms, NULL, sizeof(title)));
    assert(!cli_parse_chapter_line("00:00:00.0\tTitle", &start_ms, title, 0));
    assert(!cli_parse_chapter_line("not a timestamp", &start_ms, title, sizeof(title)));
}

static void set_source_date_epoch(const char *value)
{
#ifdef _WIN32
    assert(_putenv_s("SOURCE_DATE_EPOCH", value ? value : "") == 0);
#else
    if (value)
        assert(setenv("SOURCE_DATE_EPOCH", value, 1) == 0);
    else
        assert(unsetenv("SOURCE_DATE_EPOCH") == 0);
#endif
}

static void test_creation_time(void)
{
    uint32_t value = 17;
    char message[128];
    const char *old_epoch = getenv("SOURCE_DATE_EPOCH");
    char *saved_epoch = NULL;
    if (old_epoch) {
        saved_epoch = malloc(strlen(old_epoch) + 1);
        assert(saved_epoch);
        strcpy(saved_epoch, old_epoch);
    }

    assert(resolve_creation_time("1700000000", NULL, &value, message, sizeof(message)));
    assert(value == 1700000000 && message[0] == '\0');
    assert(resolve_creation_time("4102444800", NULL, &value, message, sizeof(message)));
    assert(value == 4102444800U && message[0] == '\0');
    assert(resolve_creation_time("4294967295", NULL, &value, message, sizeof(message)));
    assert(value == UINT32_MAX && message[0] == '\0');
    assert(resolve_creation_time("soon", NULL, &value, message, sizeof(message)));
    assert(value == 0 && strstr(message, "invalid creation time") != NULL);
    assert(!resolve_creation_time("4294967296", NULL, &value, message, sizeof(message)));
    assert(strstr(message, "out of range") != NULL);
    assert(!resolve_creation_time("-1", NULL, &value, message, sizeof(message)));
    assert(strstr(message, "out of range") != NULL);
    assert(!resolve_creation_time("7258118400", NULL, &value, message, sizeof(message)));
    assert(strstr(message, "out of range") != NULL);
    assert(!resolve_creation_time("9223372036854775808", NULL, &value, message, sizeof(message)));
    assert(strstr(message, "out of range") != NULL);

    set_source_date_epoch(NULL);
    assert(resolve_creation_time(NULL, NULL, &value, message, sizeof(message)));
    assert(value == 0 && message[0] == '\0');
    set_source_date_epoch("1600000000");
    assert(resolve_creation_time(NULL, NULL, &value, message, sizeof(message)));
    assert(value == 1600000000 && message[0] == '\0');
    set_source_date_epoch("soon");
    assert(resolve_creation_time(NULL, NULL, &value, message, sizeof(message)));
    assert(value == 0 && strstr(message, "invalid SOURCE_DATE_EPOCH") != NULL);
    set_source_date_epoch("4294967296");
    assert(!resolve_creation_time(NULL, NULL, &value, message, sizeof(message)));
    assert(strstr(message, "SOURCE_DATE_EPOCH") != NULL && strstr(message, "out of range") != NULL);

    assert(resolve_creation_time("auto", NULL, &value, message, sizeof(message)));
    assert(value == 0 && strstr(message, "stdin") != NULL);
    assert(resolve_creation_time("auto", "-", &value, message, sizeof(message)));
    assert(value == 0 && strstr(message, "stdin") != NULL);
    assert(resolve_creation_time("auto", "cli-no-such-input-for-creation-time", &value,
                                 message, sizeof(message)));
    assert(value == 0 && strstr(message, "couldn't stat()") != NULL);
    assert(resolve_creation_time("now", NULL, &value, message, sizeof(message)));
    assert(value > 0 && message[0] == '\0');

    set_source_date_epoch(saved_epoch);
    free(saved_epoch);
}

int main(void)
{
    test_uint();
    test_fps();
    test_chapter_line();
    test_creation_time();
    return 0;
}
