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

#include "faam.h"

#ifdef __cplusplus
extern "C" {
#endif

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
