/*
 * Shared CLI helpers for the faac/faad/faam frontends.
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
    printf("%s\n", name);
    if (copyright)
        printf("%s\n", copyright);
    cli_print_lgpl_notice(stdout);
}

void cli_print_lgpl_notice(FILE *stream) {
    fprintf(stream, "\nThis software is free software; you can redistribute it and/or\n"
                    "modify it under the terms of the GNU Lesser General Public\n"
                    "License as published by the Free Software Foundation; either\n"
                    "version 2.1 of the License, or (at your option) any later version.\n"
                    "\n"
                    "This software is distributed in the hope that it will be useful,\n"
                    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
                    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU\n"
                    "Lesser General Public License for more details.\n\n");
}
