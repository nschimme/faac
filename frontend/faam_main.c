/*
 * FAAM - Freeware Advanced Audio Muxer
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

/*
 * FAAM - Freeware Advanced Audio Muxer / Demuxer / Manipulator CLI
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

#ifdef HAVE_GETOPT_H
# include <getopt.h>
#else
# include "getopt.h"
# include "getopt.c"
#endif

#include "faam.h"
#include "adts.h"
#include "annexb.h"
#include "faam_engine.h"
#include "progress.h"
#include "cli_io.h"
#include "cli_parse.h"
#include "charset.h"
#include "help.h"
#include "endian.h"
#include "asc_codec.h"
#include "cover_art.h"

static int g_verbose = 1;

static bool set_verbosity(const char *text)
{
    uint8_t value;
    if (!cli_parse_verbosity(text, &value)) {
        fprintf(stderr, "Invalid verbosity '%s' (expected 0, 1, or 2)\n",
                text ? text : "");
        return false;
    }
    g_verbose = value;
    return true;
}

enum {
    HELP_COMMANDS = 1,
    HELP_MUX,
    HELP_INSPECT,
    HELP_TAG,
    HELP_CHAPTER
};

static const help_t help_commands[] = {
    {"-i <file> ... -o <out>",
     "Mux elementary streams into an MP4 (no subcommand word); '-' selects stdin/stdout",
     "Inputs may be ADTS AAC or, in a video-enabled build, Annex-B H.264/H.265. Repeat -i to create multiple tracks, up to eight."},
    {"info <file>",
     "Print container, track, tag, chapter and gapless summaries; '-' reads stdin",
     "Use this to inspect track IDs before selecting a track with demux. JSON output is available with --json."},
    {"dump <file>",
     "Print the MP4 atom tree; '-' reads stdin",
     "The dump lists atom offsets, sizes and nesting, which is useful when diagnosing container layout."},
    {"demux <file>",
     "Extract one track's elementary stream; '-' reads stdin and -o - writes stdout",
     "The first track is selected by default. AAC can be written as raw access units or ADTS; video is written as Annex-B."},
    {"tag <file>",
     "Edit iTunes metadata tags in place",
     "Existing modeled and unmodeled metadata is preserved unless --clear or --remove is used. Work on a copy when the edit must be crash-safe."},
    {"chapter import|export <file>",
     "Edit or list chapters of an audiobook",
     "Import replaces the file's chapters from a tab-separated chapter list; export writes the current chapters to a text file."},
    {NULL, NULL, NULL}
};

static const help_t help_mux[] = {
    {"-i, --input <file>",
     "Elementary stream to mux: ADTS AAC, or Annex-B H.264 or H.265 (video needs a build with -Dmuxer-video). "
     "Video is muxed one picture per sample in stream order, and B-frames are not supported: no composition "
     "time offsets are written, so reordered pictures would play out of order. Use - for stdin. "
     "Repeat for more tracks (max 8)",
     "AAC input must be ADTS. Video input must be Annex-B and include codec parameter sets; FAAM does not parse dimensions from the stream. Each access unit becomes one MP4 sample."},
    {"-o, --output <file>",
     "Output MP4 file (required); - streams fragmented MP4 when supported; --faststart, --brand m4b or gapless options use progressive MP4",
     "Progressive files are written by default. With stdout, FAAM uses one-second fragmented MP4 when supported; faststart, M4B branding or gapless metadata require progressive output, which is emitted after muxing finishes."},
    {"-v <verbose>", "Set verbosity level: -v0 is quiet; -v1 (default) shows status; -v2 adds detailed summaries where available.",
     "Status and progress are written to stderr when media is sent to stdout. Errors are always reported regardless of verbosity."},
    {"--overwrite",
     "Overwrite an existing output file",
     "FAAM refuses to replace an existing destination without this option. An output that aliases an input is rejected even when overwrite is enabled."},
    {"--codec:N <codec>",
     "Codec of the Nth -i, counting from 0: aac, h264 or h265 (default: from the file extension, "
     ".264/.h264 or .265/.hevc, else aac)",
     "Use this when an input extension is ambiguous or does not identify its codec. N is the zero-based position of the matching -i option."},
    {"--brand <name>",
     "m4a (default) or m4b, the audiobook brand",
     "M4B branding is intended for audiobooks and forces progressive output when writing to stdout. Files containing video use the isom brand."},
    {"--faststart",
     "Move the index to the front so the file plays while it downloads; rewrites the whole file "
     "when muxing ends, so skip it on flash recorders",
     "Faststart requires seekable progressive output because the movie index is moved ahead of media data after the inputs are read."},
    {"--width <pixels>",
     "Video width; required for video, the stream is not parsed for it",
     "Set the coded display width from 1 through 65535. FAAM stores it in the video track metadata; it does not inspect the bitstream to infer it."},
    {"--height <pixels>",
     "Video height; required for video",
     "Set the coded display height from 1 through 65535. FAAM stores it in the video track metadata; it does not inspect the bitstream to infer it."},
    {"--rotation <degrees>",
     "Display rotation of video tracks, clockwise: 0 (default), 90, 180 or 270",
     "Writes a display matrix for compatible players. The encoded pictures are not rotated or rewritten."},
    {"--fps <n[/d]>",
     "Video frame rate, a whole number or a fraction such as 30000/1001 (default: 30)",
     "The value assigns sample durations in the video track. Fractional rates preserve the exact numerator/denominator timing."},
    {"--encoder-delay <samples>",
     "Leading priming samples, for gapless playback of audio (needs an audio track); counted in the "
     "track's sample rate, which for HE-AAC is the core rate, as faam info prints it for a faac file",
     "Writes gapless metadata and an edit list. The value is in samples at the AAC track's core sample rate, not necessarily the decoded output rate."},
    {"--padding-delay <samples>",
     "Trailing padding samples in the same units as --encoder-delay; the total length is derived from the "
     "frames muxed",
     "The programme duration is computed from total AAC frame duration minus encoder delay and trailing padding."},
    {"--sbr-signaling <mode>",
     "How the AAC track announces SBR/PS, which ADTS cannot say: none (default, decoder detects), "
     "compatible, explicit, ps or ps-explicit (HE-AAC v2, which needs a mono core)",
     "Use compatible for backward-compatible signaling or explicit for hierarchical signaling. The ps modes signal parametric stereo; the core AAC stream must be mono."},
    {"--language <code3>",
     "Set ISO 639-2/T 3-letter language code of audio tracks (e.g. eng, ger); default und. "
     "--lang is the same",
     "The language is stored on each audio track. Exactly three ASCII letters are accepted, in either case."},
    {"--creation-time <value>",
     "Set creation/modification time (auto, now, or timestamp); without it SOURCE_DATE_EPOCH is used "
     "when set, else the time is left unset",
     "auto uses the first input's modification time; now uses the current time; a number is Unix time in seconds and must fit the MP4 32-bit timestamp field."},
    {NULL,
     "Every option of --help-tag except --remove and --clear also applies when muxing.", NULL},
    {NULL, NULL, NULL}
};

static const help_t help_inspect[] = {
    {"-o, --output <file>",
     "demux: output file (default: output.raw); - writes to stdout. A name containing .aac or .adts gets ADTS headers; "
     "video is written as Annex-B",
     "For AAC, names containing .aac or .adts select ADTS output; other names select raw access units. Video samples are prefixed with Annex-B start codes."},
    {"-v <verbose>",
     "Set verbosity level: -v0 is quiet; -v1 (default) shows status; -v2 adds detailed summaries where available.",
     "Progress and completion messages go to stderr if extracted media is sent to stdout."},
    {"--format <raw|adts>",
     "demux: choose AAC output format explicitly; default is inferred from the output filename (raw for stdout)",
     "This option applies only to AAC tracks. ADTS output requires codec configuration that can be represented in an ADTS header."},
    {"-t, --track <id>",
     "demux: track ID to extract (default: the first track)",
     "Use the track IDs reported by faam info. If the file has no track with the requested ID, extraction fails."},
    {"--export-asc <file>",
     "demux: also write the track's codec configuration (AudioSpecificConfig, avcC or hvcC)",
     "Writes the selected track's decoder configuration bytes alongside the extracted stream."},
    {"--overwrite",
     "demux: overwrite an existing output or --export-asc file",
     "Both output paths are checked before either is written. An output that aliases the input or the other output is rejected."},
    {NULL, NULL, NULL}
};

static const help_t help_tag[] = {
    {"-v <verbose>",
     "Set verbosity level: -v0 is quiet; -v1 (default) shows status; -v2 adds detailed summaries where available.",
     "The same verbosity setting applies to muxing and tag editing."},
    {"--title <text>", "Title", "Set the standard iTunes title field. Text is converted to UTF-8."},
    {"--titlesort <text>", "Title sort name", "Set the sort order text used by media libraries independently of the displayed title."},
    {"--artist <text>", "Artist", "Set the standard iTunes artist field. Text is converted to UTF-8."},
    {"--album <text>", "Album", "Set the standard iTunes album field. Text is converted to UTF-8."},
    {"--albumartist <text>", "Album artist", "Set the album artist field, which can differ from the track artist."},
    {"--composer <text>", "Composer", "Set the standard iTunes composer field."},
    {"--artistsort <text>", "Artist sort name", "Set the artist sort key independently of the displayed artist name."},
    {"--albumsort <text>", "Album sort name", "Set the album sort key independently of the displayed album name."},
    {"--albumartistsort <text>", "Album artist sort name", "Set the album-artist sort key independently of the displayed album artist."},
    {"--composersort <text>", "Composer sort name", "Set the composer sort key independently of the displayed composer name."},
    {"--year <text>", "Year", "Set the release year as text."},
    {"--comment <text>", "Comment", "Set the standard iTunes comment field."},
    {"--lyrics <text>", "Set plain, unsynchronized lyrics", "Text is converted to UTF-8 and line endings are normalized to LF. No timing information is added; use --lyrics-file to supply multiline text."},
    {"--grouping <text>", "Set the grouping tag", "Text is converted to UTF-8."},
    {"--copyright <text>", "Set the copyright tag", "Text is converted to UTF-8."},
    {"--description <text>", "Set the description tag", "Text is converted to UTF-8."},
    {"--long-description <text>", "Set the long description tag", "Text is converted to UTF-8."},
    {"--work <text>", "Set the classical work name", "Text is converted to UTF-8."},
    {"--movement <text>", "Set the classical movement name", "Text is converted to UTF-8."},
    {"--bpm <0-65535>", "Set beats per minute", "Accepts an integer from 0 through 65535."},
    {"--movement-number <0-65535>", "Set the movement number", "Accepts an integer from 0 through 65535."},
    {"--movement-total <0-65535>", "Set the total number of movements", "Accepts an integer from 0 through 65535."},
    {"--show-work-movement", "Enable work and movement display", "Sets the flag used by players to display classical work and movement names."},
    {"--lyrics-file <file>", "Read lyrics from a UTF-8 file", "Accepts an optional UTF-8 BOM, normalizes CR, CRLF, and LF line endings to LF, preserves surrounding whitespace, and rejects invalid UTF-8 or NUL bytes. No timing synchronization is implied."},
    {"--genre <value>",
     "ID3v1 genre number (0-191) or name; anything else is stored as free text",
     "The genre is stored as its text name; setting it removes a numeric genre atom the file already has."},
    {"--compilation", "Mark as part of a compilation", "Sets the compilation flag on the MP4."},
    {"--track <n[/total]>", "Track number", "Optionally include the total track count after a slash, for example 3/12."},
    {"--disc <n[/total]>", "Disc number", "Optionally include the total disc count after a slash, for example 1/2."},
    {"--cover-art <file>", "Cover image, GIF, JPEG, PNG or BMP",
     "FAAM detects GIF, JPEG, PNG or BMP from the file contents. Cover art is limited to 32 MiB."},
    {"--tag <name=value>",
     "Custom tag, name and value separated by = or ,; repeat for more",
     "The first '=' or ',' separates the name from the value. Both must be non-empty. All CLI UTF-8 metadata text normalizes CR and CRLF line endings to LF."},
    {"--remove <field>",
     "tag only: remove a field before the other options apply: title, titlesort, artist, artistsort, "
     "album, albumsort, albumartist, albumartistsort, composer, composersort, year, comment, genre, "
     "compilation, track, disc, cover-art, custom, lyrics, grouping, copyright, description, "
     "long-description, work, movement, bpm, movement-number, movement-total, show-work-movement, encoder or genre-code. Repeatable",
     "Removal runs before setting other supplied fields. The custom field removes all custom tags; at most 32 --remove options are accepted."},
    {"--clear",
     "tag only: remove every existing tag before the other options apply (drops all metadata, including preserved/unmodeled tags)",
     "Unlike updating one modeled field, --clear also removes metadata FAAM does not understand. It runs before adding any new values in the same command."},
    {NULL, NULL, NULL}
};

static const help_t help_chapter[] = {
    {"import <file>",
     "Replace the chapters of an MP4 with those of the --chapters file; --chapters - reads stdin",
     "The chapter file contains one timestamp and title per line, separated by a tab. Import replaces existing chapters while preserving tags."},
    {"export <file>",
     "Write chapters from an MP4 (input - reads stdin); -o - writes stdout",
     "A file without chapters is an error. The output uses the same timestamp and title format accepted by import."},
    {"--chapters <file>",
     "Chapter list to import: one \"HH:MM:SS.mmm<TAB>Title\" line per chapter (max 255); the fraction "
     "is 1 to 3 digits, .5 being 500 ms",
     "Timestamps are interpreted as chapter start times. The maximum chapter count is 255."},
    {"-o, --output <file>",
     "Chapter list to write, in the same format; - writes stdout",
     "This option is used by chapter export. When the path is -, chapter text is written to stdout."},
    {"-v <verbose>",
     "Set verbosity level: -v0 is quiet; -v1 (default) shows status; -v2 adds detailed summaries where available.",
     "At verbosity zero, status confirmations are suppressed; errors are still printed."},
    {"--overwrite",
     "export: overwrite an existing chapter list",
     "Without this flag, chapter export refuses to replace an existing text file."},
    {NULL, NULL, NULL}
};

static const help_group_t g_help[] = {
    {HELP_COMMANDS, "Subcommands", "--help-commands", help_commands, 1},
    {HELP_MUX, "Mux options", "--help-mux", help_mux, 1},
    {HELP_INSPECT, "Inspect and extract options (info, dump, demux)", "--help-inspect", help_inspect, 1},
    {HELP_TAG, "Tag options (mux and tag)", "--help-tag", help_tag, 1},
    {HELP_CHAPTER, "Chapter options", "--help-chapter", help_chapter, 1},
    {0, NULL, NULL, NULL, 0}
};

typedef struct {
    FILE *spool;
    FILE *output;
    uint64_t emitted;
    uint64_t position;
    uint64_t pending;
} mux_stream_output;

static int32_t mux_stream_write(void *user, const void *data, uint32_t bytes)
{
    mux_stream_output *s = (mux_stream_output *)user;
    uint32_t total = bytes;
    if (s->position < s->emitted) {
        uint64_t ignored = s->emitted - s->position;
        if (ignored >= bytes) { s->position += bytes; return (int32_t)total; }
        data = (const uint8_t *)data + ignored;
        bytes -= (uint32_t)ignored;
        s->position = s->emitted;
    }
    uint64_t at = s->position - s->emitted;
    if (!cli_fseek(s->spool, at) || fwrite(data, 1, bytes, s->spool) != bytes)
        return -1;
    s->position += bytes;
    uint64_t pending_end = s->position - s->emitted;
    if (pending_end > s->pending) s->pending = pending_end;
    return (int32_t)total;
}

static bool mux_stream_seek(void *user, uint64_t position)
{
    ((mux_stream_output *)user)->position = position;
    return true;
}

static uint64_t mux_stream_tell(void *user)
{
    return ((mux_stream_output *)user)->position;
}

static bool mux_stream_flush(void *user)
{
    mux_stream_output *s = (mux_stream_output *)user;
    if (fflush(s->spool) != 0) return false;
    if (s->pending) {
        if (!cli_fseek(s->spool, 0)) return false;
        uint8_t buf[65536];
        uint64_t left = s->pending;
        while (left) {
            size_t n = left < sizeof(buf) ? (size_t)left : sizeof(buf);
            if (fread(buf, 1, n, s->spool) != n || fwrite(buf, 1, n, s->output) != n)
                return false;
            left -= n;
        }
        s->emitted += s->pending;
        s->pending = 0;
        FILE *old_spool = s->spool;
        s->spool = NULL;
        if (fclose(old_spool) != 0) return false;
        s->spool = tmpfile();
        if (!s->spool) return false;
    }
    return fflush(s->output) == 0;
}

static faam_io mux_stream_io(mux_stream_output *s)
{
    faam_io io = { .struct_size = sizeof(io) };
    io.struct_size = sizeof(io);
    io.user_data = s;
    io.write = mux_stream_write;
    io.seek = mux_stream_seek;
    io.tell = mux_stream_tell;
    io.flush = mux_stream_flush;
    return io;
}

/* Set once in main from the linked library. */
static const char *g_lib_version;

