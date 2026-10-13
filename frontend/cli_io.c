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

/* Must precede every system header: it widens off_t for fseeko/ftello on 32-bit POSIX. */
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#include <stdio.h>
#ifndef _WIN32
#include <sys/types.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#endif

#include "cli_io.h"
#include "cover_art.h"
#include "charset.h"

static int32_t cli_io_read(void *user_data, void *buf, uint32_t bytes)
{
    FILE *f = (FILE *)user_data;
    size_t got = fread(buf, 1, bytes, f);
    /* A short count is end of data unless the stream failed, which the library needs to hear. */
    if (got < bytes && ferror(f)) return -1;
    return (int32_t)got;
}

static int32_t cli_io_write(void *user_data, const void *buf, uint32_t bytes)
{
    return fwrite(buf, 1, bytes, (FILE *)user_data) == bytes ? (int32_t)bytes : -1;
}

bool cli_fseek(FILE *f, uint64_t offset)
{
#ifdef _WIN32
    return _fseeki64(f, (__int64)offset, SEEK_SET) == 0;
#else
    return fseeko(f, (off_t)offset, SEEK_SET) == 0;
#endif
}

uint64_t cli_ftell(FILE *f)
{
#ifdef _WIN32
    return (uint64_t)_ftelli64(f);
#else
    return (uint64_t)ftello(f);
#endif
}

static bool cli_io_seek(void *user_data, uint64_t offset) { return cli_fseek((FILE *)user_data, offset); }
static uint64_t cli_io_tell(void *user_data) { return cli_ftell((FILE *)user_data); }

static bool cli_io_flush(void *user_data)
{
    return fflush((FILE *)user_data) == 0;
}

faam_io cli_faam_io(FILE *f)
{
    faam_io io = { sizeof(faam_io), f, cli_io_read, cli_io_write, cli_io_seek, cli_io_tell, cli_io_flush };
    return io;
}

bool cli_fsize(FILE *f, uint64_t *size)
{
#ifdef _WIN32
    if (_fseeki64(f, 0, SEEK_END) != 0) return false;
    __int64 end = _ftelli64(f);
    if (_fseeki64(f, 0, SEEK_SET) != 0) return false;
#else
    if (fseeko(f, 0, SEEK_END) != 0) return false;
    off_t end = ftello(f);
    if (fseeko(f, 0, SEEK_SET) != 0) return false;
#endif
    if (end < 0) return false;
    *size = (uint64_t)end;
    return true;
}

bool cli_read_all(FILE *f, size_t max, uint8_t **buf, size_t *len)
{
    /* A regular file is read in one go; a pipe has no length and grows the buffer instead. */
    uint64_t known = 0;
    size_t cap = cli_fsize(f, &known) && known < max ? (size_t)known + 1 : 1 << 16, used = 0;
    uint8_t *data = (uint8_t *)malloc(cap);
    *buf = NULL;
    *len = 0;
    if (!data) return false;
    for (;;) {
        size_t got = fread(data + used, 1, cap - used, f);
        used += got;
        if (used < cap) break; /* end of data, or a read error the caller sees through ferror */
        if (cap > max) { free(data); return false; }
        size_t grown = cap * 2 > max + 1 ? max + 1 : cap * 2;
        uint8_t *bigger = (uint8_t *)realloc(data, grown);
        if (!bigger) { free(data); return false; }
        data = bigger;
        cap = grown;
    }
    if (used > max || ferror(f)) { free(data); return false; }
    *buf = data;
    *len = used;
    return true;
}

#define COVER_ART_MAX_BYTES ((size_t)32 * 1024 * 1024)

const char *load_cover_art(const char *path, uint8_t **data, uint64_t *size)
{
    const char *err = NULL;
    FILE *f = cli_fopen(path, "rb");

    *data = NULL;
    *size = 0;
    if (!f)
        return "Error opening cover art file!\n";

    uint64_t sz = 0;
    if (!cli_fsize(f, &sz))
        sz = 0;
    clearerr(f);

    if (sz == 0 || sz > COVER_ART_MAX_BYTES)
    {
        err = "Invalid cover art file size!\n";
    }
    else
    {
        uint8_t *buf = malloc((size_t)sz);
        if (!buf)
            err = "Out of memory reading cover art file!\n";
        else if (fread(buf, 1, (size_t)sz, f) != (size_t)sz)
            err = "Error reading cover art file!\n";
        else if (faam_detect_cover_type(buf, (uint32_t)sz) == FAAM_COVER_AUTO)
            err = "Unsupported cover image file format!\n";

        if (err)
        {
            free(buf);
        }
        else
        {
            *data = buf;
            *size = (uint64_t)sz;
        }
    }
    fclose(f);
    return err;
}

/* The muxer takes 32-bit Unix seconds; a wider or negative value would wrap
   into a different date, so it is refused instead. */
static bool unix_seconds_fit(long long v)
{
    return v >= 0 && (unsigned long long)v <= 0xFFFFFFFFULL;
}

/* 0: parsed into *t, 1: not a number, 2: does not fit 32-bit Unix seconds. */
static int parse_unix_seconds(const char *str, uint32_t *t)
{
    char *endptr;
    errno = 0;
    long long v = strtoll(str, &endptr, 10);
    if (errno == ERANGE)
        return 2;
    if (errno != 0 || *endptr != '\0')
        return 1;
    if (!unix_seconds_fit(v))
        return 2;
    *t = (uint32_t)v;
    return 0;
}

