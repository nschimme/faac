/* Getopt for Microsoft C
   This code is a modification of the Free Software Foundation, Inc.
   Getopt library for parsing command line argument the purpose was
   to provide a Microsoft Visual C friendly derivative. This code
   provides functionality for both Unicode and Multibyte builds.

   Date: 02/03/2011 - Ludvik Jerabek - Initial Release
   Version: 1.1
   Comment: Supports getopt, getopt_long, and getopt_long_only
   and POSIXLY_CORRECT environment flag
   License: LGPL

   Revisions:
   02/03/2011 - Ludvik Jerabek - Initial Release
   02/20/2011 - Ludvik Jerabek - Fixed compiler warnings at Level 4
   07/05/2011 - Ludvik Jerabek - Added no_argument, required_argument, optional_argument defs
   08/03/2011 - Ludvik Jerabek - Fixed non-argument runtime bug which caused runtime exception
   08/09/2011 - Ludvik Jerabek - Added code to export functions for DLL and LIB
   02/15/2012 - Ludvik Jerabek - Fixed _GETOPT_THROW definition missing in implementation file
   08/01/2012 - Ludvik Jerabek - Created separate functions for char and wchar_t characters so single dll can do both unicode and ansi
   10/15/2012 - Ludvik Jerabek - Modified to match latest GNU features
   06/19/2015 - Ludvik Jerabek - Fixed maximum option limitation caused by option_a (255) and option_w (65535) structure val variable
   09/24/2022 - Ludvik Jerabek - Updated to match most recent getopt release
   09/25/2022 - Ludvik Jerabek - Fixed memory allocation (malloc call) issue for wchar_t*


   **DISCLAIMER**
   THIS MATERIAL IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND,
   EITHER EXPRESS OR IMPLIED, INCLUDING, BUT Not LIMITED TO, THE
   IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
   PURPOSE, OR NON-INFRINGEMENT. SOME JURISDICTIONS DO NOT ALLOW THE
   EXCLUSION OF IMPLIED WARRANTIES, SO THE ABOVE EXCLUSION MAY NOT
   APPLY TO YOU. IN NO EVENT WILL I BE LIABLE TO ANY PARTY FOR ANY
   DIRECT, INDIRECT, SPECIAL OR OTHER CONSEQUENTIAL DAMAGES FOR ANY
   USE OF THIS MATERIAL INCLUDING, WITHOUT LIMITATION, ANY LOST
   PROFITS, BUSINESS INTERRUPTION, LOSS OF PROGRAMS OR OTHER DATA ON
   YOUR INFORMATION HANDLING SYSTEM OR OTHERWISE, EVEN If WE ARE
   EXPRESSLY ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
*/

#ifndef FAAC_GETOPT_H
#define FAAC_GETOPT_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_GETOPT_H
#if defined(__GNUC__) || defined(__clang__)
#include_next <getopt.h>
#else
#include <getopt.h>
#endif
#else

#ifdef  __cplusplus
extern "C" {
#endif

// Standard GNU options
#define no_argument		0
#define required_argument	1
#define optional_argument	2

extern char *optarg;
extern int optind;
extern int opterr;
extern int optopt;

struct option
{
  const char *name;
  int has_arg;
  int *flag;
  int val;
};

extern int getopt (int ___argc, char *const *___argv, const char *__shortopts);
extern int getopt_long (int ___argc, char *const *___argv,
			const char *__shortopts,
		        const struct option *__longopts, int *__longind);
extern int getopt_long_only (int ___argc, char *const *___argv,
			     const char *__shortopts,
		             const struct option *__longopts, int *__longind);

// Unicode support
struct option_w
{
    const wchar_t* name;
    int has_arg;
    int* flag;
    int val;
};

extern wchar_t* optarg_w;
extern int getopt_w(int argc, wchar_t* const * argv, const wchar_t* optstring);
extern int getopt_long_w(int argc, wchar_t* const * argv, const wchar_t* options,
                                     const struct option_w* long_options, int* opt_index);
extern int getopt_long_only_w(int argc, wchar_t* const * argv, const wchar_t* options,
                                          const struct option_w* long_options, int* opt_index);


#ifdef  __cplusplus
}
#endif

#endif /* HAVE_GETOPT_H */

#endif /* FAAC_GETOPT_H */