static void print_usage(int mode)
{
    char version_buf[128];
    show_help_usage("faam", mode,
                    cli_version_string(version_buf, sizeof(version_buf), g_lib_version),
                    "faam -i <input> [-i <input>...] -o <out.mp4> [options]\n"
                    "       faam <info|dump|demux|tag|chapter> [options] <file>",
                    g_help);
}

static void print_version_banner(void)
{
    if (g_verbose == 0) return;

    char version_buf[128];
    fprintf(stderr, "Freeware Advanced Audio Muxer\nFAAM %s\n\n",
            cli_version_string(version_buf, sizeof(version_buf), g_lib_version));
}

enum {
    OPT_BRAND = 400,
    OPT_FASTSTART,
    OPT_ENCODER_DELAY,
    OPT_PADDING_DELAY,
    OPT_SBR_SIGNALING,
    OPT_EXPORT_ASC,
    OPT_WIDTH,
    OPT_HEIGHT,
    OPT_ROTATION,
    OPT_FPS,
    OPT_LANGUAGE,
    OPT_CREATION_TIME,
    OPT_OVERWRITE,
    OPT_FORMAT,
    OPT_REMOVE,
    OPT_CLEAR,
    OPT_CHAPTERS,
    /* The tag options are contiguous: mux and tag hand the whole range to tag_apply(). */
    OPT_TAG_FIRST,
    OPT_TRACK = OPT_TAG_FIRST,
    OPT_TITLE,
    OPT_TITLE_SORT,
    OPT_ARTIST,
    OPT_ALBUM,
    OPT_ARTIST_SORT,
    OPT_ALBUM_SORT,
    OPT_ALBUM_ARTIST,
    OPT_ALBUM_ARTIST_SORT,
    OPT_COMPOSER,
    OPT_COMPOSER_SORT,
    OPT_YEAR,
    OPT_COMMENT,
    OPT_GENRE,
    OPT_COMPILATION,
    OPT_DISC,
    OPT_COVER_ART,
    OPT_TAG,
    OPT_LYRICS,
    OPT_GROUPING,
    OPT_COPYRIGHT,
    OPT_DESCRIPTION,
    OPT_LONG_DESCRIPTION,
    OPT_WORK,
    OPT_MOVEMENT,
    OPT_BPM,
    OPT_MOVEMENT_NUMBER,
    OPT_MOVEMENT_TOTAL,
    OPT_SHOW_WORK_MOVEMENT,
    OPT_LYRICS_FILE,
    OPT_TAG_LAST = OPT_LYRICS_FILE
};

#define FAAM_CLI_TITLE_MAX 256 /* title buffer incl. NUL; longer titles are truncated */
#define FAAM_CLI_MAX_CHAPTERS 255 /* what the library stores in a chpl atom */
#define FAAM_TAG_MAX_REMOVE 32
#define FAAM_MUX_MAX_TRACKS 8 /* matches faam_muxer_config.tracks[8] in include/faam.h */

static const char *bad_value(char *buf, size_t size, const char *opt, const char *val, const char *want)
{
    snprintf(buf, size, "Error: invalid %s value \"%s\" (%s)\n", opt, val, want);
    return buf;
}

static bool output_refused(const char *path, bool overwrite)
{
    if (overwrite || !cli_file_exists(path)) return false;
    fprintf(stderr, "Output file %s already exists (use --overwrite)\n", path);
    return true;
}

/* Owned UTF-8 copies of CLI strings; metadata entries only borrow them, so they
 * must outlive the muxer or faam_update_metadata_stream. argv text arrives in
 * whatever the shell/locale handed us and iTunes atoms require UTF-8, hence
 * utf8_ensure(), as faac does for the same values. */
typedef struct { char **v; size_t n; } owned_list;

static const char *own_utf8(owned_list *l, const char *src)
{
    char *u = utf8_ensure(src);
    char *c = u ? u : strdup(src);
    char **nv = c ? realloc(l->v, (l->n + 1) * sizeof(*nv)) : NULL;
    if (!nv) { free(c); return NULL; }
    l->v = nv;
    l->v[l->n++] = c;
    return c;
}

static void owned_free(owned_list *l)
{
    for (size_t i = 0; i < l->n; i++) free(l->v[i]);
    free(l->v);
    l->v = NULL; l->n = 0;
}

/* Metadata being assembled from the tag options, shared by mux and tag. */
typedef struct {
    uint32_t num_metadata;
    owned_list owned;
    faam_metadata_entry *entries;
    uint32_t entries_cap;
    uint16_t track_total, disc_total;
    bool given; /* some tag option was applied */
} tag_ctx;

static void tag_free(tag_ctx *t)
{
    free(t->entries);
    owned_free(&t->owned);
}

#define TAG_LONG_OPTIONS \
    {"title", required_argument, 0, OPT_TITLE}, \
    {"titlesort", required_argument, 0, OPT_TITLE_SORT}, \
    {"artist", required_argument, 0, OPT_ARTIST}, \
    {"artistsort", required_argument, 0, OPT_ARTIST_SORT}, \
    {"album", required_argument, 0, OPT_ALBUM}, \
    {"albumsort", required_argument, 0, OPT_ALBUM_SORT}, \
    {"albumartist", required_argument, 0, OPT_ALBUM_ARTIST}, \
    {"albumartistsort", required_argument, 0, OPT_ALBUM_ARTIST_SORT}, \
    {"composer", required_argument, 0, OPT_COMPOSER}, \
    {"composersort", required_argument, 0, OPT_COMPOSER_SORT}, \
    {"year", required_argument, 0, OPT_YEAR}, \
    {"comment", required_argument, 0, OPT_COMMENT}, \
    {"genre", required_argument, 0, OPT_GENRE}, \
    {"compilation", no_argument, 0, OPT_COMPILATION}, \
    {"track", required_argument, 0, OPT_TRACK}, \
    {"disc", required_argument, 0, OPT_DISC}, \
    {"cover-art", required_argument, 0, OPT_COVER_ART}, \
    {"tag", required_argument, 0, OPT_TAG}, \
    {"lyrics", required_argument, 0, OPT_LYRICS}, \
    {"grouping", required_argument, 0, OPT_GROUPING}, \
    {"copyright", required_argument, 0, OPT_COPYRIGHT}, \
    {"description", required_argument, 0, OPT_DESCRIPTION}, \
    {"long-description", required_argument, 0, OPT_LONG_DESCRIPTION}, \
    {"work", required_argument, 0, OPT_WORK}, \
    {"movement", required_argument, 0, OPT_MOVEMENT}, \
    {"bpm", required_argument, 0, OPT_BPM}, \
    {"movement-number", required_argument, 0, OPT_MOVEMENT_NUMBER}, \
    {"movement-total", required_argument, 0, OPT_MOVEMENT_TOTAL}, \
    {"show-work-movement", no_argument, 0, OPT_SHOW_WORK_MOVEMENT}, \
    {"lyrics-file", required_argument, 0, OPT_LYRICS_FILE}

/* CLI aliases describe atom keys; the library handles every key generically. */
typedef struct { int opt; const char *name, *label; uint32_t key; unsigned width; } tag_alias;
static const tag_alias tag_aliases[] = {
    { OPT_TITLE, "title", "Title", FAAM_METADATA_KEY_TITLE, 0 },
    { OPT_TITLE_SORT, "titlesort", "Title Sort", FAAM_METADATA_KEY_TITLE_SORT, 0 },
    { OPT_ARTIST, "artist", "Artist", FAAM_METADATA_KEY_ARTIST, 0 },
    { OPT_ARTIST_SORT, "artistsort", "Artist Sort", FAAM_METADATA_KEY_ARTIST_SORT, 0 },
    { OPT_ALBUM, "album", "Album", FAAM_METADATA_KEY_ALBUM, 0 },
    { OPT_ALBUM_SORT, "albumsort", "Album Sort", FAAM_METADATA_KEY_ALBUM_SORT, 0 },
    { OPT_ALBUM_ARTIST, "albumartist", "Album Artist", FAAM_METADATA_KEY_ALBUM_ARTIST, 0 },
    { OPT_ALBUM_ARTIST_SORT, "albumartistsort", "Album Artist Sort", FAAM_METADATA_KEY_ALBUM_ARTIST_SORT, 0 },
    { OPT_COMPOSER, "composer", "Composer", FAAM_METADATA_KEY_COMPOSER, 0 },
    { OPT_COMPOSER_SORT, "composersort", "Composer Sort", FAAM_METADATA_KEY_COMPOSER_SORT, 0 },
    { OPT_YEAR, "year", "Year", FAAM_METADATA_KEY_YEAR, 0 },
    { OPT_COMMENT, "comment", "Comment", FAAM_METADATA_KEY_COMMENT, 0 },
    { OPT_GENRE, "genre", "Genre", FAAM_METADATA_KEY_GENRE, 0 },
    { OPT_COMPILATION, "compilation", "Compilation", FAAM_METADATA_KEY_COMPILATION, 1 },
    { OPT_TRACK, "track", "Track", FAAM_METADATA_KEY_TRACK, 8 },
    { OPT_DISC, "disc", "Disc", FAAM_METADATA_KEY_DISC, 8 },
    { OPT_COVER_ART, "cover-art", "Cover Art", FAAM_METADATA_KEY_COVER_ART, 0 },
    { 0, "encoder", "Encoder", FAAM_METADATA_KEY_ENCODER, 0 },
    { 0, "genre-code", "Genre Code", FAAM_METADATA_KEY_GENRE_CODE, 2 },
    { OPT_LYRICS, "lyrics", "Lyrics", FAAM_METADATA_KEY_LYRICS, 0 },
    { OPT_GROUPING, "grouping", "Grouping", FAAM_METADATA_KEY_GROUPING, 0 },
    { OPT_COPYRIGHT, "copyright", "Copyright", FAAM_METADATA_KEY_COPYRIGHT, 0 },
    { OPT_DESCRIPTION, "description", "Description", FAAM_METADATA_KEY_DESCRIPTION, 0 },
    { OPT_LONG_DESCRIPTION, "long-description", "Long Description", FAAM_METADATA_KEY_LONG_DESCRIPTION, 0 },
    { OPT_WORK, "work", "Work", FAAM_METADATA_KEY_WORK, 0 },
    { OPT_MOVEMENT, "movement", "Movement", FAAM_METADATA_KEY_MOVEMENT, 0 },
    { OPT_BPM, "bpm", "BPM", FAAM_METADATA_KEY_BPM, 2 },
    { OPT_MOVEMENT_NUMBER, "movement-number", "Movement Number", FAAM_METADATA_KEY_MOVEMENT_NUMBER, 2 },
    { OPT_MOVEMENT_TOTAL, "movement-total", "Movement Total", FAAM_METADATA_KEY_MOVEMENT_TOTAL, 2 },
    { OPT_SHOW_WORK_MOVEMENT, "show-work-movement", "Show Work/Movement", FAAM_METADATA_KEY_SHOW_WORK_MOVEMENT, 1 },
};

