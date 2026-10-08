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

#include "git_version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <locale.h>
#include <errno.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#include <sys/types.h>
#elif defined(HAVE_ICONV)
#include <langinfo.h>
#include <iconv.h>
#include <strings.h>
#endif

#include "charset.h"
#include "cli_io.h"

#ifdef _WIN32
#ifndef strcasecmp
#define strcasecmp _stricmp
#endif

static UINT s_orig_output_cp = 0;
static bool s_cp_saved = false;

static void win32_restore_console_cp(void)
{
    if (s_cp_saved && s_orig_output_cp != 0)
        SetConsoleOutputCP(s_orig_output_cp);
}

static bool win32_has_console_output(void)
{
    HANDLE handles[] = { GetStdHandle(STD_OUTPUT_HANDLE), GetStdHandle(STD_ERROR_HANDLE) };
    for (size_t i = 0; i < sizeof(handles) / sizeof(handles[0]); i++)
    {
        DWORD mode;
        if (handles[i] != NULL && handles[i] != INVALID_HANDLE_VALUE &&
            GetConsoleMode(handles[i], &mode))
            return true;
    }
    return false;
}
#endif

void cli_init_console(void)
{
#ifdef _WIN32
    if (!s_cp_saved && win32_has_console_output())
    {
        s_orig_output_cp = GetConsoleOutputCP();
        if (s_orig_output_cp != 0 && SetConsoleOutputCP(CP_UTF8))
        {
            s_cp_saved = true;
            atexit(win32_restore_console_cp);
        }
    }
    setlocale(LC_CTYPE, ".UTF-8");
#else
    setlocale(LC_CTYPE, "");
#endif
}

static const char *id3_genres[] = {
    "Blues", "Classic Rock", "Country", "Dance",
    "Disco", "Funk", "Grunge", "Hip-Hop",
    "Jazz", "Metal", "New Age", "Oldies",
    "Other", "Pop", "R&B", "Rap",
    "Reggae", "Rock", "Techno", "Industrial",
    "Alternative", "Ska", "Death Metal", "Pranks",
    "Soundtrack", "Euro-Techno", "Ambient", "Trip-Hop",
    "Vocal", "Jazz+Funk", "Fusion", "Trance",
    "Classical", "Instrumental", "Acid", "House",
    "Game", "Sound Clip", "Gospel", "Noise",
    "Alternative Rock", "Bass", "Soul", "Punk",
    "Space", "Meditative", "Instrumental Pop", "Instrumental Rock",
    "Ethnic", "Gothic", "Darkwave", "Techno-Industrial",
    "Electronic", "Pop-Folk", "Eurodance", "Dream",
    "Southern Rock", "Comedy", "Cult", "Gangsta",
    "Top 40", "Christian Rap", "Pop/Funk", "Jungle",
    "Native US", "Cabaret", "New Wave", "Psychadelic",
    "Rave", "Showtunes", "Trailer", "Lo-Fi",
    "Tribal", "Acid Punk", "Acid Jazz", "Polka",
    "Retro", "Musical", "Rock & Roll", "Hard Rock",
    "Folk", "Folk-Rock", "National Folk", "Swing",
    "Fast Fusion", "Bebob", "Latin", "Revival",
    "Celtic", "Bluegrass", "Avantgarde", "Gothic Rock",
    "Progressive Rock", "Psychedelic Rock", "Symphonic Rock", "Slow Rock",
    "Big Band", "Chorus", "Easy Listening", "Acoustic",
    "Humour", "Speech", "Chanson", "Opera",
    "Chamber Music", "Sonata", "Symphony", "Booty Bass",
    "Primus", "Porn Groove", "Satire", "Slow Jam",
    "Club", "Tango", "Samba", "Folklore",
    "Ballad", "Power Ballad", "Rhythmic Soul", "Freestyle",
    "Duet", "Punk Rock", "Drum Solo", "Acapella",
    "Euro-House", "Dance Hall", "Goa", "Drum & Bass",
    "Club - House", "Hardcore", "Terror", "Indie",
    "BritPop", "Negerpunk", "Polsk Punk", "Beat",
    "Christian Gangsta Rap", "Heavy Metal", "Black Metal", "Crossover",
    "Contemporary Christian", "Christian Rock", "Merengue", "Salsa",
    "Thrash Metal", "Anime", "JPop", "Synthpop",
    "Unknown"
};

/* Smart-quote substitution (Notes, Pages, word processors) turns straight
   quotes into curly ones before a pasted --tag/--genre argument ever
   reaches us, so both forms need stripping. */
