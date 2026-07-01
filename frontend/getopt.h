#ifndef FAAC_GETOPT_H
#define FAAC_GETOPT_H

/*
 * FAAC getopt proxy header.
 * This file either includes the system getopt.h or provides a fallback
 * implementation for platforms that lack it (e.g., MSVC).
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_GETOPT_H
/* We have a system getopt.h. Use it, but ensure GNU extensions are exposed. */
# ifndef _GNU_SOURCE
#  define _GNU_SOURCE
# endif
# if defined(__GNUC__) || defined(__clang__)
#  include_next <getopt.h>
# else
#  include <getopt.h>
# endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Standard getopt variables.
 * These are always needed by main.c. We declare them here as extern to
 * ensure they are visible even if the system header didn't declare them
 * (common on some Windows/MinGW environments).
 */
extern char *optarg;
extern int optind;
extern int opterr;
extern int optopt;

/*
 * getopt_long and struct option.
 * We provide these if they were not provided by a system header.
 * We use common guards to avoid redefinition conflicts.
 */
#if !defined(_GETOPT_H) && !defined(_GETOPT_H_) && !defined(__GETOPT_H__) && !defined(__GETOPT_LONG_H__)

struct option {
    const char *name;
    int has_arg;
    int *flag;
    int val;
};

#define no_argument       0
#define required_argument 1
#define optional_argument 2

extern int getopt(int nargc, char * const *nargv, const char *options);
extern int getopt_long(int nargc, char * const *nargv, const char *options,
    const struct option *long_options, int *idx);
extern int getopt_long_only(int nargc, char * const *nargv, const char *options,
    const struct option *long_options, int *idx);

#endif /* !defined(_GETOPT_H) ... */

#ifdef __cplusplus
}
#endif

#endif /* FAAC_GETOPT_H */