static const tag_alias *tag_alias_find(int opt, const char *name) {
    for (size_t i = 0; i < sizeof(tag_aliases) / sizeof(tag_aliases[0]); i++)
        if (name ? !strcmp(tag_aliases[i].name, name) : tag_aliases[i].opt == opt) return &tag_aliases[i];
    return NULL;
}

/* The CLI canonicalizes all text tag values to LF for platform-independent
 * files. The public library API remains byte-preserving. */
static size_t normalize_text_line_endings(uint8_t *out, const uint8_t *input, size_t len) {
    size_t output_len = 0;
    for (size_t i = 0; i < len; i++) {
        if (input[i] == '\r') {
            out[output_len++] = '\n';
            if (i + 1 < len && input[i + 1] == '\n') i++;
        } else {
            out[output_len++] = input[i];
        }
    }
    return output_len;
}

static const char *tag_value_set(tag_ctx *t, uint32_t key, uint32_t type,
                                  const char *mean, const char *name,
                                  const void *data, size_t len, bool remove) {
    if (len > UINT32_MAX - 24) return "Metadata value is too large.\n";
    char *copy = NULL;
    if (!remove) {
        copy = (char *)malloc(len + 1);
        char **grown = copy ? realloc(t->owned.v, (t->owned.n + 1) * sizeof(*grown)) : NULL;
        if (!grown) { free(copy); return "Out of memory.\n"; }
        size_t copy_len = len;
        if (type == FAAM_METADATA_UTF8)
            copy_len = normalize_text_line_endings((uint8_t *)copy, data, len);
        else if (len)
            memcpy(copy, data, len);
        copy[copy_len] = 0;
        t->owned.v = grown; t->owned.v[t->owned.n++] = copy;
        len = copy_len;
    }
    uint32_t index = 0;
    while (index < t->num_metadata) {
        const faam_metadata_entry *e = &t->entries[index];
        if (e->key == key && (!name || (e->name && !strcmp(e->name, name) &&
            !strcmp(e->mean ? e->mean : "com.apple.iTunes", mean ? mean : "com.apple.iTunes")))) break;
        index++;
    }
    if (index == t->num_metadata) {
        if (index == t->entries_cap) {
            size_t cap = t->entries_cap ? (size_t)t->entries_cap * 2 : 16;
            if (cap > UINT32_MAX || cap > SIZE_MAX / sizeof(*t->entries)) return "Too many metadata entries.\n";
            faam_metadata_entry *grown = realloc(t->entries, (size_t)cap * sizeof(*grown));
            if (!grown) return "Out of memory.\n";
            t->entries = grown; t->entries_cap = (uint32_t)cap;
        }
        t->num_metadata++;
    }
    t->entries[index] = (faam_metadata_entry){ .struct_size = sizeof(faam_metadata_entry),
        .key = key, .data_type = type, .mean = mean, .name = name,
        .data = copy, .data_bytes = (uint32_t)len, .flags = remove ? FAAM_METADATA_REMOVE : 0 };
    return NULL;
}

static const char *tag_entry_set(tag_ctx *t, const tag_alias *alias,
                                  const void *data, size_t len, bool remove) {
    return tag_value_set(t, alias->key, alias->width ? FAAM_METADATA_INTEGER : FAAM_METADATA_UTF8,
                         NULL, NULL, data, len, remove);
}

/* Strict, length-aware UTF-8 validation for lyrics files. NUL cannot be text. */
static bool lyrics_utf8_valid(const uint8_t *s, size_t len) {
    for (size_t i = 0; i < len;) {
        uint32_t c = s[i++], min = 0;
        unsigned more = 0;
        if (!c) return false;
        if (c < 0x80) continue;
        if (c >= 0xc2 && c <= 0xdf) { more = 1; min = 0x80; c &= 0x1f; }
        else if (c >= 0xe0 && c <= 0xef) { more = 2; min = 0x800; c &= 0xf; }
        else if (c >= 0xf0 && c <= 0xf4) { more = 3; min = 0x10000; c &= 7; }
        else return false;
        if (len - i < more) return false;
        while (more--) {
            if ((s[i] & 0xc0) != 0x80) return false;
            c = (c << 6) | (s[i++] & 0x3f);
        }
        if (c < min || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return false;
    }
    return true;
}

static const char *tag_lyrics_file(tag_ctx *t, const char *path) {
    FILE *f = cli_fopen(path, "rb");
    if (!f) return "Cannot open lyrics file.\n";
    uint8_t *data = NULL;
    size_t len = 0;
    bool ok = cli_read_all(f, INT32_MAX - 24, &data, &len);
    fclose(f);
    if (!ok) return "Cannot read lyrics file (I/O error, too large, or out of memory).\n";
    size_t start = len >= 3 && !memcmp(data, "\xef\xbb\xbf", 3) ? 3 : 0;
    const char *err = lyrics_utf8_valid(data + start, len - start) ?
        tag_entry_set(t, tag_alias_find(OPT_LYRICS, NULL), data + start, len - start, false) :
        "Lyrics file must be valid UTF-8 without NUL bytes.\n";
    free(data);
    return err;
}

static void print_metadata_text(const char *label, const void *data, uint32_t length) {
    const uint8_t *p = data;
    printf("  %s:", label);
    if (memchr(data, '\n', length) || memchr(data, '\r', length)) {
        printf("\n    ");
        for (uint32_t i = 0; i < length; i++) {
            if (p[i] == '\r' || p[i] == '\n') {
                /* Render CR, LF, and CRLF as one host-native line break. */
                if (p[i] == '\r' && i + 1 < length && p[i + 1] == '\n') i++;
                putchar('\n');
                if (i + 1 < length) printf("    ");
                continue;
            }
            putchar(p[i]);
        }
        if (!length || (p[length - 1] != '\n' && p[length - 1] != '\r')) putchar('\n');
    } else {
        putchar(' ');
        if (length) fwrite(data, 1, length, stdout);
        putchar('\n');
    }
}

/* All CLI conveniences produce the same generic public metadata entries. */
static const char *tag_apply(tag_ctx *t, int opt, char *arg)
{
    t->given = true;
    if (opt == OPT_LYRICS_FILE) return tag_lyrics_file(t, arg);
    const tag_alias *alias = tag_alias_find(opt, NULL);
    if (opt == OPT_TAG) {
        char *name = NULL, *value = NULL;
        const char *err = parse_tag_arg(arg, &name, &value);
        if (err) return err;
        const char *n = own_utf8(&t->owned, name), *v = own_utf8(&t->owned, value);
        if (!n || !v) return "Out of memory.\n";
        return tag_value_set(t, FAAM_METADATA_KEY_FREEFORM, FAAM_METADATA_UTF8,
                             NULL, n, v, strlen(v), false);
    }
    if (!alias) return "Unknown metadata option.\n";
    if (opt == OPT_COVER_ART) {
        uint8_t *data = NULL;
        uint64_t size = 0;
        const char *err = load_cover_art(arg, &data, &size);
        if (err) return err;
        err = tag_value_set(t, alias->key, faam_detect_cover_type(data, (uint32_t)size),
                            NULL, NULL, data, (size_t)size, false);
        free(data);
        return err;
    }
    if (opt == OPT_GENRE) {
        const char *name = NULL;
        if (!parse_genre(trim_quotes_and_spaces(arg), &name)) return "Genre number out of range.\n";
        /* The text atom is the one every player reads. A numeric atom already in the file is removed
         * so the two cannot disagree. */
        const char *err = tag_value_set(t, FAAM_METADATA_KEY_GENRE_CODE, FAAM_METADATA_IMPLICIT,
                                        NULL, NULL, NULL, 0, true);
        if (err) return err;
        const char *v = own_utf8(&t->owned, name);
        if (!v) return "Out of memory.\n";
        return tag_entry_set(t, alias, v, strlen(v), false);
    }
    if (opt == OPT_TRACK || opt == OPT_DISC) {
        uint16_t num = 0, total = opt == OPT_TRACK ? t->track_total : t->disc_total;
        /* A bare number keeps the existing total; cmd_tag seeds it. */
        for (uint32_t i = 0; i < t->num_metadata; i++) {
            const faam_metadata_entry *e = &t->entries[i];
            if (e->key == alias->key && e->data_bytes >= 6) total = read_u16_be((const uint8_t *)e->data + 4);
        }
        if (!parse_index_arg(arg, &num, &total))
            return opt == OPT_TRACK ? "Wrong track number.\n" : "Wrong disc number.\n";
        if (opt == OPT_TRACK) t->track_total = total; else t->disc_total = total;
        uint8_t bytes[8] = {0}; write_u16_be(bytes + 2, num); write_u16_be(bytes + 4, total);
        return tag_value_set(t, alias->key, FAAM_METADATA_IMPLICIT, NULL, NULL,
                             num ? bytes : NULL, num ? 8 : 0, !num);
    }
    if (alias->width) {
        unsigned long long value = 1;
        if (alias->width != 1 && !cli_parse_uint(arg, UINT16_MAX, &value))
            return "Metadata number must be an integer from 0 to 65535.\n";
        uint8_t bytes[2]; write_u16_be(bytes, (uint16_t)value);
        return tag_entry_set(t, alias, bytes + (2 - alias->width), alias->width, false);
    }
    const char *v = own_utf8(&t->owned, opt == OPT_LYRICS ? arg : trim_quotes_and_spaces(arg));
    return v ? tag_entry_set(t, alias, v, strlen(v), false) : "Out of memory.\n";
}

static bool metadata_remove_valid(const char *name) {
    return tag_alias_find(0, name) || !strcmp(name, "custom");
}

static int cmd_info(int argc, char **argv)
{
    const char *filepath = NULL;
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "v:hH", long_options, NULL)) != -1) {
        switch (opt) {
        case 'v': if (!set_verbosity(optarg)) return 1; break;
        case 'h': print_usage(HELP_COMMANDS); return 0;
        case 'H': print_usage('H'); return 0;
        default: print_usage('h'); return 1;
        }
    }
    if (optind < argc) filepath = argv[optind];

    if (!filepath) {
        print_usage(HELP_COMMANDS);
        return 1;
    }

    FILE *f = cli_open_seekable_input(filepath);
    if (!f) {
        fprintf(stderr, "Error opening %s\n", filepath);
        return 1;
    }

    faam_io io = cli_faam_io(f);

    faam_demuxer *d = NULL;
    faam_status st = faam_demuxer_open(NULL, &io, &d);
    if (st != FAAM_OK) {
        fprintf(stderr, "Error parsing %s: %s\n", filepath, faam_strerror(st));
        fclose(f);
        return 1;
    }

    uint32_t num_tracks = 0, total_tracks = 0;
    faam_demuxer_get_num_tracks(d, &num_tracks, &total_tracks);
    if (total_tracks == 0) {
        fprintf(stderr, "Error parsing %s: no media tracks found\n", filepath);
        faam_demuxer_close(&d);
        fclose(f);
        return 1;
    }

    faam_gapless_info gapless = {0};
    gapless.struct_size = sizeof(gapless);
    faam_demuxer_get_track_gapless(d, 0, &gapless);

    char brand[5] = {0};
    if (faam_demuxer_get_major_brand(d, brand) != FAAM_OK) brand[0] = '\0';
    for (int i = 3; i >= 0 && brand[i] == ' '; i--) brand[i] = '\0'; /* "M4A " */
    printf("Container: ISO BMFF (major brand %s)\n", brand[0] ? brand : "unknown");
    printf("Tracks Count: %u\n", total_tracks);
    if (total_tracks > num_tracks) printf("Tracks Listed: %u (the library holds at most that many)\n", num_tracks);

    for (uint32_t t = 0; t < num_tracks; t++) {
        faam_track_info ti = {0}; ti.struct_size = sizeof(ti);
        faam_demuxer_get_track_info(d, t, &ti);
        printf("\nTrack #%u (ID %u):\n", t + 1, ti.track_id);
        printf("  Type: %s\n", ti.track_type == FAAM_TRACK_VIDEO ? "Video" :
                            ti.track_type == FAAM_TRACK_AUDIO ? "Audio" : "Other");
        if (ti.codec_id == FAAM_CODEC_GENERIC && ti.fourcc) {
            char cc[5] = { (char)(ti.fourcc >> 24), (char)(ti.fourcc >> 16), (char)(ti.fourcc >> 8), (char)ti.fourcc, 0 };
            for (int k = 0; k < 4; k++) if ((unsigned char)cc[k] < 0x20 || (unsigned char)cc[k] > 0x7e) cc[k] = '?';
            printf("  Codec: Generic (%s)\n", cc);
        } else {
            printf("  Codec: %s\n", ti.codec_id == FAAM_CODEC_H264 ? "H.264 / AVC" :
                                  ti.codec_id == FAAM_CODEC_H265 ? "H.265 / HEVC" :
                                  ti.codec_id == FAAM_CODEC_AAC ? "AAC" : "Generic");
        }
        printf("  Timescale: %u\n", ti.timescale);
        if (ti.track_type == FAAM_TRACK_VIDEO) {
            printf("  Dimensions: %ux%u\n", ti.width, ti.height);
            if (ti.rotation_degrees) printf("  Rotation: %u degrees\n", ti.rotation_degrees);
        } else if (ti.track_type == FAAM_TRACK_AUDIO) {
            printf("  Sample Rate: %u Hz\n", ti.sample_rate);
            printf("  Channels: %u\n", ti.channels);

            if (ti.codec_id == FAAM_CODEC_AAC) {
                uint8_t cdata[FAAM_CODEC_DATA_MAX];
                uint32_t cdata_len = 0;
                if (faam_demuxer_get_codec_data(d, ti.track_id, cdata, sizeof(cdata), &cdata_len) == FAAM_OK && cdata_len >= 2) {
                    AscInfo asc;
                    asc_codec_parse(cdata, cdata_len, &asc);
                    /* The object type is the core's; SBR and PS are what make it HE-AAC. */
                    const char *label = asc.ps_present ? "HE-AAC v2" : asc.sbr_present ? "HE-AAC v1" :
                                        asc.object_type == 2 ? "AAC-LC" : asc.object_type == 42 ? "xHE-AAC / USAC" : NULL;
                    if (label) printf("  AAC Object Type: %d (%s)\n", asc.object_type, label);
                    else printf("  AAC Object Type: %d\n", asc.object_type);
                    printf("  SBR Present: %s\n", asc.sbr_present ? "Yes" : "No");
                    printf("  PS Present: %s\n", asc.ps_present ? "Yes" : "No");
                }
            }
        }
        if (ti.total_frames == 0)
            printf("  Total Frames: 0 (unknown for fragmented files)\n");
        else
            printf("  Total Frames: %u\n", ti.total_frames);
    }

    uint32_t meta_count = 0;
    if (faam_demuxer_get_num_metadata(d, &meta_count) == FAAM_OK && meta_count) {
        printf("\nMetadata Tags:\n");
        for (uint32_t i = 0; i < meta_count; i++) {
            faam_metadata_entry e = { .struct_size = sizeof(e) };
            if (faam_demuxer_get_metadata_entry(d, i, &e) != FAAM_OK) continue;
            const tag_alias *a = NULL;
            for (size_t j = 0; j < sizeof(tag_aliases) / sizeof(tag_aliases[0]); j++)
                if (tag_aliases[j].key == e.key) { a = &tag_aliases[j]; break; }
            const uint8_t *v = e.data;
            if (e.key == FAAM_METADATA_KEY_COVER_ART)
                printf("  Cover Art: present (%u bytes, type %u)\n", e.data_bytes, e.data_type);
            else if (a && a->width == 8 && e.data_bytes >= 6)
                printf("  %s: %u/%u\n", a->label, read_u16_be(v + 2), read_u16_be(v + 4));
            else if (a && a->width && e.data_bytes == a->width)
                printf("  %s: %u\n", a->label, a->width == 1 ? v[0] : read_u16_be(v));
            else if (e.data_type == FAAM_METADATA_UTF8)
                print_metadata_text(e.name ? e.name : a ? a->label : "Text", e.data, e.data_bytes);
            else printf("  Atom 0x%08x: type %u, locale %u, %u bytes\n", e.key, e.data_type, e.locale, e.data_bytes);
        }
    }

    if (gapless.encoder_delay > 0 || gapless.end_padding > 0) {
        printf("\nGapless Audio Metadata:\n");
        printf("  Encoder Delay: %u samples\n", gapless.encoder_delay);
        printf("  Trailing Padding: %u samples\n", gapless.end_padding);
    }

    uint32_t ch_count = 0;
    if (faam_demuxer_get_num_chapters(d, &ch_count) == FAAM_OK && ch_count > 0) {
        printf("\nChapters / Bookmarks (%u entries):\n", ch_count);
        for (uint32_t i = 0; i < ch_count; i++) {
            faam_chapter ch;
            memset(&ch, 0, sizeof(ch));
            ch.struct_size = sizeof(ch);
            if (faam_demuxer_get_chapter(d, i, &ch) != FAAM_OK) continue;
            printf("  Chapter #%u: start=%llu ms, title=\"%s\"\n",
                   i + 1, (unsigned long long)ch.start_ms, ch.title);
        }
    }

    faam_demuxer_close(&d);
    fclose(f);
    return 0;
}

