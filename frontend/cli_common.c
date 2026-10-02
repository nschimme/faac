/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2001 Menno Bakker
 * Copyright (C) 2002-2017 Krzysztof Nikiel
 * Copyright (C) 2004 Dan Villiom P. Christiansen
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

#include "cli_common.h"
#include "git_version.h"

#include <stdio.h>

FILE *cli_fopen(const char *path, const char *mode) {
#ifdef _WIN32
    return win32_fopen_utf8(path, mode);
#else
    return fopen(path, mode);
#endif
}

const char *cli_version_string(char *buf, size_t buf_size, const char *version) {
    if (FAAC_GIT_VERSION[0])
        snprintf(buf, buf_size, "%s (%s)", version, FAAC_GIT_VERSION);
    else
        snprintf(buf, buf_size, "%s", version);
    return buf;
}

void cli_print_license(const char *name, const char *copyright) {
    fprintf(stderr, "%s\n", name);
    if (copyright)
        fprintf(stderr, "%s\n", copyright);
    fprintf(stderr, "\n");
    cli_print_lgpl_notice(stderr, "library");
}

void cli_print_lgpl_notice(FILE *stream, const char *subject) {
    fprintf(stream, "This %s is free software; you can redistribute it and/or\n", subject);
    fprintf(stream,
                    "modify it under the terms of the GNU Lesser General Public\n"
                    "License as published by the Free Software Foundation; either\n"
                    "version 2.1 of the License, or (at your option) any later version.\n"
                    "\n"
                    "This %s is distributed in the hope that it will be useful,\n"
                    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
                    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU\n"
                    "Lesser General Public License for more details.\n\n", subject);
}
