/*
 * Shared CLI helpers for the faac/faad/faam frontends.
 */

#ifndef CLI_COMMON_H
#define CLI_COMMON_H

#ifdef _WIN32
#include "charset.h"
#else
#include <stdio.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* fopen() a UTF-8 path: win32_fopen_utf8() on Windows, plain fopen()
   elsewhere. Same open-failure semantics as fopen() either way -- callers
   still check the returned NULL and report the error themselves. */
FILE *cli_fopen(const char *path, const char *mode);

/* Format a CLI version with the build's short Git revision when available. */
const char *cli_version_string(char *buf, size_t buf_size, const char *version);

/* Print project attribution and the shared LGPL-2.1-or-later notice. */
void cli_print_license(const char *name, const char *copyright);
void cli_print_lgpl_notice(FILE *stream);

#ifdef __cplusplus
}
#endif

#endif /* CLI_COMMON_H */