static inline uint32_t read_u32(const uint8_t *b) {
    uint32_t val;
    memcpy(&val, b, 4);
    return htobe32(val);
}

/* Lists the boxes in [offset, end) of buf, which holds the file from file offset base on. */
static void dump_atoms(const uint8_t *buf, uint64_t base, size_t offset, size_t end, int indent)
{
    size_t cur = offset;
    while (cur + 8 <= end) {
        uint32_t size = read_u32(buf + cur);
        char type[5] = {0};
        memcpy(type, buf + cur + 4, 4);

        if (size < 8 || size > end - cur) break;

        for (int i = 0; i < indent; i++) printf("  ");
        printf("[%s] size=%u offset=%llu\n", type, size, (unsigned long long)(base + cur));

        if (memcmp(type, "moov", 4) == 0 || memcmp(type, "trak", 4) == 0 ||
            memcmp(type, "mdia", 4) == 0 || memcmp(type, "minf", 4) == 0 ||
            memcmp(type, "stbl", 4) == 0 || memcmp(type, "udta", 4) == 0 ||
            memcmp(type, "meta", 4) == 0 || memcmp(type, "ilst", 4) == 0 ||
            memcmp(type, "stsd", 4) == 0 || memcmp(type, "avc1", 4) == 0 ||
            memcmp(type, "hvc1", 4) == 0 || memcmp(type, "mp4a", 4) == 0) {
            size_t sub_off = cur + 8;
            if (memcmp(type, "meta", 4) == 0) sub_off += 4;
            if (memcmp(type, "stsd", 4) == 0) sub_off += 8;
            dump_atoms(buf, base, sub_off, cur + size, indent + 1);
        }

        cur += size;
    }
}

#define FAAM_DUMP_MOOV_MAX (256u << 20) /* the same cap the demuxer puts on moov */

static int cmd_dump(int argc, char **argv)
{
    const char *filepath = NULL;
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "v:hH", long_options, NULL)) != -1) {
        if (opt == 'v') { if (!set_verbosity(optarg)) return 1; continue; }
        if (opt == 'h') { print_usage(HELP_COMMANDS); return 0; }
        if (opt == 'H') { print_usage('H'); return 0; }
        print_usage('h');
        return 1;
    }
    if (optind < argc) filepath = argv[optind];

    if (!filepath) {
        print_usage(HELP_COMMANDS);
        return 1;
    }

    FILE *f = cli_open_seekable_input(filepath);
    if (!f) {
        fprintf(stderr, "Error opening %s\n", filepath);
        return 1;
    }

    uint64_t len = 0;
    if (!cli_fsize(f, &len)) {
        fclose(f);
        fprintf(stderr, "Error: cannot find the size of %s\n", filepath);
        return 1;
    }
    if (len < 32) {
        fclose(f);
        fprintf(stderr, "Error: File too short\n");
        return 1;
    }

    if (g_verbose > 0) printf("Dumping MP4 Atom Tree for: %s\n", filepath);

    /* Only the top-level headers are read from the file, so a multi-gigabyte mdat costs nothing;
       a moov is loaded to list what is inside it. */
    int ret = 0;
    uint64_t pos = 0;
    while (pos + 8 <= len) {
        uint8_t hdr[16];
        if (!cli_fseek(f, pos) || fread(hdr, 1, 8, f) != 8) break;
        uint64_t size = read_u32(hdr);
        uint32_t header = 8;
        if (size == 1) {
            if (fread(hdr + 8, 1, 8, f) != 8) break;
            size = ((uint64_t)read_u32(hdr + 8) << 32) | read_u32(hdr + 12);
            header = 16;
        }
        if (size < header || size > len - pos) break;

        char type[5] = {0};
        memcpy(type, hdr + 4, 4);
        printf("[%s] size=%llu offset=%llu\n", type, (unsigned long long)size, (unsigned long long)pos);

        if (memcmp(type, "moov", 4) == 0 && header == 8) {
            if (size > FAAM_DUMP_MOOV_MAX) {
                fprintf(stderr, "Error: the moov box of %s is too large to list\n", filepath);
                ret = 1;
                break;
            }
            uint8_t *buf = (uint8_t *)malloc((size_t)size);
            if (!buf || !cli_fseek(f, pos) || fread(buf, 1, (size_t)size, f) != (size_t)size) {
                free(buf);
                fprintf(stderr, "Error reading the moov box of %s\n", filepath);
                ret = 1;
                break;
            }
            dump_atoms(buf, pos, 8, (size_t)size, 1);
            free(buf);
        }
        pos += size;
    }
    fclose(f);
    return ret;
}

/* No --codec:N given for a given -i: infer from file extension, falling
 * back to aac (matches ADTS being the common bare elementary-stream case). */
static const char *mux_infer_codec_from_ext(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
    const char *base = slash ? slash + 1 : path;
    const char *dot = strrchr(base, '.');
    if (!dot) return "aac";
    if (!strcasecmp(dot, ".264") || !strcasecmp(dot, ".h264")) return "h264";
    if (!strcasecmp(dot, ".265") || !strcasecmp(dot, ".hevc")) return "h265";
    return "aac";
}

/* Reads an entire file into a freshly malloc'd buffer. Returns NULL (*out_len = 0) on an
 * empty file, a file too large for memory, a seek failure or a short read; *why says which. */
static uint8_t *read_whole_file(FILE *f, size_t *out_len, const char **why)
{
    uint64_t size = 0;
    *out_len = 0;
    *why = NULL;
    if (!cli_fsize(f, &size)) { *why = "cannot read it"; return NULL; }
    if (size == 0) { *why = "it is empty"; return NULL; }
    if (size > SIZE_MAX / 2) { *why = "it is too large to read into memory"; return NULL; }

    uint8_t *buf = (uint8_t *)malloc((size_t)size);
    if (!buf) { *why = "it is too large to read into memory"; return NULL; }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        *why = "cannot read it";
        return NULL;
    }
    *out_len = (size_t)size;
    return buf;
}

typedef struct {
    adts_params params;
    uint64_t start; /* offset of the first frame, past any ID3v2 tag */
    uint8_t prefix[10];
    uint8_t prefix_len;
} adts_info;

/* Validates the first ADTS header of f and leaves the position unspecified.
 * Returns NULL, or what is wrong with the input. */
static const char *adts_probe(FILE *f, adts_info *out)
{
    uint8_t h[10];
    uint64_t start = 0;
    size_t got;
    bool streaming = f == stdin;

    out->prefix_len = 0;
    if (!streaming && !cli_fseek(f, 0)) return "cannot seek";
    got = fread(h, 1, sizeof(h), f);
    if (got == 0) return "empty input";
    if (got == sizeof(h) && !memcmp(h, "ID3", 3)) {
        /* An ID3v2 tag some players prepend to ADTS files: synchsafe size, optional footer. */
        start = 10 + (((uint64_t)(h[6] & 0x7F) << 21) | ((uint64_t)(h[7] & 0x7F) << 14) |
                      ((uint64_t)(h[8] & 0x7F) << 7) | (h[9] & 0x7F)) + ((h[5] & 0x10) ? 10 : 0);
        if (streaming) {
            uint64_t skip = start - sizeof(h);
            uint8_t discard[4096];
            while (skip) {
                size_t n = skip < sizeof(discard) ? (size_t)skip : sizeof(discard);
                if (fread(discard, 1, n, f) != n) return "not an ADTS stream";
                skip -= n;
            }
        } else if (!cli_fseek(f, start)) return "not an ADTS stream";
        got = fread(h, 1, 7, f);
        if (streaming && got) {
            memcpy(out->prefix, h, got);
            out->prefix_len = (uint8_t)got;
        }
    } else if (streaming) {
        memcpy(out->prefix, h, got);
        out->prefix_len = (uint8_t)got;
    }
    if (got < 7 || !adts_header_ok(h)) return "not an ADTS stream (no valid frame header at the start)";

    adts_parse_header(h, &out->params);
    if (out->params.channel_cfg == 0) return "unsupported ADTS stream (channel configuration is in the stream, not the header)";
    if (out->params.raw_blocks != 0)
        return "unsupported ADTS stream (more than one raw data block per frame; each frame is muxed as one 1024-sample unit)";
    if (adts_frame_length(h) < adts_header_length(h)) return "not an ADTS stream (first frame length is impossible)";
    out->start = start;
    return NULL;
}

