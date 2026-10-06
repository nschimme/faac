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

#ifndef HELP_H
#define HELP_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *opt;
    const char *shorthelp;
    const char *longhelp;
} help_t;

typedef struct {
    int id;
    const char *name;
    const char *option;
    const help_t *help;
} help_group_t;

void show_help(const char *prog_name, int mode, const char *lib_version, const help_group_t *groups);

#ifdef __cplusplus
}
#endif

#endif /* HELP_H */
