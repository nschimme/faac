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

/* Replays files through a harness when no fuzzing engine is linked, so the
 * corpus runs as a plain regression test on every compiler. A directory
 * argument replays its files, as with libFuzzer. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static int run_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf = NULL;
    size_t cap = 0, len = 0, n;

    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }
    do {
        if (len == cap) {
            uint8_t *p;
            cap = cap ? cap * 2 : 65536;
            p = realloc(buf, cap);
            if (!p) {
                free(buf);
                fclose(f);
                return 1;
            }
            buf = p;
        }
        n = fread(buf + len, 1, cap - len, f);
        len += n;
    } while (n);
    fclose(f);

    LLVMFuzzerTestOneInput(buf, len);
    free(buf);
    return 0;
}

static int run_path(const char *path)
{
    int rc = 0;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    char pattern[MAX_PATH];
    HANDLE h;

    if (GetFileAttributesA(path) & FILE_ATTRIBUTE_DIRECTORY) {
        snprintf(pattern, sizeof(pattern), "%s\\*", path);
        h = FindFirstFileA(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE)
            return 1;
        do {
            char full[MAX_PATH];
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            snprintf(full, sizeof(full), "%s\\%s", path, fd.cFileName);
            rc |= run_file(full);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
        return rc;
    }
#else
    struct stat st;

    if (!stat(path, &st) && S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        struct dirent *e;

        if (!d)
            return 1;
        while ((e = readdir(d))) {
            char *full;
            size_t sz;
            if (e->d_name[0] == '.')
                continue;
            sz = strlen(path) + strlen(e->d_name) + 2;
            full = malloc(sz);
            if (!full)
                break;
            snprintf(full, sz, "%s/%s", path, e->d_name);
            rc |= run_file(full);
            free(full);
        }
        closedir(d);
        return rc;
    }
#endif
    return run_file(path);
}

int main(int argc, char **argv)
{
    int i, rc = 0;

    for (i = 1; i < argc; i++) {
        /* libFuzzer options pass through the same command line. */
        if (argv[i][0] == '-')
            continue;
        rc |= run_path(argv[i]);
    }
    return rc;
}