typedef struct { progress_throttle_t throttle; } mux_progress_context;

static bool mux_progress(const progress_info_t *info, void *user)
{
    mux_progress_context *ctx = (mux_progress_context *)user;
    if (info->is_final || progress_throttle_tick(&ctx->throttle, info, 0.033))
        progress_render_cli(info);
    return true;
}

typedef struct {
    faam_track_info track;
    FILE *output;
    bool adts_output;
    adts_params adts;
    bool write_failed;
    bool frame_too_large;
    uint32_t frames;
    uint64_t bytes;
} demux_output_context;

static bool demux_write_sample(void *user, const faam_frame_loc *loc,
                               const uint8_t *frame, uint32_t frame_bytes)
{
    demux_output_context *ctx = (demux_output_context *)user;
    static const uint8_t annexb_sc[4] = { 0x00, 0x00, 0x00, 0x01 };

    if (ctx->track.track_type == FAAM_TRACK_VIDEO) {
        uint32_t pos = 0;
        while (pos + 4 <= frame_bytes) {
            uint32_t nal_len = ((uint32_t)frame[pos] << 24) | ((uint32_t)frame[pos + 1] << 16) |
                               ((uint32_t)frame[pos + 2] << 8) | (uint32_t)frame[pos + 3];
            pos += 4;
            if (pos + nal_len <= frame_bytes) {
                ctx->write_failed |= fwrite(annexb_sc, 1, 4, ctx->output) != 4;
                ctx->write_failed |= fwrite(frame + pos, 1, nal_len, ctx->output) != nal_len;
                pos += nal_len;
            } else break;
        }
    } else {
        if (ctx->adts_output && ctx->track.codec_id == FAAM_CODEC_AAC) {
            if (frame_bytes > ADTS_MAX_FRAME - ADTS_HEADER_SIZE) {
                ctx->frame_too_large = true;
                return false;
            }
            uint8_t adts[ADTS_HEADER_SIZE];
            adts_write_header(&ctx->adts, frame_bytes, adts);
            ctx->write_failed |= fwrite(adts, 1, sizeof(adts), ctx->output) != sizeof(adts);
        }
        ctx->write_failed |= fwrite(frame, 1, frame_bytes, ctx->output) != frame_bytes;
    }
    ctx->frames++;
    ctx->bytes += frame_bytes;
    (void)loc;
    return !ctx->write_failed;
}