char *trim_quotes_and_spaces(char *s)
{
    if (!s) return s;

    while (*s)
    {
        if (*s == ' ' || *s == '\t' || *s == '"' || *s == '\'')
        {
            s++;
        }
        else if ((unsigned char)s[0] == 0xE2 && (unsigned char)s[1] == 0x80 &&
                 ((unsigned char)s[2] == 0x9C || (unsigned char)s[2] == 0x9D || (unsigned char)s[2] == 0x98 || (unsigned char)s[2] == 0x99))
        {
            s += 3;
        }
        else
        {
            break;
        }
    }

    size_t len = strlen(s);
    while (len > 0)
    {
        if (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '"' || s[len - 1] == '\'')
        {
            s[--len] = '\0';
        }
        else if (len >= 3 && (unsigned char)s[len - 3] == 0xE2 && (unsigned char)s[len - 2] == 0x80 &&
                 ((unsigned char)s[len - 1] == 0x9C || (unsigned char)s[len - 1] == 0x9D || (unsigned char)s[len - 1] == 0x98 || (unsigned char)s[len - 1] == 0x99))
        {
            len -= 3;
            s[len] = '\0';
        }
        else
        {
            break;
        }
    }

    return s;
}

const char *parse_tag_arg(char *arg, char **name, char **value)
{
    char *eq = strchr(arg, '=');
    char *comma = strchr(arg, ',');
    char *sep;

    if (eq && comma)
        sep = (eq < comma) ? eq : comma;
    else
        sep = eq ? eq : comma;
    if (!sep)
        return "Missing tag value.\n";

    *sep++ = '\0';
    *name = trim_quotes_and_spaces(arg);
    *value = trim_quotes_and_spaces(sep);
    if (**name == '\0')
        return "Tag name cannot be empty.\n";
    if (**value == '\0')
        return "Tag value cannot be empty.\n";
    return NULL;
}

bool parse_index_arg(const char *arg, uint16_t *n, uint16_t *total)
{
    return sscanf(arg, "%hu/%hu", n, total) >= 1;
}

#define COVER_ART_MAX_BYTES ((size_t)32 * 1024 * 1024)

static bool is_cover_image(const uint8_t *buf, size_t len)
{
    if (len < 12)
        return false;
    return !memcmp(buf, "\x89\x50\x4E\x47\x0D\x0A\x1A\x0A", 8) ||            /* PNG */
           !memcmp(buf, "\xFF\xD8\xFF\xE0", 4) || !memcmp(buf, "\xFF\xD8\xFF\xE1", 4) || /* JPEG */
           !memcmp(buf, "GIF87a", 6) || !memcmp(buf, "GIF89a", 6);                  /* GIF */
}

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
        else if (!is_cover_image(buf, (size_t)sz))
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

bool parse_genre(const char *arg, uint16_t *genre_id, const char **genre_name)
{
    if (!arg || !*arg)
        return false;

    bool is_num = true;
    for (const char *p = arg; *p; p++)
    {
        if (*p < '0' || *p > '9')
        {
            is_num = false;
            break;
        }
    }

    int max_genres = (int)(sizeof(id3_genres) / sizeof(id3_genres[0]));

    if (is_num)
    {
        int g = atoi(arg);
        if (g < 0 || g > 255)
            return false;
        if (genre_id) *genre_id = (uint16_t)(g + 1);
        if (genre_name)
        {
            if (g >= 0 && g < max_genres)
                *genre_name = id3_genres[g];
            else
                *genre_name = NULL;
        }
        return true;
    }

    for (int i = 0; i < max_genres; i++)
    {
        if (strcasecmp(arg, id3_genres[i]) == 0)
        {
            if (genre_id) *genre_id = (uint16_t)(i + 1);
            if (genre_name) *genre_name = id3_genres[i];
            return true;
        }
    }

    /* genre_id 0 suppresses the numeric 'gnre' atom; the text atom still
       carries whatever the user typed. */
    if (genre_id) *genre_id = 0;
    if (genre_name) *genre_name = arg;
    return true;
}

/* Structural check only (continuation-byte pattern, NUL-termination): does
   not reject overlong encodings, surrogates, or code points past U+10FFFF.
   A plausibility check, not a strict validator -- good enough to decide
   "does this need repair_to_utf8()", not for security-sensitive callers. */
static bool utf8_is_valid(const char *str)
{
    if (!str)
        return true;

    const unsigned char *bytes = (const unsigned char *)str;
    while (*bytes)
    {
        if (bytes[0] <= 0x7F)
        {
            bytes += 1;
        }
        else if ((bytes[0] & 0xE0) == 0xC0)
        {
            if (bytes[1] == '\0' || (bytes[1] & 0xC0) != 0x80) return false;
            bytes += 2;
        }
        else if ((bytes[0] & 0xF0) == 0xE0)
        {
            if (bytes[1] == '\0' || (bytes[1] & 0xC0) != 0x80 ||
                bytes[2] == '\0' || (bytes[2] & 0xC0) != 0x80) return false;
            bytes += 3;
        }
        else if ((bytes[0] & 0xF8) == 0xF0)
        {
            if (bytes[1] == '\0' || (bytes[1] & 0xC0) != 0x80 ||
                bytes[2] == '\0' || (bytes[2] & 0xC0) != 0x80 ||
                bytes[3] == '\0' || (bytes[3] & 0xC0) != 0x80) return false;
            bytes += 4;
        }
        else
        {
            return false;
        }
    }
    return true;
}