bool resolve_creation_time(const char *spec, const char *input_filename,
                           uint32_t *out, char *msg, size_t msg_size)
{
    uint32_t t = 0;

    msg[0] = '\0';
    if (!spec)
    {
        const char *sde = getenv("SOURCE_DATE_EPOCH");
        if (sde)
        {
            int rc = parse_unix_seconds(sde, &t);
            if (rc == 2)
            {
                snprintf(msg, msg_size, "SOURCE_DATE_EPOCH %s is out of range (0 to 4294967295)\n", sde);
                return false;
            }
            if (rc == 1)
            {
                snprintf(msg, msg_size, "invalid SOURCE_DATE_EPOCH %s, ignoring\n", sde);
                t = 0;
            }
        }
    }
    else if (!strcmp(spec, "auto"))
    {
        if (input_filename && strcmp(input_filename, "-") != 0)
        {
            time_t mtime;
#ifdef _WIN32
            bool ok = win32_mtime_utf8(input_filename, &mtime) == 0;
#else
            struct stat st;
            bool ok = stat(input_filename, &st) == 0;
            mtime = st.st_mtime;
#endif
            if (ok)
            {
                if (!unix_seconds_fit((long long)mtime))
                {
                    snprintf(msg, msg_size, "creation time of %s is out of range (0 to 4294967295)\n", input_filename);
                    return false;
                }
                t = (uint32_t)mtime;
            }
            else
                snprintf(msg, msg_size, "couldn't stat() input file %s, defaulting to 0\n", input_filename);
        }
        else
        {
            snprintf(msg, msg_size, "cannot use --creation-time auto with stdin, defaulting to 0\n");
        }
    }
    else if (!strcmp(spec, "now"))
    {
        long long now = (long long)time(NULL);
        if (!unix_seconds_fit(now))
        {
            snprintf(msg, msg_size, "the clock is out of range for a creation time (0 to 4294967295)\n");
            return false;
        }
        t = (uint32_t)now;
    }
    else
    {
        int rc = parse_unix_seconds(spec, &t);
        if (rc == 2)
        {
            snprintf(msg, msg_size, "creation time %s is out of range (0 to 4294967295)\n", spec);
            return false;
        }
        if (rc == 1)
        {
            snprintf(msg, msg_size, "invalid creation time %s, defaulting to 0\n", spec);
            t = 0;
        }
    }
    *out = t;
    return true;
}

bool cli_file_exists(const char *path)
{
    FILE *f = cli_fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

bool cli_same_file(const char *path1, const char *path2)
{
#ifdef _WIN32
    /* Volume serial and file index name a file whatever path or hard link reached it. */
    bool same = false;
    wchar_t *w1 = win32_utf8_to_utf16(path1);
    wchar_t *w2 = win32_utf8_to_utf16(path2);
    if (w1 && w2)
    {
        const DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
        HANDLE h1 = CreateFileW(w1, 0, share, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        HANDLE h2 = CreateFileW(w2, 0, share, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        BY_HANDLE_FILE_INFORMATION i1, i2;
        if (h1 != INVALID_HANDLE_VALUE && h2 != INVALID_HANDLE_VALUE &&
            GetFileInformationByHandle(h1, &i1) && GetFileInformationByHandle(h2, &i2))
            same = i1.dwVolumeSerialNumber == i2.dwVolumeSerialNumber &&
                   i1.nFileIndexHigh == i2.nFileIndexHigh && i1.nFileIndexLow == i2.nFileIndexLow;
        if (h1 != INVALID_HANDLE_VALUE) CloseHandle(h1);
        if (h2 != INVALID_HANDLE_VALUE) CloseHandle(h2);
    }
    free(w1);
    free(w2);
    return same;
#else
    struct stat s1, s2;
    return stat(path1, &s1) == 0 && stat(path2, &s2) == 0 && s1.st_dev == s2.st_dev && s1.st_ino == s2.st_ino;
#endif
}

int cli_remove(const char *path)
{
#ifdef _WIN32
    wchar_t *wpath = win32_utf8_to_utf16(path);
    if (!wpath)
        return -1;
    int ret = _wremove(wpath);
    free(wpath);
    return ret;
#else
    /* A failed run must not unlink a device the user pointed -o at, e.g. /dev/null. */
    struct stat st;
    if (stat(path, &st) == 0 && !S_ISREG(st.st_mode))
        return 0;
    return remove(path);
#endif
}


#ifdef _WIN32
FILE *win32_fopen_utf8(const char *utf8_path, const char *mode)
{
    if (!utf8_path || !mode)
        return NULL;

    wchar_t *wpath = win32_utf8_to_utf16(utf8_path);
    if (!wpath)
        return NULL;

    wchar_t *wmode = win32_utf8_to_utf16(mode);
    if (!wmode)
    {
        free(wpath);
        return NULL;
    }

    FILE *f = _wfopen(wpath, wmode);
    free(wpath);
    free(wmode);
    return f;
}

int win32_access_utf8(const char *utf8_path, int amode)
{
    wchar_t *wpath = win32_utf8_to_utf16(utf8_path);
    if (!wpath)
        return -1;

    int ret = _waccess(wpath, amode);
    free(wpath);
    return ret;
}

int win32_mtime_utf8(const char *utf8_path, time_t *mtime)
{
    wchar_t *wpath = win32_utf8_to_utf16(utf8_path);
    if (!wpath)
        return -1;

    struct _stat64 st;
    int ret = _wstat64(wpath, &st);
    free(wpath);
    if (ret == 0)
        *mtime = (time_t)st.st_mtime;
    return ret;
}
#endif

FILE *cli_fopen(const char *path, const char *mode) {
#ifdef _WIN32
    return win32_fopen_utf8(path, mode);
#else
    return fopen(path, mode);
#endif
}