static int cmd_mux(int argc, char **argv)
{
    const char *output_file = NULL;
    bool is_m4b = false;
    bool faststart = false;
    bool overwrite = false;
    uint32_t delay = 0;
    uint32_t padding = 0;
    bool gapless_given = false;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t rotation = 0;
    uint32_t fps_num = 30;
    uint32_t fps_den = 1;
    const char *language = "und";
    const char *creation_spec = NULL;
    /* ADTS cannot say whether SBR/PS follow, so the caller states it:
     * none (decoder detects), compatible (sync extension), explicit
     * (hierarchical AOT 5), ps / ps-explicit for HE-AAC v2. */
    const char *sbr_signaling = "none";

    const char *input_files[FAAM_MUX_MAX_TRACKS] = {0};
    const char *codec_override[FAAM_MUX_MAX_TRACKS] = {0};
    int num_inputs = 0;

    int ret = 1;
    char errbuf[256];
    const char *fail = NULL;
    char **filtered = NULL;
    tag_ctx tags;
    FILE *fin[FAAM_MUX_MAX_TRACKS] = {0};
    FILE *fout = NULL;
    bool out_created = false;
    bool mux_stdout = false;
    bool mux_stream = false;
    faam_muxer *m = NULL;
    faam_muxer_config cfg;
    faam_track_config tc[FAAM_MUX_MAX_TRACKS];
    faam_io io;
    mux_stream_output stream_output = {0};
    uint32_t track_id[FAAM_MUX_MAX_TRACKS] = {0};
    uint32_t frames[FAAM_MUX_MAX_TRACKS] = {0};
    adts_info adts[FAAM_MUX_MAX_TRACKS];
    bool has_audio_track = false;
    char time_warn[512];
    faam_status st;

    /* Each track's extradata buffer must stay valid until the single
     * faam_muxer_open() call below (it memcpy's codec_data into its own
     * storage at that point) -- since all tracks are configured before
     * that one call, each needs its own buffer alive simultaneously. */
    uint8_t asc_buf[FAAM_MUX_MAX_TRACKS][16];
    uint32_t asc_len[FAAM_MUX_MAX_TRACKS] = {0};
    uint8_t vcfg_buf[FAAM_MUX_MAX_TRACKS][ANNEXB_CONFIG_MAX];
    uint32_t vcfg_len[FAAM_MUX_MAX_TRACKS] = {0};

    /* Video tracks: file is read into memory once here (during codec_data
     * harvesting) and the same buffer is reused by the AU-packetizer pass
     * below, instead of reading the file starting from disk a second time. Freed
     * as each track's packetizer pass finishes (or on early error exit). */
    uint8_t *vbuf_store[FAAM_MUX_MAX_TRACKS] = {0};
    size_t vbuf_len_store[FAAM_MUX_MAX_TRACKS] = {0};

    memset(&tags, 0, sizeof(tags));

    /* --codec:N (ffmpeg-style stream-indexed option) can't be expressed in
     * a static getopt_long table, so pull "--codec:N <value>" pairs out of
     * argv before the ordinary parse loop, keyed by -i occurrence index. */
    filtered = (char **)malloc(sizeof(char *) * (size_t)argc);
    if (!filtered) return 1;
    int fargc = 0;
    filtered[fargc++] = argv[0];
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--codec:", 8) == 0) {
            char *endptr = NULL;
            long n = strtol(argv[i] + 8, &endptr, 10);
            if (*endptr != '\0' || n < 0 || n >= FAAM_MUX_MAX_TRACKS || i + 1 >= argc) {
                fprintf(stderr, "Error: malformed --codec:N (N must be 0-%d, with a value)\n", FAAM_MUX_MAX_TRACKS - 1);
                goto done;
            }
            const char *c = argv[i + 1];
            if (strcmp(c, "aac") && strcmp(c, "h264") && strcmp(c, "avc") && strcmp(c, "h265") && strcmp(c, "hevc")) {
                fprintf(stderr, "Error: unknown codec \"%s\" for --codec:%ld (use aac, h264 or h265)\n", c, n);
                goto done;
            }
            codec_override[n] = c;
            i++; /* consume the value too */
            continue;
        }
        filtered[fargc++] = argv[i];
    }

    static struct option long_options[] = {
        {"input", required_argument, 0, 'i'},
        {"output", required_argument, 0, 'o'},
        {"overwrite", no_argument, 0, OPT_OVERWRITE},
        {"brand", required_argument, 0, OPT_BRAND},
        {"faststart", no_argument, 0, OPT_FASTSTART},
        {"width", required_argument, 0, OPT_WIDTH},
        {"height", required_argument, 0, OPT_HEIGHT},
        {"rotation", required_argument, 0, OPT_ROTATION},
        {"fps", required_argument, 0, OPT_FPS},
        {"encoder-delay", required_argument, 0, OPT_ENCODER_DELAY},
        {"padding-delay", required_argument, 0, OPT_PADDING_DELAY},
        {"sbr-signaling", required_argument, 0, OPT_SBR_SIGNALING},
        {"language", required_argument, 0, OPT_LANGUAGE},
        {"lang", required_argument, 0, OPT_LANGUAGE},
        {"creation-time", required_argument, 0, OPT_CREATION_TIME},
        TAG_LONG_OPTIONS,
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    optind = 1;
    while (!fail && (opt = getopt_long(fargc, filtered, "v:i:o:hH", long_options, NULL)) != -1) {
        unsigned long long v;
        switch (opt) {
        case 'v': if (!set_verbosity(optarg)) return 1; break;
        case 'i':
            if (num_inputs >= FAAM_MUX_MAX_TRACKS) {
                snprintf(errbuf, sizeof(errbuf), "Error: too many -i inputs (max %d)\n", FAAM_MUX_MAX_TRACKS);
                fail = errbuf;
                break;
            }
            input_files[num_inputs++] = optarg;
            break;
        case 'o': output_file = optarg; break;
        case OPT_OVERWRITE: overwrite = true; break;
        case OPT_FASTSTART: faststart = true; break;
        case OPT_BRAND:
            if (!strcasecmp(optarg, "m4b")) is_m4b = true;
            else if (strcasecmp(optarg, "m4a")) fail = bad_value(errbuf, sizeof(errbuf), "--brand", optarg, "use m4a or m4b");
            break;
        case OPT_WIDTH:
            if (!cli_parse_uint(optarg, 65535, &v) || v == 0) fail = bad_value(errbuf, sizeof(errbuf), "--width", optarg, "1-65535 pixels");
            else width = (uint32_t)v;
            break;
        case OPT_HEIGHT:
            if (!cli_parse_uint(optarg, 65535, &v) || v == 0) fail = bad_value(errbuf, sizeof(errbuf), "--height", optarg, "1-65535 pixels");
            else height = (uint32_t)v;
            break;
        case OPT_ROTATION:
            if (!cli_parse_uint(optarg, 270, &v) || v % 90) fail = bad_value(errbuf, sizeof(errbuf), "--rotation", optarg, "0, 90, 180 or 270 degrees clockwise");
            else rotation = (uint32_t)v;
            break;
        case OPT_FPS:
            if (!cli_parse_fps(optarg, &fps_num, &fps_den)) fail = bad_value(errbuf, sizeof(errbuf), "--fps", optarg, "n or n/d, up to 1000000");
            break;
        case OPT_ENCODER_DELAY:
            if (!cli_parse_uint(optarg, UINT32_MAX, &v)) fail = bad_value(errbuf, sizeof(errbuf), "--encoder-delay", optarg, "a sample count");
            else { delay = (uint32_t)v; gapless_given = true; }
            break;
        case OPT_PADDING_DELAY:
            if (!cli_parse_uint(optarg, UINT32_MAX, &v)) fail = bad_value(errbuf, sizeof(errbuf), "--padding-delay", optarg, "a sample count");
            else { padding = (uint32_t)v; gapless_given = true; }
            break;
        case OPT_SBR_SIGNALING:
            if (strcmp(optarg, "none") && strcmp(optarg, "compatible") && strcmp(optarg, "explicit") &&
                strcmp(optarg, "ps") && strcmp(optarg, "ps-explicit"))
                fail = bad_value(errbuf, sizeof(errbuf), "--sbr-signaling", optarg,
                                 "use none, compatible, explicit, ps or ps-explicit");
            else sbr_signaling = optarg;
            break;
        case OPT_LANGUAGE: {
            const char *l = trim_quotes_and_spaces(optarg);
            bool ok = strlen(l) == 3;
            for (int i = 0; ok && i < 3; i++)
                ok = (l[i] >= 'a' && l[i] <= 'z') || (l[i] >= 'A' && l[i] <= 'Z');
            if (!ok) fail = bad_value(errbuf, sizeof(errbuf), "--language", optarg, "an ISO 639-2/T 3-letter code such as eng");
            else language = l;
            break;
        }
        case OPT_CREATION_TIME: creation_spec = optarg; break;
        case 'h': print_usage(HELP_MUX); ret = 0; goto done;
        case 'H': print_usage('H'); ret = 0; goto done;
        default:
            if (opt >= OPT_TAG_FIRST && opt <= OPT_TAG_LAST) {
                fail = tag_apply(&tags, opt, optarg);
            } else {
                print_usage('h');
                goto done;
            }
            break;
        }
    }
    if (fail) {
        fprintf(stderr, "%s", fail);
        goto done;
    }

    if (num_inputs == 0) {
        print_usage(HELP_MUX);
        goto done;
    }
    if (!output_file) {
        fprintf(stderr, "Error: -o <out.mp4> is required\n");
        goto done;
    }
    for (int i = num_inputs; i < FAAM_MUX_MAX_TRACKS; i++) {
        if (codec_override[i]) {
            fprintf(stderr, "Error: --codec:%d has no matching -i\n", i);
            goto done;
        }
    }
    /* Opening the output truncates it, which --overwrite allows, so this is checked first and
       on its own: the same file reached through a link or another spelling is still an input. */
    for (int i = 0; i < num_inputs; i++) {
        if (strcmp(input_files[i], "-") == 0) {
            for (int j = i + 1; j < num_inputs; j++) {
                if (strcmp(input_files[j], "-") == 0) {
                    fprintf(stderr, "Error: stdin can be used for only one input track\n");
                    goto done;
                }
            }
        }
        if (strcmp(output_file, "-") != 0 && strcmp(input_files[i], "-") != 0 &&
            cli_same_file(input_files[i], output_file)) {
            fprintf(stderr, "Error: the output %s is the same file as the input %s\n", output_file, input_files[i]);
            goto done;
        }
    }

    print_version_banner();

    for (int i = 0; i < num_inputs; i++) {
        const char *codec_str = codec_override[i] ? codec_override[i] : mux_infer_codec_from_ext(input_files[i]);
        bool streaming_aac = !strcmp(input_files[i], "-") && strcmp(codec_str, "aac") == 0;
        if (streaming_aac) {
            if (!cli_binary_stream(stdin)) {
                fprintf(stderr, "Cannot set binary input mode\n");
                goto done;
            }
            fin[i] = stdin;
        } else {
            fin[i] = cli_open_seekable_input(input_files[i]);
        }
        if (!fin[i]) {
            fprintf(stderr, "Error opening %s\n", input_files[i]);
            goto done;
        }
    }

    faam_muxer_config_init(&cfg, sizeof(cfg));
    if (is_m4b) cfg.flags |= FAAM_MUXER_M4B;
    if (faststart) cfg.flags |= FAAM_MUXER_FASTSTART;
    memset(tc, 0, sizeof(tc));
    for (int i = 0; i < FAAM_MUX_MAX_TRACKS; i++) tc[i].struct_size = sizeof(tc[i]);

    for (int i = 0; i < num_inputs; i++) {
        const char *codec_str = codec_override[i] ? codec_override[i] : mux_infer_codec_from_ext(input_files[i]);
        bool is_video = strcmp(codec_str, "aac") != 0;

        if (is_video && (!width || !height)) {
            fprintf(stderr, "Error: %s is video and needs both --width and --height (the stream's SPS is not parsed)\n",
                    input_files[i]);
            goto done;
        }

        if (is_video) {
            bool hevc = strcmp(codec_str, "h265") == 0 || strcmp(codec_str, "hevc") == 0;
            const char *why = NULL;

            tc[i].track_type = FAAM_TRACK_VIDEO;
            tc[i].codec_id = hevc ? FAAM_CODEC_H265 : FAAM_CODEC_H264;
            tc[i].timescale = FAAM_VIDEO_TIMESCALE;
            tc[i].width = (uint16_t)width;
            tc[i].height = (uint16_t)height;
            tc[i].rotation_degrees = (uint16_t)rotation;

            /* The whole stream is read once: first for the avcC/hvcC, then the same buffer is cut
             * into access units below instead of reading the file again. */
            vbuf_store[i] = read_whole_file(fin[i], &vbuf_len_store[i], &why);
            if (!vbuf_store[i]) {
                fprintf(stderr, "Error: %s: %s\n", input_files[i], why);
                goto done;
            }
            const char *cfg_err = annexb_build_config(hevc ? ANNEXB_H265 : ANNEXB_H264, vbuf_store[i], vbuf_len_store[i],
                                                       vcfg_buf[i], sizeof(vcfg_buf[i]), &vcfg_len[i]);
            if (cfg_err) {
                fprintf(stderr, "Error: %s: %s\n", input_files[i], cfg_err);
                goto done;
            }
            tc[i].codec_data = vcfg_buf[i];
            tc[i].codec_data_len = vcfg_len[i];
        } else {
            const char *perr = adts_probe(fin[i], &adts[i]);
            if (perr) {
                fprintf(stderr, "Error: %s: %s\n", input_files[i], perr);
                goto done;
            }
            /* HE-AAC v2 is a mono core plus parametric stereo. */
            if (!strncmp(sbr_signaling, "ps", 2) && adts[i].params.channel_cfg != 1) {
                fprintf(stderr, "Error: %s: --sbr-signaling %s needs a mono core (HE-AAC v2), but the stream has channel configuration %u\n",
                        input_files[i], sbr_signaling, adts[i].params.channel_cfg);
                goto done;
            }

            AscBuildInfo build = {0};
            build.object_type = (uint8_t)(adts[i].params.profile + 1);
            build.sr_idx = adts[i].params.sr_idx;
            build.channels = adts[i].params.channel_cfg;
            if (strcmp(sbr_signaling, "none") != 0) {
                build.sbr_present = true;
                build.sbr_sr_idx = adts[i].params.sr_idx >= 3 ? adts[i].params.sr_idx - 3 : 0; /* double rate: table 1.16 steps by 3 */
                build.hierarchical = strstr(sbr_signaling, "explicit") != NULL;
                build.ps_signaled = strncmp(sbr_signaling, "ps", 2) == 0;
                build.ps_present = build.ps_signaled;
            }
            asc_len[i] = asc_codec_build(&build, asc_buf[i], sizeof(asc_buf[i]));
            if (asc_len[i] == 0) {
                fprintf(stderr, "Error: %s: cannot describe this stream in an AudioSpecificConfig\n", input_files[i]);
                goto done;
            }

            static const uint8_t channels_of_config[8] = { 0, 1, 2, 3, 4, 5, 6, 8 };
            tc[i].track_type = FAAM_TRACK_AUDIO;
            tc[i].codec_id = FAAM_CODEC_AAC;
            tc[i].sample_rate = asc_codec_sample_rates[adts[i].params.sr_idx];
            tc[i].timescale = tc[i].sample_rate;
            tc[i].channels = channels_of_config[adts[i].params.channel_cfg];
            memcpy(tc[i].language, language, 3);
            tc[i].codec_data = asc_buf[i];
            tc[i].codec_data_len = asc_len[i];
            has_audio_track = true;
        }
    }

    /* The delay and padding are properties of an audio track's edit list; with none they would
       be dropped without a word. */
    if (gapless_given && !has_audio_track) {
        fprintf(stderr, "Error: --encoder-delay and --padding-delay need an audio track to apply to\n");
        goto done;
    }

    faam_gapless_info track_gapless;
    memset(&track_gapless, 0, sizeof(track_gapless));
    track_gapless.struct_size = sizeof(track_gapless);
    if (has_audio_track) {
        track_gapless.encoder_delay = delay;
        track_gapless.end_padding = padding;
        cfg.gapless = &track_gapless;
    }

    /* The first input stands in for "auto"; it is the file the user is converting. */
    if (!resolve_creation_time(creation_spec, input_files[0], &cfg.creation_time, time_warn, sizeof(time_warn))) {
        fprintf(stderr, "Error: %s", time_warn);
        goto done;
    }
    if (g_verbose > 0 && time_warn[0]) fprintf(stderr, "Warning: %s", time_warn);

    if (tags.given) { cfg.metadata = tags.entries; cfg.num_metadata = tags.num_metadata; }

    cfg.tracks = tc;
    cfg.num_tracks = (uint32_t)num_inputs;

    if (strcmp(output_file, "-") != 0 && output_refused(output_file, overwrite)) goto done;
    /* Faststart reads back what it wrote to move it. */
    mux_stdout = strcmp(output_file, "-") == 0;
    /* Stdout is intended for pipelines. Use a fixed one-second fragment
     * interval automatically when this build supports it. Fragmented MP4
     * cannot preserve M4B branding or the CLI's late-known gapless duration,
     * so those requests retain progressive output. */
    faam_library_info library_info = {0};
    library_info.struct_size = sizeof(library_info);
    bool can_fragment = faam_get_library_info(&library_info) == FAAM_OK &&
                        (library_info.flags & FAAM_LIB_FRAGMENTED) != 0;
    mux_stream = mux_stdout && !faststart && !is_m4b && !gapless_given && can_fragment;
    if (mux_stream) cfg.fragment_ms = 1000;
    if (mux_stdout && !cli_binary_stream(stdout)) {
        fprintf(stderr, "Cannot set binary output mode\n");
        goto done;
    }
    if (mux_stream) {
        stream_output.spool = tmpfile();
        stream_output.output = stdout;
    } else {
        fout = mux_stdout ? tmpfile() : cli_fopen(output_file, faststart ? "w+b" : "wb");
    }
    if ((mux_stream && !stream_output.spool) || (!mux_stream && !fout)) {
        fprintf(stderr, "Error creating %s\n", output_file);
        goto done;
    }
    out_created = !mux_stdout;

    if (mux_stream) {
        io = mux_stream_io(&stream_output);
    } else io = cli_faam_io(fout);

    st = faam_muxer_open(&cfg, &io, &m);
    if (st != FAAM_OK) {
        fprintf(stderr, "Error initializing muxer: %s\n", faam_strerror(st));
        goto done;
    }
    for (int i = 0; i < num_inputs; i++) faam_muxer_get_track_id(m, (uint32_t)i, &track_id[i]);

    uint64_t total_input_bytes = 0;
    for (int i = 0; i < num_inputs; i++) {
        if (fin[i] && fin[i] != stdin) {
            uint64_t sz = 0;
            if (cli_fsize(fin[i], &sz)) total_input_bytes += sz;
        }

        if (tc[i].track_type == FAAM_TRACK_AUDIO) {
            if (fin[i] != stdin && !cli_fseek(fin[i], adts[i].start)) {
                fprintf(stderr, "Error reading %s\n", input_files[i]);
                goto done;
            }
            bool multi_block = false;
            mux_progress_context progress = {0};
            progress_throttle_reset(&progress.throttle);
            st = faam_engine_mux_adts_frames(m, track_id[i], fin[i], &adts[i].params,
                                             adts[i].prefix, adts[i].prefix_len,
                                             &frames[i], &multi_block,
                                             g_verbose > 0 ? mux_progress : NULL, &progress);
            if (multi_block) {
                fprintf(stderr, "Error: %s: a frame has more than one raw data block, which is not supported "
                        "(each frame is muxed as one 1024-sample unit)\n", input_files[i]);
                goto done;
            }
        } else {
            mux_progress_context progress = {0};
            progress_throttle_reset(&progress.throttle);
            st = faam_engine_mux_video_frames(m, track_id[i],
                                              tc[i].codec_id == FAAM_CODEC_H265 ? ANNEXB_H265 : ANNEXB_H264,
                                              vbuf_store[i], vbuf_len_store[i], fps_num, fps_den,
                                              &frames[i], g_verbose > 0 ? mux_progress : NULL, &progress);
            free(vbuf_store[i]);
            vbuf_store[i] = NULL;
            if (st == FAAM_ERR_NO_MEMORY) {
                fprintf(stderr, "Error: out of memory\n");
                goto done;
            }
        }

        if (st == (faam_status)FAAM_ENGINE_CANCELLED) goto done;
        if (st != FAAM_OK) {
            fprintf(stderr, "Error muxing %s: %s\n", input_files[i], faam_strerror(st));
            goto done;
        }
        if (frames[i] == 0) {
            fprintf(stderr, "Error: %s: no %s frames found\n", input_files[i],
                    tc[i].track_type == FAAM_TRACK_AUDIO ? "ADTS" : "video");
            goto done;
        }
    }

    st = faam_muxer_finalize(m);
    if (st != FAAM_OK) {
        fprintf(stderr, "Error finalizing %s: %s\n", output_file, faam_strerror(st));
        goto done;
    }
    faam_muxer_close(&m);
    if (mux_stdout && !mux_stream && (!cli_fseek(fout, 0) || !cli_copy_stream(fout, stdout))) {
        fclose(fout);
        fout = NULL;
        fprintf(stderr, "Error writing %s\n", output_file);
        goto done;
    }
    if (mux_stream) {
        if (!stream_output.spool || !cli_finish_output(&stream_output.spool)) {
            stream_output.spool = NULL;
            fprintf(stderr, "Error writing %s\n", output_file);
            goto done;
        }
        stream_output.spool = NULL;
    } else {
        if (!cli_finish_output(&fout)) {
            fprintf(stderr, "Error writing %s\n", output_file);
            goto done;
        }
        fout = NULL;
    }

    if (g_verbose > 0)
        fprintf(mux_stdout ? stderr : stdout, "Successfully muxed %d input(s) -> %s\n", num_inputs, output_file);

    if (g_verbose >= 2 && !mux_stdout) {
        uint64_t output_size = 0;
        FILE *fout_stat = cli_fopen(output_file, "rb");
        if (fout_stat) {
            cli_fsize(fout_stat, &output_size);
            fclose(fout_stat);
        }

        fprintf(stderr, "\n--- Mux Summary ---\n");
        fprintf(stderr, " Tracks Muxed       : %d track(s)\n", num_inputs);
        for (int i = 0; i < num_inputs; i++) {
            fprintf(stderr, "   Track #%d           : %s | %u frame(s)\n",
                    i + 1, tc[i].track_type == FAAM_TRACK_AUDIO ? "AAC Audio" : "Video", frames[i]);
        }
        if (total_input_bytes > 0 && output_size > 0) {
            fprintf(stderr, " File Size           : Input = %.2f MB | Output = %.2f MB | Ratio = %.2f:1\n",
                    total_input_bytes / (1024.0 * 1024.0), output_size / (1024.0 * 1024.0),
                    (double)total_input_bytes / (double)output_size);
        }
        fprintf(stderr, "--------------------\n");
    }

    ret = 0;

done:
    if (m) faam_muxer_close(&m);
    for (int i = 0; i < FAAM_MUX_MAX_TRACKS; i++) {
        if (fin[i] && fin[i] != stdin) fclose(fin[i]);
        free(vbuf_store[i]);
    }
    if (mux_stream && stream_output.spool) fclose(stream_output.spool);
    else if (fout) fclose(fout);
    if (ret != 0 && out_created) cli_remove(output_file);
    tag_free(&tags);
    free(filtered);
    return ret;
}