#ifdef _WIN32
char *win32_utf16_to_utf8(const wchar_t *wstr)
{
    if (!wstr)
        return NULL;

    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (len <= 0)
        return NULL;

    char *str = malloc((size_t)len);
    if (str)
    {
        WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str, len, NULL, NULL);
    }
    return str;
}

/* Attempts to repair str, assumed to be in the current Windows ANSI code
   page, into UTF-8. Returns NULL if it can't. */
static char *repair_to_utf8(const char *str)
{
    int wn = MultiByteToWideChar(CP_ACP, 0, str, -1, NULL, 0);
    if (wn <= 0)
        return NULL;

    wchar_t *ws = malloc((size_t)wn * sizeof(wchar_t));
    if (!ws)
        return NULL;

    MultiByteToWideChar(CP_ACP, 0, str, -1, ws, wn);
    char *utf8 = win32_utf16_to_utf8(ws);
    free(ws);
    return utf8;
}

wchar_t *win32_utf8_to_utf16(const char *utf8_str)
{
    if (!utf8_str)
        return NULL;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8_str, -1, NULL, 0);
    if (wlen <= 0)
        return NULL;

    wchar_t *wstr = malloc((size_t)wlen * sizeof(wchar_t));
    if (wstr)
        MultiByteToWideChar(CP_UTF8, 0, utf8_str, -1, wstr, wlen);

    return wstr;
}

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
#else /* POSIX */
#ifdef HAVE_ICONV
/* Mirrors the Windows path (assume the current code page, convert to UTF-8)
   using POSIX's equivalent: the locale's codeset name plus iconv(). Returns
   NULL if it can't repair str. Requires setlocale(LC_CTYPE, "") to have
   been called at startup -- otherwise nl_langinfo() reports the "C"
   locale's codeset (ASCII) regardless of the environment. */
static char *repair_to_utf8(const char *str)
{
    const char *codeset = nl_langinfo(CODESET);
    if (!codeset || !strcasecmp(codeset, "UTF-8") || !strcasecmp(codeset, "UTF8"))
        return NULL; /* locale already claims UTF-8 (or is unknown): this is
                        corrupt input, not a different encoding */

    iconv_t cd = iconv_open("UTF-8", codeset);
    if (cd == (iconv_t)-1)
        return NULL;

    size_t inbytes = strlen(str);
    size_t outcap = inbytes * 4 + 16; /* worst-case UTF-8 expansion + headroom */
    char *outbuf = malloc(outcap);
    if (!outbuf)
    {
        iconv_close(cd);
        return NULL;
    }

    char *inp = (char *)str;
    char *outp = outbuf;
    size_t inleft = inbytes;
    size_t outleft = outcap - 1; /* leave room for the NUL below */

    size_t rc = iconv(cd, &inp, &inleft, &outp, &outleft);
    /* Flush to the initial shift state: a no-op for simple codesets like
       ISO-8859-*, but stateful ones (e.g. ISO-2022-JP) need a trailing
       escape sequence emitted here, or the converted tag is truncated. */
    if (rc != (size_t)-1 && inleft == 0)
        iconv(cd, NULL, NULL, &outp, &outleft);
    iconv_close(cd);

    if (rc == (size_t)-1 || inleft != 0)
    {
        free(outbuf);
        return NULL;
    }

    *outp = '\0';
    return outbuf;
}
#else
/* No iconv available on this platform: can't attempt repair. */
static char *repair_to_utf8(const char *str)
{
    (void)str;
    return NULL;
}
#endif
#endif

char *utf8_ensure(const char *str)
{
    if (!str)
        return NULL;

    if (utf8_is_valid(str))
        return strdup(str);

    char *fixed = repair_to_utf8(str);
    if (fixed)
        return fixed;

    fprintf(stderr, "warning: tag value is not valid UTF-8, writing as-is\n");
    return strdup(str);
}

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

void cli_print_patent_notice(FILE *stream) {
    fputs("\nPlease note that the use of this software may require the payment of patent\n"
          "royalties. You need to consider this issue before you start building derivative\n"
          "works. We are not warranting or indemnifying you in any way for patent\n"
          "royalities! YOU ARE SOLELY RESPONSIBLE FOR YOUR OWN ACTIONS!\n\n", stream);
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
