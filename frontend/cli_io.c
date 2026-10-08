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

#include "cli_io.h"

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