static int cmd_demux(int argc, char **argv)
{
    const char *input_file = NULL;
    const char *output_file = "output.raw";
    const char *export_asc = NULL;
    uint32_t selected_track_id = 0;
    bool overwrite = false;
    bool demux_stdout = false;
    int output_format = 0; /* 0: infer from filename, 1: raw AAC, 2: ADTS */

    static struct option long_options[] = {
        {"output", required_argument, 0, 'o'},
        {"track", required_argument, 0, 't'},
        {"export-asc", required_argument, 0, OPT_EXPORT_ASC},
        {"format", required_argument, 0, OPT_FORMAT},
        {"overwrite", no_argument, 0, OPT_OVERWRITE},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "v:o:t:hH", long_options, NULL)) != -1) {
        unsigned long long v;
        switch (opt) {
        case 'v': if (!set_verbosity(optarg)) return 1; break;
        case 'o': output_file = optarg; break;
        case 't':
            if (!cli_parse_uint(optarg, UINT32_MAX, &v) || v == 0) {
                fprintf(stderr, "Error: invalid --track value \"%s\" (a track ID, 1 or more)\n", optarg);
                return 1;
            }
            selected_track_id = (uint32_t)v;
            break;
        case OPT_EXPORT_ASC: export_asc = optarg; break;
        case OPT_FORMAT:
            if (!strcmp(optarg, "raw")) output_format = 1;
            else if (!strcmp(optarg, "adts")) output_format = 2;
            else {
                fprintf(stderr, "Error: invalid --format value \"%s\" (use raw or adts)\n", optarg);
                return 1;
            }
            break;
        case OPT_OVERWRITE: overwrite = true; break;
        case 'h': print_usage(HELP_INSPECT); return 0;
        case 'H': print_usage('H'); return 0;
        default: print_usage('h'); return 1;
        }
    }

    if (optind < argc) input_file = argv[optind];

    if (!input_file) {
        print_usage(HELP_INSPECT);
        return 1;
    }
    print_version_banner();
    demux_stdout = strcmp(output_file, "-") == 0;

    FILE *fin = cli_open_seekable_input(input_file);
    if (!fin) {
        fprintf(stderr, "Error opening %s\n", input_file);
        return 1;
    }

    faam_io io_in = cli_faam_io(fin);

    int ret = 1;
    FILE *fout = NULL;
    bool out_created = false;
    bool asc_created = false;
    faam_demuxer *d = NULL;
    uint32_t num_tracks = 0;
    faam_track_info ti;
    faam_status st;
    st = faam_demuxer_open(NULL, &io_in, &d);
    if (st != FAAM_OK) {
        fprintf(stderr, "Error initializing demuxer on %s: %s\n", input_file, faam_strerror(st));
        goto done;
    }

    faam_demuxer_get_num_tracks(d, &num_tracks, NULL);

    memset(&ti, 0, sizeof(ti));
    ti.struct_size = sizeof(ti);
    for (uint32_t t = 0; t < num_tracks; t++) {
        faam_track_info tmp_info = {0}; tmp_info.struct_size = sizeof(tmp_info);
        if (faam_demuxer_get_track_info(d, t, &tmp_info) == FAAM_OK &&
            (selected_track_id == 0 || tmp_info.track_id == selected_track_id)) {
            ti = tmp_info;
            break;
        }
    }
    if (ti.track_id == 0) {
        if (selected_track_id) fprintf(stderr, "Error: %s has no track with ID %u\n", input_file, selected_track_id);
        else fprintf(stderr, "Error: %s has no tracks\n", input_file);
        goto done;
    }
    if (output_format == 2 && (ti.track_type != FAAM_TRACK_AUDIO || ti.codec_id != FAAM_CODEC_AAC)) {
        fprintf(stderr, "Error: --format adts requires an AAC audio track\n");
        goto done;
    }

    /* Opening an output truncates it, which --overwrite allows: an output that is the input, or
     * each other, would destroy what is still to be read. Checked on its own, ahead of --overwrite. */
    if ((strcmp(input_file, "-") != 0 && strcmp(output_file, "-") != 0 && cli_same_file(input_file, output_file)) ||
        (export_asc && strcmp(input_file, "-") != 0 && cli_same_file(input_file, export_asc))) {
        fprintf(stderr, "Error: an output file is the same file as the input %s\n", input_file);
        goto done;
    }
    if (export_asc && (!strcmp(output_file, export_asc) || cli_same_file(output_file, export_asc))) {
        fprintf(stderr, "Error: -o and --export-asc name the same file\n");
        goto done;
    }

    /* Both files are checked before either is written, so a refusal leaves nothing behind. */
    if ((strcmp(output_file, "-") != 0 && output_refused(output_file, overwrite)) ||
        (export_asc && output_refused(export_asc, overwrite)))
        goto done;

    bool is_adts_out = output_format == 2 ||
        (output_format == 0 && (strstr(output_file, ".aac") != NULL || strstr(output_file, ".adts") != NULL));
    adts_params adts_p = {0};
    if (is_adts_out && ti.track_type == FAAM_TRACK_AUDIO && ti.codec_id == FAAM_CODEC_AAC) {
        uint8_t cdata[FAAM_CODEC_DATA_MAX];
        uint32_t cdata_len = 0;
        st = faam_demuxer_get_codec_data(d, ti.track_id, cdata, sizeof(cdata), &cdata_len);
        if (st != FAAM_OK || cdata_len < 2) {
            fprintf(stderr, "Error: track %u has no AudioSpecificConfig to build ADTS headers from\n", ti.track_id);
            goto done;
        }
        AscInfo asc;
        asc_codec_parse(cdata, cdata_len, &asc);
        const char *why = adts_params_from_asc(&asc, false, &adts_p);
        if (why) {
            fprintf(stderr, "Error: track %u cannot be written as ADTS, %s\n", ti.track_id, why);
            goto done;
        }
    }

    if (export_asc) {
        uint8_t cdata[FAAM_CODEC_DATA_MAX];
        uint32_t cdata_len = 0;
        st = faam_demuxer_get_codec_data(d, ti.track_id, cdata, sizeof(cdata), &cdata_len);
        if (st != FAAM_OK) {
            fprintf(stderr, "Error reading codec data of track %u: %s\n", ti.track_id, faam_strerror(st));
            goto done;
        }
        FILE *fasc = cli_fopen(export_asc, "wb");
        if (!fasc) {
            fprintf(stderr, "Error creating %s\n", export_asc);
            goto done;
        }
        asc_created = true;
        bool asc_ok = fwrite(cdata, 1, cdata_len, fasc) == cdata_len;
        if (fclose(fasc) != 0 || !asc_ok) {
            fprintf(stderr, "Error writing %s\n", export_asc);
            goto done;
        }
        if (g_verbose > 0)
            fprintf(demux_stdout ? stderr : stdout, "Exported codec extradata (%u bytes) to %s\n", cdata_len, export_asc);
    }

    if (demux_stdout && !cli_binary_stream(stdout)) {
        fprintf(stderr, "Cannot set binary output mode\n");
        goto done;
    }
    fout = demux_stdout ? stdout : cli_fopen(output_file, "wb");
    if (!fout) {
        fprintf(stderr, "Error opening output %s\n", output_file);
        goto done;
    }
    out_created = !demux_stdout;

    demux_output_context output_ctx = { .track = ti, .output = fout,
                                        .adts_output = is_adts_out, .adts = adts_p };
    mux_progress_context progress = {0};
    progress_throttle_reset(&progress.throttle);
    st = faam_engine_demux_track(d, &ti, demux_write_sample, &output_ctx,
                                 g_verbose > 0 ? mux_progress : NULL, &progress);
    if (st == FAAM_ERR_NO_MEMORY) {
        fprintf(stderr, "Error: out of memory\n");
        goto done;
    }
    if (st == (faam_status)FAAM_ENGINE_CANCELLED && output_ctx.frame_too_large) {
        fprintf(stderr, "Error: a frame of track %u is too large for an ADTS header\n", ti.track_id);
        goto done;
    }
    if (st != FAAM_OK && st != (faam_status)FAAM_ENGINE_CANCELLED) {
        fprintf(stderr, "Error reading a frame of track %u from %s: %s\n", ti.track_id, input_file, faam_strerror(st));
        goto done;
    }

    bool write_failed = output_ctx.write_failed || !cli_finish_output(&fout);
    if (write_failed) {
        fprintf(stderr, "Error writing %s\n", output_file);
        goto done;
    }

    if (g_verbose > 0)
        fprintf(demux_stdout ? stderr : stdout, "Successfully demuxed %s -> %s\n", input_file, output_file);

    if (g_verbose >= 2) {
        fprintf(stderr, "\n--- Demux Summary ---\n");
        fprintf(stderr, " Track #%u           : %s | %u frame(s)\n", ti.track_id,
                ti.track_type == FAAM_TRACK_VIDEO ? "Video" : "Audio", output_ctx.frames);
        fprintf(stderr, " Payload            : %.2f MB\n", output_ctx.bytes / (1024.0 * 1024.0));
        fprintf(stderr, "---------------------\n");
    }
    ret = 0;

done:
    if (fout && fout != stdout) fclose(fout);
    if (ret != 0) {
        if (out_created) cli_remove(output_file);
        if (asc_created) cli_remove(export_asc);
    }
    faam_demuxer_close(&d);
    fclose(fin);
    return ret;
}

