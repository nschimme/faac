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

#ifndef CLI_IO_H
#define CLI_IO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "faam.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Read a --cover-art file into a malloc'd buffer after checking its size and
   that it is a GIF, JPEG or PNG. Returns NULL, or an error message
   (newline-terminated); *data stays NULL on error. */
const char *load_cover_art(const char *path, uint8_t **data, uint64_t *size);

/* Resolve a --creation-time value to Unix seconds: "auto" is the mtime of
   input_filename, "now" the current time, otherwise decimal seconds; NULL
   spec uses SOURCE_DATE_EPOCH. Returns false, with a newline-terminated error
   in msg, for a value outside 0..4294967295, which the file cannot hold. Text
   that is not a number gives 0 and a warning in msg (empty when all is well). */
bool resolve_creation_time(const char *spec, const char *input_filename,
                           uint32_t *out, char *msg, size_t msg_size);

#ifdef _WIN32
/* fopen() on a UTF-8 path: converts to UTF-16 and calls _wfopen(), since the
   narrow CRT's fopen() interprets its argument in the current ANSI code
   page, not UTF-8. */
FILE *win32_fopen_utf8(const char *utf8_path, const char *mode);

/* access() on a UTF-8 path, same rationale as win32_fopen_utf8(). */
int win32_access_utf8(const char *utf8_path, int amode);

/* mtime of a UTF-8 path, same rationale as win32_fopen_utf8(). Returns 0 and
   sets *mtime on success, -1 on failure (path not found, etc). */
int win32_mtime_utf8(const char *utf8_path, time_t *mtime);
#endif

/* fopen() a UTF-8 path: win32_fopen_utf8() on Windows, plain fopen()
   elsewhere. Same open-failure semantics as fopen() either way -- callers
   still check the returned NULL and report the error themselves. */
FILE *cli_fopen(const char *path, const char *mode);

/* True if path can be opened for reading, i.e. writing there would overwrite. */
bool cli_file_exists(const char *path);

/* True if both paths exist and are one file: the same path spelled two ways, through a
   symbolic link, or a hard link. Writing the output over such an input would destroy it. */
bool cli_same_file(const char *path1, const char *path2);

/* remove() a UTF-8 path (same rationale as win32_fopen_utf8()); on POSIX only a
   regular file is removed, so a device given as an output survives a failed run. */
int cli_remove(const char *path);

/* 64-bit file offsets for the frontends: a 32-bit long, as on MSVC and 32-bit Linux, cannot
   address a file past 2 GiB, so seek and tell go through fseeko/ftello (_fseeki64/_ftelli64
   on Windows). */
bool cli_fseek(FILE *f, uint64_t offset);
uint64_t cli_ftell(FILE *f);

/* Length of the file in bytes. The position is left at the start. */
bool cli_fsize(FILE *f, uint64_t *size);

/* The whole stream in a malloc'd buffer (free it), or false when it is longer than max bytes or
   memory runs out. A regular file is read from its start in one go; a pipe, which has no
   length, is read from where it stands. */
bool cli_read_all(FILE *f, size_t max, uint8_t **buf, size_t *len);

/* The libfaam stream callbacks over a stdio FILE; user_data is the FILE *. */
faam_io cli_faam_io(FILE *f);

#ifdef __cplusplus
}
#endif

#endif /* CLI_IO_H */