static int cmd_tag(int argc, char **argv)
{
    const char *filepath = NULL;
    tag_ctx t;
    memset(&t, 0, sizeof(t));

    bool want_clear = false;
    char remove_names[FAAM_TAG_MAX_REMOVE][32];
    int num_remove = 0;
    faam_demuxer *d = NULL;
    FILE *f = NULL;
    int ret = 1;

    static struct option long_options[] = {
        TAG_LONG_OPTIONS,
        {"remove", required_argument, 0, OPT_REMOVE},
        {"clear", no_argument, 0, OPT_CLEAR},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    /* Pass 1: only look for --clear/--remove, so their effect ("clear/strip
     * before applying anything else in this invocation") doesn't depend on
     * where they happen to fall in argv relative to the value-setting flags. */
    {
        int opt;
        int saved_opterr = opterr;
        opterr = 0;
        optind = 1;
        while ((opt = getopt_long(argc, argv, "v:hH", long_options, NULL)) != -1) {
            if (opt == 'v') { if (!set_verbosity(optarg)) return 1; }
            else if (opt == 'h') { print_usage(HELP_TAG); return 0; }
            else if (opt == 'H') { print_usage('H'); return 0; }
            else if (opt == OPT_CLEAR) {
                want_clear = true;
            } else if (opt == OPT_REMOVE) {
                if (!metadata_remove_valid(optarg)) {
                    fprintf(stderr, "Error: unknown --remove field \"%s\"\n", optarg);
                    return 1;
                }
                if (num_remove >= FAAM_TAG_MAX_REMOVE) {
                    fprintf(stderr, "Error: too many --remove options (max %d)\n", FAAM_TAG_MAX_REMOVE);
                    return 1;
                }
                strncpy(remove_names[num_remove], optarg, sizeof(remove_names[0]) - 1);
                remove_names[num_remove][sizeof(remove_names[0]) - 1] = '\0';
                num_remove++;
            }
        }
        opterr = saved_opterr;
    }

    if (optind < argc) filepath = argv[optind];
    /* optind above reflects pass 1's scan; re-derive it after pass 2 below. */

    if (!filepath) {
        print_usage(HELP_TAG);
        return 1;
    }

    f = cli_fopen(filepath, "r+b");
    if (!f) {
        fprintf(stderr, "Error opening %s\n", filepath);
        return 1;
    }

    faam_io io = cli_faam_io(f);

    if (!want_clear) {
        /* Validate the file and collect totals/freeform keys needed by CLI conveniences. */
        uint32_t num_tracks = 0;
        faam_status rd;
        /* Namespace/name pointers for custom removals are borrowed until the update. */
        rd = faam_demuxer_open(NULL, &io, &d);
        if (rd != FAAM_OK) {
            fprintf(stderr, "Error reading the existing tags of %s: %s (nothing was changed)\n", filepath, faam_strerror(rd));
            goto done;
        }
        faam_demuxer_get_num_tracks(d, &num_tracks, NULL);
        if (num_tracks == 0) {
            fprintf(stderr, "Error: %s has no tracks; it is not an MP4 file this tool can tag (nothing was changed)\n", filepath);
            goto done;
        }
        /* Preserve existing totals when --track/--disc supply only a number. */
        uint32_t existing_count = 0;
        rd = faam_demuxer_get_num_metadata(d, &existing_count);
        if (rd != FAAM_OK) goto done;
        bool remove_custom = false;
        for (int j = 0; j < num_remove; j++) if (!strcmp(remove_names[j], "custom")) remove_custom = true;
        for (uint32_t i = 0; i < existing_count; i++) {
            faam_metadata_entry e = { .struct_size = sizeof(e) };
            if (faam_demuxer_get_metadata_entry(d, i, &e) != FAAM_OK) goto done;
            if (e.data_bytes >= 6 && e.key == FAAM_METADATA_KEY_TRACK) t.track_total = read_u16_be((const uint8_t *)e.data + 4);
            if (e.data_bytes >= 6 && e.key == FAAM_METADATA_KEY_DISC) t.disc_total = read_u16_be((const uint8_t *)e.data + 4);
            bool custom = remove_custom && e.key == FAAM_METADATA_KEY_FREEFORM &&
                !(e.name && !strcmp(e.name, "iTunSMPB") && (!e.mean || !strcmp(e.mean, "com.apple.iTunes")));
            const char *err = NULL;
            if (custom) err = tag_value_set(&t, e.key, e.data_type, e.mean, e.name, NULL, 0, true);
            if (err) { fprintf(stderr, "%s", err); goto done; }
        }
    }
    for (int i = 0; i < num_remove; i++) {
        if (!strcmp(remove_names[i], "track")) t.track_total = 0;
        if (!strcmp(remove_names[i], "disc")) t.disc_total = 0;
        const tag_alias *alias = tag_alias_find(0, remove_names[i]);
        const char *err = alias ? tag_entry_set(&t, alias, NULL, 0, true) : NULL;
        if (!err && !strcmp(remove_names[i], "genre"))
            err = tag_entry_set(&t, tag_alias_find(0, "genre-code"), NULL, 0, true);
        if (err) { fprintf(stderr, "%s", err); goto done; }
    }

    /* Pass 2: value setters override earlier removals in the patch. */
    const char *err_msg = NULL;
    int opt;
    optind = 1;
    bool bad_option = false;
    while ((opt = getopt_long(argc, argv, "v:hH", long_options, NULL)) != -1) {
        if (opt == 'v') { if (!set_verbosity(optarg)) return 1; }
        else if (opt >= OPT_TAG_FIRST && opt <= OPT_TAG_LAST) {
            err_msg = tag_apply(&t, opt, optarg);
        } else if (opt != OPT_REMOVE && opt != OPT_CLEAR && opt != 'h' && opt != 'H') {
            bad_option = true; /* the others were handled in pass 1 */
        }
        if (err_msg || bad_option) break;
    }

    if (err_msg || bad_option) {
        if (err_msg) fprintf(stderr, "%s", err_msg);
        else print_usage('h');
        goto done;
    }

    faam_status st = faam_update_metadata_stream(&io, t.entries, t.num_metadata, want_clear ? FAAM_METADATA_UPDATE_CLEAR : 0);
    if (st != FAAM_OK) {
        fprintf(stderr, "Error updating tags on %s: %s\n", filepath, faam_strerror(st));
        goto done;
    }

    if (g_verbose > 0) printf("Tags updated successfully on %s\n", filepath);
    ret = 0;

done:
    tag_free(&t);
    faam_demuxer_close(&d);
    fclose(f);
    return ret;
}

/* Chapter file format: one chapter per line, "HH:MM:SS.mmm<TAB>Title". No
 * duration field -- Nero's chpl atom (chapter.c) doesn't store one
 * either; a chapter's extent is implicitly "until the next chapter starts". */
static int cmd_chapter_import(const char *filepath, const char *chapters_path)
{
    FILE *cf = strcmp(chapters_path, "-") == 0 ? stdin : cli_fopen(chapters_path, "r");
    if (!cf) {
        fprintf(stderr, "Error opening chapter file %s\n", chapters_path);
        return 1;
    }

    faam_chapter chapters[FAAM_CLI_MAX_CHAPTERS];
    char titles[FAAM_CLI_MAX_CHAPTERS][FAAM_CLI_TITLE_MAX];
    uint32_t count = 0;
    char line[512];
    int lineno = 0;
    bool bad = false;

    while (fgets(line, sizeof(line), cf)) {
        lineno++;
        if (line[0] == '\0' || line[0] == '\n') continue;

        if (count >= FAAM_CLI_MAX_CHAPTERS) {
            fprintf(stderr, "%s:%d: too many chapters (max %d)\n", chapters_path, lineno, FAAM_CLI_MAX_CHAPTERS);
            bad = true;
            break;
        }
        if (!cli_parse_chapter_line(line, &chapters[count].start_ms,
                                    titles[count], sizeof(titles[count]))) {
            fprintf(stderr, "%s:%d: malformed chapter line (expected HH:MM:SS.mmm<TAB>Title)\n", chapters_path, lineno);
            bad = true;
            break;
        }
        chapters[count].struct_size = sizeof(chapters[count]);
        chapters[count].title = titles[count];
        count++;
    }
    if (cf != stdin) fclose(cf);

    if (bad) return 1;
    if (count == 0) {
        fprintf(stderr, "Error: %s contains no chapters\n", chapters_path);
        return 1;
    }

    FILE *f = cli_fopen(filepath, "r+b");
    if (!f) {
        fprintf(stderr, "Error opening %s\n", filepath);
        return 1;
    }

    faam_io io = cli_faam_io(f);
    faam_status st = faam_update_chapters_stream(&io, chapters, count, 0);
    fclose(f);

    if (st != FAAM_OK) {
        fprintf(stderr, "Error updating chapters on %s: %s\n", filepath, faam_strerror(st));
        return 1;
    }

    if (g_verbose > 0) printf("Successfully imported %u chapters into %s\n", count, filepath);
    return 0;
}

static int cmd_chapter_export(const char *filepath, const char *output_path, bool overwrite)
{
    FILE *f = cli_open_seekable_input(filepath);
    if (!f) {
        fprintf(stderr, "Error opening %s\n", filepath);
        return 1;
    }

    faam_io io = cli_faam_io(f);

    faam_demuxer *d = NULL;
    faam_status st = faam_demuxer_open(NULL, &io, &d);
    if (st != FAAM_OK) {
        fprintf(stderr, "Error parsing %s: %s\n", filepath, faam_strerror(st));
        fclose(f);
        return 1;
    }

    uint32_t count = 0;
    int ret = 1;
    FILE *of = NULL;
    faam_demuxer_get_num_chapters(d, &count);

    /* An empty list written with exit 0 would look like a successful export. */
    if (count == 0) {
        fprintf(stderr, "No chapters in %s\n", filepath);
        goto done;
    }
    bool to_stdout = strcmp(output_path, "-") == 0;
    if (strcmp(filepath, "-") != 0 && !to_stdout && cli_same_file(filepath, output_path)) {
        fprintf(stderr, "Error: the output %s is the same file as the input %s\n", output_path, filepath);
        goto done;
    }
    if (!to_stdout && output_refused(output_path, overwrite)) goto done;

    /* Chapter titles are borrowed from the demuxer; keep it open while writing. */
    of = to_stdout ? stdout : cli_fopen(output_path, "w");
    if (!of) {
        fprintf(stderr, "Error creating %s\n", output_path);
        goto done;
    }

    bool write_failed = false;
    for (uint32_t i = 0; i < count; i++) {
        faam_chapter ch;
        memset(&ch, 0, sizeof(ch));
        ch.struct_size = sizeof(ch);
        if (faam_demuxer_get_chapter(d, i, &ch) != FAAM_OK) { write_failed = true; break; }
        uint64_t ms = ch.start_ms;
        unsigned hh = (unsigned)(ms / 3600000);
        unsigned mm = (unsigned)((ms / 60000) % 60);
        unsigned ss = (unsigned)((ms / 1000) % 60);
        unsigned mmm = (unsigned)(ms % 1000);
        write_failed |= fprintf(of, "%02u:%02u:%02u.%03u\t%s\n", hh, mm, ss, mmm,
                                ch.title ? ch.title : "") < 0;
    }
    write_failed |= !cli_finish_output(&of);
    if (write_failed) {
        fprintf(stderr, "Error writing %s\n", output_path);
        if (!to_stdout) cli_remove(output_path);
        goto done;
    }

    if (g_verbose > 0)
        fprintf(to_stdout ? stderr : stdout, "Successfully exported %u chapters from %s to %s\n", count, filepath, output_path);
    ret = 0;

done:
    faam_demuxer_close(&d);
    fclose(f);
    return ret;
}

static int cmd_chapter(int argc, char **argv)
{
    /* main() dispatches as cmd_chapter(argc-1, argv+1), so argv[0] here is
     * literally "chapter" (mirroring how cmd_info/cmd_tag/etc. get their own
     * name in argv[0]); the real subcommand is argv[1]. */
    if (argc < 2) {
        print_usage(HELP_CHAPTER);
        return 1;
    }

    const char *subcmd = argv[1];
    if (!strcmp(subcmd, "-h") || !strcmp(subcmd, "--help")) { print_usage(HELP_CHAPTER); return 0; }
    if (!strcmp(subcmd, "-H")) { print_usage('H'); return 0; }
    int sub_argc = argc - 1;
    char **sub_argv = argv + 1; /* sub_argv[0] == subcmd, skipped by getopt like a program name */

    const char *filepath = NULL;
    const char *chapters_path = NULL;
    const char *output_path = NULL;

    static struct option long_options[] = {
        {"chapters", required_argument, 0, OPT_CHAPTERS},
        {"output", required_argument, 0, 'o'},
        {"overwrite", no_argument, 0, OPT_OVERWRITE},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    bool overwrite = false;
    int opt;
    optind = 1;
    while ((opt = getopt_long(sub_argc, sub_argv, "v:o:hH", long_options, NULL)) != -1) {
        switch (opt) {
        case 'v': if (!set_verbosity(optarg)) return 1; break;
        case OPT_CHAPTERS: chapters_path = optarg; break;
        case 'o': output_path = optarg; break;
        case OPT_OVERWRITE: overwrite = true; break;
        case 'h': print_usage(HELP_CHAPTER); return 0;
        case 'H': print_usage('H'); return 0;
        default: print_usage('h'); return 1;
        }
    }
    if (optind < sub_argc) filepath = sub_argv[optind];

    if (!strcmp(subcmd, "import")) {
        if (!filepath || !chapters_path) {
            print_usage(HELP_CHAPTER);
            return 1;
        }
        return cmd_chapter_import(filepath, chapters_path);
    } else if (!strcmp(subcmd, "export")) {
        if (!filepath || !output_path) {
            print_usage(HELP_CHAPTER);
            return 1;
        }
        return cmd_chapter_export(filepath, output_path, overwrite);
    }

    fprintf(stderr, "Unknown chapter subcommand: %s\n", subcmd);
    print_usage(HELP_CHAPTER);
    return 1;
}

int main(int argc, char **argv)
{
    cli_init_console();

    faam_library_info libinfo = { .struct_size = sizeof(libinfo) };
    if (faam_get_library_info(&libinfo) != FAAM_OK) {
        fprintf(stderr, "Wrong libfaam version!\n");
        return 1;
    }
    g_lib_version = libinfo.version;
#ifdef _WIN32
    int wargc = 0;
    wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    char **allocated_argv = NULL;
    if (wargv && wargc > 0) {
        allocated_argv = (char **)calloc((size_t)wargc, sizeof(char *));
        if (allocated_argv) {
            for (int i = 0; i < wargc; i++)
                allocated_argv[i] = win32_utf16_to_utf8(wargv[i]);
            argv = allocated_argv;
            argc = wargc;
        }
    }
#endif

    if (argc < 2) {
        print_usage('?');
        return 1;
    }

    int ret = 0;
    int help_mode = 0;
    const char *cmd = argv[1];
    for (int g = 0; g_help[g].id; g++)
        if (strcmp(cmd, g_help[g].option) == 0) help_mode = g_help[g].id;
    if (strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) help_mode = 'h';
    if (strcmp(cmd, "-H") == 0) help_mode = 'H';

    if (help_mode) {
        print_usage(help_mode);
        ret = 0;
    } else if (strcmp(cmd, "--license") == 0) {
        faam_library_info info = { .struct_size = sizeof(info) };
        fprintf(stderr, "FAAM - Freeware Advanced Audio Muxer (https://freewareadvancedaudio.github.io)\n");
        if (faam_get_library_info(&info) == FAAM_OK && info.copyright)
            fprintf(stderr, "%s\n", info.copyright);
        cli_print_patent_notice(stderr);
        cli_print_lgpl_notice(stderr, "library");
        ret = 0;
    } else if (strcmp(cmd, "info") == 0) {
        ret = cmd_info(argc - 1, argv + 1);
    } else if (strcmp(cmd, "dump") == 0) {
        ret = cmd_dump(argc - 1, argv + 1);
    } else if (strcmp(cmd, "demux") == 0) {
        ret = cmd_demux(argc - 1, argv + 1);
    } else if (strcmp(cmd, "tag") == 0) {
        ret = cmd_tag(argc - 1, argv + 1);
    } else if (strcmp(cmd, "chapter") == 0) {
        ret = cmd_chapter(argc - 1, argv + 1);
    } else if (cmd[0] == '-') {
        /* Verb-less mux invocation (faam -i a -i b -o out.mp4): there is no
         * verb word occupying argv[1], so argv[1] is already the first real
         * flag. cmd_mux()/getopt_long expect argv[0] of their sub-array to
         * be a skippable "program name" slot (mirroring how "mux" filled
         * that role in the old faam mux <input> -o <out> form) -- passing
         * argv+1 directly here would make getopt_long silently skip and
         * drop this first flag. Splice in a placeholder argv[0] instead. */
        char **mux_argv = (char **)malloc(sizeof(char *) * (size_t)argc);
        if (!mux_argv) {
            fprintf(stderr, "Error: out of memory\n");
            ret = 1;
        } else {
            mux_argv[0] = "mux";
            for (int i = 1; i < argc; i++) mux_argv[i] = argv[i];
            ret = cmd_mux(argc, mux_argv);
            free(mux_argv);
        }
    } else {
        fprintf(stderr, "Unknown subcommand: %s\n", cmd);
        print_usage('?');
        ret = 1;
    }

#ifdef _WIN32
    if (allocated_argv) {
        for (int i = 0; i < argc; i++) {
            if (allocated_argv[i]) free(allocated_argv[i]);
        }
        free(allocated_argv);
    }
#endif

    return ret;
}
