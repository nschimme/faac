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

#ifdef _WIN32
#include <windows.h>
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
#include "cli_io.h"
#include "charset.h"
#include "help.h"
#include "endian.h"
#include "asc_codec.h"

#define HAS(s) ((s) && (s)[0])

enum {
    HELP_COMMANDS = 1,
    HELP_MUX,
    HELP_INSPECT,
    HELP_TAG,
    HELP_CHAPTER
};

static const help_t help_commands[] = {
    {"-i <file> ... -o <out>",
     "Mux elementary streams into an MP4 (no subcommand word)", NULL},
    {"info <file>",
     "Print container, track, tag, chapter and gapless summaries", NULL},
    {"dump <file>",
     "Print the MP4 atom tree", NULL},
    {"demux <file>",
     "Extract one track's elementary stream", NULL},
    {"tag <file>",
     "Edit iTunes metadata tags in place", NULL},
    {"chapter import|export <file>",
     "Edit or list chapters of an audiobook", NULL},
    {NULL, NULL, NULL}
};

static const help_t help_mux[] = {
    {"-i, --input <file>",
     "Elementary stream to mux: ADTS AAC, or Annex-B H.264 or H.265 (video needs a build with -Dmuxer-video). "
     "Video is muxed one picture per sample in stream order, and B-frames are not supported: no composition "
     "time offsets are written, so reordered pictures would play out of order. "
     "Repeat for more tracks (max 8)", NULL},
    {"-o, --output <file>",
     "Output MP4 file (required)", NULL},
    {"--overwrite",
     "Overwrite an existing output file", NULL},
    {"--codec:N <codec>",
     "Codec of the Nth -i, counting from 0: aac, h264 or h265 (default: from the file extension, "
     ".264/.h264 or .265/.hevc, else aac)", NULL},
    {"--brand <name>",
     "m4a (default) or m4b, the audiobook brand", NULL},
    {"--width <pixels>",
     "Video width; required for video, the stream is not parsed for it", NULL},
    {"--height <pixels>",
     "Video height; required for video", NULL},
    {"--rotation <degrees>",
     "Display rotation of video tracks, clockwise: 0 (default), 90, 180 or 270", NULL},
    {"--fps <n[/d]>",
     "Video frame rate, a whole number or a fraction such as 30000/1001 (default: 30)", NULL},
    {"--encoder-delay <samples>",
     "Leading priming samples, for gapless playback of audio (needs an audio track); counted in the "
     "track's sample rate, which for HE-AAC is the core rate, as faam info prints it for a faac file", NULL},
    {"--padding-delay <samples>",
     "Trailing padding samples in the same units as --encoder-delay; the total length is derived from the "
     "frames muxed", NULL},
    {"--sbr-signaling <mode>",
     "How the AAC track announces SBR/PS, which ADTS cannot say: none (default, decoder detects), "
     "compatible, explicit, ps or ps-explicit (HE-AAC v2, which needs a mono core)", NULL},
    {"--language <code3>",
     "Set ISO 639-2/T 3-letter language code of audio tracks (e.g. eng, ger); default und. "
     "--lang is the same", NULL},
    {"--creation-time <value>",
     "Set creation/modification time (auto, now, or timestamp); without it SOURCE_DATE_EPOCH is used "
     "when set, else the time is left unset", NULL},
    {NULL,
     "Every option of --help-tag except --remove and --clear also applies when muxing.", NULL},
    {NULL, NULL, NULL}
};

static const help_t help_inspect[] = {
    {"-o, --output <file>",
     "demux: output file (default: output.raw). A name containing .aac or .adts gets ADTS headers; "
     "video is written as Annex-B", NULL},
    {"-t, --track <id>",
     "demux: track ID to extract (default: the first track)", NULL},
    {"--export-asc <file>",
     "demux: also write the track's codec configuration (AudioSpecificConfig, avcC or hvcC)", NULL},
    {"--overwrite",
     "demux: overwrite an existing output or --export-asc file", NULL},
    {NULL, NULL, NULL}
};

static const help_t help_tag[] = {
    {"--title <text>", "Title", NULL},
    {"--titlesort <text>", "Title sort name", NULL},
    {"--artist <text>", "Artist", NULL},
    {"--album <text>", "Album", NULL},
    {"--albumartist <text>", "Album artist", NULL},
    {"--composer <text>", "Composer", NULL},
    {"--artistsort <text>", "Artist sort name", NULL},
    {"--albumsort <text>", "Album sort name", NULL},
    {"--albumartistsort <text>", "Album artist sort name", NULL},
    {"--composersort <text>", "Composer sort name", NULL},
    {"--year <text>", "Year", NULL},
    {"--comment <text>", "Comment", NULL},
    {"--genre <value>",
     "ID3v1 genre number (0-255) or name; anything else is stored as free text", NULL},
    {"--compilation", "Mark as part of a compilation", NULL},
    {"--track <n[/total]>", "Track number", NULL},
    {"--disc <n[/total]>", "Disc number", NULL},
    {"--cover-art <file>", "Cover image, GIF, JPEG or PNG", NULL},
    {"--tag <name=value>",
     "Custom tag, name and value separated by = or ,; repeat for more", NULL},
    {"--remove <field>",
     "tag only: remove a field before the other options apply: title, titlesort, artist, artistsort, "
     "album, albumsort, albumartist, albumartistsort, composer, composersort, year, comment, genre, "
     "compilation, track, disc, cover-art or custom. Repeatable", NULL},
    {"--clear",
     "tag only: remove every existing tag before the other options apply (drops all metadata, including preserved/unmodeled tags)", NULL},
    {NULL, NULL, NULL}
};

static const help_t help_chapter[] = {
    {"import <file>",
     "Replace the chapters of an MP4 with those of the --chapters file", NULL},
    {"export <file>",
     "Write the chapters of an MP4 to the -o file; a file without chapters is an error", NULL},
    {"--chapters <file>",
     "Chapter list to import: one \"HH:MM:SS.mmm<TAB>Title\" line per chapter (max 255); the fraction "
     "is 1 to 3 digits, .5 being 500 ms", NULL},
    {"-o, --output <file>",
     "Chapter list to write, in the same format", NULL},
    {"--overwrite",
     "export: overwrite an existing chapter list", NULL},
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

static void print_usage(int mode)
{
    faam_library_info info;
    info.struct_size = sizeof(info);
    if (faam_get_library_info(&info) != FAAM_OK)
        info.version = "1.0.0";

    char version_buf[128];
    show_help_usage("faam", mode,
                    cli_version_string(version_buf, sizeof(version_buf), info.version),
                    "faam -i <input> [-i <input>...] -o <out.mp4> [options]\n"
                    "       faam <info|dump|demux|tag|chapter> [options] <file>",
                    g_help);
}

enum {
    OPT_BRAND = 400,
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
    OPT_TAG_LAST = OPT_TAG
};

#define FAAM_CLI_TITLE_MAX 256 /* title buffer incl. NUL; longer titles are truncated */
#define FAAM_CLI_MAX_CHAPTERS 255 /* what the library stores in a chpl atom */
#define FAAM_TAG_MAX_REMOVE 32
#define FAAM_MUX_MAX_TRACKS 8 /* matches faam_muxer_config.tracks[8] in include/faam.h */
#define FAAM_VIDEO_TIMESCALE 90000

/* Strict numeric value: decimal digits only, at most max. */
static bool parse_uint(const char *s, unsigned long long max, unsigned long long *out)
{
    char *end = NULL;
    if (!s || *s < '0' || *s > '9') return false;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno != 0 || *end != '\0' || v > max) return false;
    *out = v;
    return true;
}

/* "n" or "n/d" frames per second. */
static bool parse_fps(const char *s, uint32_t *num, uint32_t *den)
{
    char buf[64];
    char *slash;
    unsigned long long n, d = 1;

    if (strlen(s) >= sizeof(buf)) return false;
    strcpy(buf, s);
    slash = strchr(buf, '/');
    if (slash) *slash++ = '\0';
    if (!parse_uint(buf, 1000000, &n) || n == 0) return false;
    if (slash && (!parse_uint(slash, 1000000, &d) || d == 0)) return false;
    *num = (uint32_t)n;
    *den = (uint32_t)d;
    return true;
}

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

/* Owned UTF-8 copies of CLI strings; faam_metadata only borrows them, so they
 * must outlive the muxer or faam_update_tags_stream. argv text arrives in
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
    faam_metadata meta;
    owned_list owned;
    uint8_t *cover;
    faam_custom_tag *tags; /* meta.custom_tags; grows as tags are added, none are dropped */
    uint32_t tags_cap;
    bool given; /* some tag option was applied */
} tag_ctx;

static void tag_free(tag_ctx *t)
{
    free(t->cover);
    free(t->tags);
    owned_free(&t->owned);
    t->cover = NULL;
    t->tags = NULL;
}

/* Room for one more custom tag after the num_custom_tags already in t->tags. */
static bool tag_reserve(tag_ctx *t)
{
    if (t->meta.num_custom_tags < t->tags_cap) return true;
    uint32_t cap = t->tags_cap ? t->tags_cap * 2 : 8;
    faam_custom_tag *grown = (faam_custom_tag *)realloc(t->tags, cap * sizeof(*grown));
    if (!grown) return false;
    t->tags = grown;
    t->tags_cap = cap;
    t->meta.custom_tags = grown;
    return true;
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
    {"tag", required_argument, 0, OPT_TAG}

static const char **tag_text_field(tag_ctx *t, int opt)
{
    switch (opt) {
    case OPT_TITLE: return &t->meta.title;
    case OPT_TITLE_SORT: return &t->meta.title_sort;
    case OPT_ARTIST: return &t->meta.artist;
    case OPT_ARTIST_SORT: return &t->meta.artist_sort;
    case OPT_ALBUM: return &t->meta.album;
    case OPT_ALBUM_SORT: return &t->meta.album_sort;
    case OPT_ALBUM_ARTIST: return &t->meta.album_artist;
    case OPT_ALBUM_ARTIST_SORT: return &t->meta.album_artist_sort;
    case OPT_COMPOSER: return &t->meta.composer;
    case OPT_COMPOSER_SORT: return &t->meta.composer_sort;
    case OPT_YEAR: return &t->meta.year;
    case OPT_COMMENT: return &t->meta.comment;
    default: return NULL;
    }
}

/* Applies one tag option on top of what t already holds. Returns NULL, or the
 * message to print (newline-terminated; those shared with faac read the same). */
static const char *tag_apply(tag_ctx *t, int opt, char *arg)
{
    faam_metadata *meta = &t->meta;
    const char **text = tag_text_field(t, opt);

    t->given = true;
    if (text) {
        const char *v = own_utf8(&t->owned, trim_quotes_and_spaces(arg));
        if (!v) return "Out of memory.\n";
        *text = v;
        return NULL;
    }

    switch (opt) {
    case OPT_COMPILATION:
        meta->compilation = true;
        return NULL;
    case OPT_GENRE: {
        /* Same parsing as faac: a name or number picks the ID3v1 genre and its text;
           anything else is free text with no numeric atom, which also replaces a
           stale numeric genre when updating a file. */
        uint16_t code = 0;
        const char *name = NULL;
        if (!parse_genre(trim_quotes_and_spaces(arg), &code, &name))
            return "Genre number out of range.\n";
        meta->genre_code = code;
        meta->genre_str = name ? own_utf8(&t->owned, name) : NULL;
        return NULL;
    }
    case OPT_TRACK:
        return parse_index_arg(arg, &meta->track_num, &meta->track_total) ? NULL : "Wrong track number.\n";
    case OPT_DISC:
        return parse_index_arg(arg, &meta->disc_num, &meta->disc_total) ? NULL : "Wrong disc number.\n";
    case OPT_COVER_ART: {
        uint8_t *art = NULL;
        uint64_t size = 0;
        const char *err = load_cover_art(arg, &art, &size);
        if (err) return err;
        free(t->cover);
        t->cover = art;
        meta->cover_art = art;
        meta->cover_bytes = (uint32_t)size;
        meta->cover_type = FAAM_COVER_AUTO;
        return NULL;
    }
    case OPT_TAG: {
        char *name = NULL, *value = NULL;
        const char *err = parse_tag_arg(arg, &name, &value);
        if (err) return err;
        if (!tag_reserve(t)) return "Couldn't add tag (out of memory).\n";
        /* The name lands in the same atom as the value, so both get the same treatment. */
        const char *n = own_utf8(&t->owned, name);
        const char *v = own_utf8(&t->owned, value);
        if (!n || !v) return "Couldn't add tag (out of memory).\n";
        t->tags[meta->num_custom_tags].struct_size = sizeof(faam_custom_tag);
        t->tags[meta->num_custom_tags].mean = NULL;
        t->tags[meta->num_custom_tags].name = n;
        t->tags[meta->num_custom_tags].value = v;
        meta->num_custom_tags++;
        return NULL;
    }
    default:
        return "Error: unknown tag option.\n";
    }
}

/* --remove field names: the small, CLI-facing subset that maps to a single
 * scalar/pointer field. Sort tags and multi-part fields (track/disc) are
 * addressable too since they're just as easy to zero individually. */
static bool remove_metadata_field(faam_metadata *meta, const char *name)
{
    if (!strcmp(name, "title")) meta->title = NULL;
    else if (!strcmp(name, "titlesort")) meta->title_sort = NULL;
    else if (!strcmp(name, "artistsort")) meta->artist_sort = NULL;
    else if (!strcmp(name, "artist")) meta->artist = NULL;
    else if (!strcmp(name, "albumsort")) meta->album_sort = NULL;
    else if (!strcmp(name, "albumartistsort")) meta->album_artist_sort = NULL;
    else if (!strcmp(name, "albumartist")) meta->album_artist = NULL;
    else if (!strcmp(name, "album")) meta->album = NULL;
    else if (!strcmp(name, "composersort")) meta->composer_sort = NULL;
    else if (!strcmp(name, "composer")) meta->composer = NULL;
    else if (!strcmp(name, "year")) meta->year = NULL;
    else if (!strcmp(name, "comment")) meta->comment = NULL;
    else if (!strcmp(name, "genre")) { meta->genre_str = NULL; meta->genre_code = 0; }
    else if (!strcmp(name, "compilation")) meta->compilation = false;
    else if (!strcmp(name, "track")) { meta->track_num = 0; meta->track_total = 0; }
    else if (!strcmp(name, "disc")) { meta->disc_num = 0; meta->disc_total = 0; }
    else if (!strcmp(name, "cover-art")) { meta->cover_art = NULL; meta->cover_bytes = 0; }
    else if (!strcmp(name, "custom")) meta->num_custom_tags = 0;
    else return false;
    return true;
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
    while ((opt = getopt_long(argc, argv, "hH", long_options, NULL)) != -1) {
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

    FILE *f = cli_fopen(filepath, "rb");
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
        printf("  Total Frames: %u\n", ti.total_frames);
    }

    faam_metadata meta;
    memset(&meta, 0, sizeof(meta));
    meta.struct_size = sizeof(meta);
    if (faam_demuxer_get_metadata(d, &meta) == FAAM_OK) {
        bool has_any = HAS(meta.title) || HAS(meta.title_sort) || HAS(meta.artist) || HAS(meta.artist_sort) ||
                       HAS(meta.album) || HAS(meta.album_sort) || HAS(meta.album_artist) || HAS(meta.album_artist_sort) ||
                       HAS(meta.composer) || HAS(meta.composer_sort) || HAS(meta.year) || HAS(meta.comment) ||
                       meta.genre_code || HAS(meta.genre_str) || meta.compilation || meta.track_num ||
                       meta.disc_num || meta.cover_bytes > 0 || meta.num_custom_tags > 0;
        if (has_any) {
            printf("\nMetadata Tags:\n");
            if (HAS(meta.title)) printf("  Title: %s\n", meta.title);
            if (HAS(meta.title_sort)) printf("  Title Sort: %s\n", meta.title_sort);
            if (HAS(meta.artist)) printf("  Artist: %s\n", meta.artist);
            if (HAS(meta.artist_sort)) printf("  Artist Sort: %s\n", meta.artist_sort);
            if (HAS(meta.album)) printf("  Album: %s\n", meta.album);
            if (HAS(meta.album_sort)) printf("  Album Sort: %s\n", meta.album_sort);
            if (HAS(meta.album_artist)) printf("  Album Artist: %s\n", meta.album_artist);
            if (HAS(meta.album_artist_sort)) printf("  Album Artist Sort: %s\n", meta.album_artist_sort);
            if (HAS(meta.composer)) printf("  Composer: %s\n", meta.composer);
            if (HAS(meta.composer_sort)) printf("  Composer Sort: %s\n", meta.composer_sort);
            if (HAS(meta.genre_str)) printf("  Genre: %s\n", meta.genre_str);
            else if (meta.genre_code) printf("  Genre: #%u\n", meta.genre_code - 1);
            if (HAS(meta.year)) printf("  Year: %s\n", meta.year);
            if (HAS(meta.comment)) printf("  Comment: %s\n", meta.comment);
            if (meta.compilation) printf("  Compilation: Yes\n");
            if (meta.track_num) printf("  Track: %u/%u\n", meta.track_num, meta.track_total);
            if (meta.disc_num) printf("  Disc: %u/%u\n", meta.disc_num, meta.disc_total);
            if (meta.cover_bytes > 0) printf("  Cover Art: present (%u bytes)\n", meta.cover_bytes);
            if (meta.num_custom_tags > 0) {
                printf("  Custom Tags (%u):\n", meta.num_custom_tags);
                for (uint32_t i = 0; i < meta.num_custom_tags; i++) {
                    faam_custom_tag tag;
                    memset(&tag, 0, sizeof(tag));
                    tag.struct_size = sizeof(tag);
                    if (faam_demuxer_get_custom_tag(d, i, &tag) == FAAM_OK)
                        printf("    %s = %s\n", tag.name, tag.value);
                }
            }
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
    while ((opt = getopt_long(argc, argv, "hH", long_options, NULL)) != -1) {
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

    FILE *f = cli_fopen(filepath, "rb");
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

    printf("Dumping MP4 Atom Tree for: %s\n", filepath);

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
} adts_info;

/* Validates the first ADTS header of f and leaves the position unspecified.
 * Returns NULL, or what is wrong with the input. */
static const char *adts_probe(FILE *f, adts_info *out)
{
    uint8_t h[10];
    uint64_t start = 0;
    size_t got;

    if (!cli_fseek(f, 0)) return "cannot seek";
    got = fread(h, 1, sizeof(h), f);
    if (got == 0) return "empty input";
    if (got == sizeof(h) && !memcmp(h, "ID3", 3)) {
        /* An ID3v2 tag some players prepend to ADTS files: synchsafe size, optional footer. */
        start = 10 + (((uint64_t)(h[6] & 0x7F) << 21) | ((uint64_t)(h[7] & 0x7F) << 14) |
                      ((uint64_t)(h[8] & 0x7F) << 7) | (h[9] & 0x7F)) + ((h[5] & 0x10) ? 10 : 0);
        if (!cli_fseek(f, start)) return "not an ADTS stream";
        got = fread(h, 1, 7, f);
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

/* Muxes every ADTS frame of f from its current position. Bytes that are not a frame
 * header are skipped, so a damaged stream loses the damage and keeps the rest. A header that
 * disagrees with the stream's first one in profile, rate or channels is as likely to be payload
 * that looks like a sync word as a frame of something else, and is skipped the same way. A
 * matching header that announces more than one raw data block is a frame this tool cannot mux
 * as one sample, so it ends the run: *multi_block is set. */
static faam_status mux_adts_frames(faam_muxer *m, uint32_t track_id, FILE *f, const adts_params *first,
                                   uint32_t *frames, bool *multi_block)
{
    uint8_t buf[65536];
    size_t buf_len = 0;

    for (;;) {
        size_t n = fread(buf + buf_len, 1, sizeof(buf) - buf_len, f);
        buf_len += n;

        size_t off = 0;
        while (off + 7 <= buf_len) {
            const uint8_t *h = buf + off;
            adts_params p;
            if (!adts_header_ok(h)) { off++; continue; }
            adts_parse_header(h, &p);
            if (p.profile != first->profile || p.sr_idx != first->sr_idx || p.channel_cfg != first->channel_cfg) {
                off++;
                continue;
            }
            uint32_t len = adts_frame_length(h);
            uint32_t hlen = adts_header_length(h);
            if (len < hlen) { off++; continue; }
            if (p.raw_blocks != 0) { *multi_block = true; return FAAM_OK; }
            if (off + len > buf_len) break;
            faam_status st = faam_muxer_write_frame(m, track_id, h + hlen, len - hlen, 1024, 0, FAAM_FRAME_KEYFRAME);
            if (st != FAAM_OK) return st;
            (*frames)++;
            off += len;
        }

        memmove(buf, buf + off, buf_len - off);
        buf_len -= off;
        if (ferror(f)) return FAAM_ERR_IO_READ;
        if (n == 0) break;
    }
    return FAAM_OK;
}

/* One video access unit; durations follow the frame rate num/den exactly by
 * rounding the running timestamp, not each interval. */
static faam_status mux_video_frame(faam_muxer *m, uint32_t track_id, const uint8_t *data, uint32_t len,
                                   bool key, uint32_t *frames, uint32_t fps_num, uint32_t fps_den)
{
    uint64_t k = *frames;
    uint64_t from = (k * FAAM_VIDEO_TIMESCALE * fps_den + fps_num / 2) / fps_num;
    uint64_t to = ((k + 1) * FAAM_VIDEO_TIMESCALE * fps_den + fps_num / 2) / fps_num;
    faam_status st = faam_muxer_write_frame(m, track_id, data, len, (uint32_t)(to - from), 0, key ? FAAM_FRAME_KEYFRAME : 0);
    if (st == FAAM_OK) (*frames)++;
    return st;
}

/* annexb_split() hands over one access unit at a time; the status of the write is kept for the
 * caller, which the split itself only learns as "stopped". */
typedef struct {
    faam_muxer *m;
    uint32_t track_id;
    uint32_t *frames;
    uint32_t fps_num;
    uint32_t fps_den;
    faam_status st;
} video_sink;

static bool video_sample(void *user, const uint8_t *sample, uint32_t len, bool key)
{
    video_sink *v = (video_sink *)user;
    v->st = mux_video_frame(v->m, v->track_id, sample, len, key, v->frames, v->fps_num, v->fps_den);
    return v->st == FAAM_OK;
}

static int cmd_mux(int argc, char **argv)
{
    const char *output_file = NULL;
    bool is_m4b = false;
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
    faam_muxer *m = NULL;
    faam_muxer_config cfg;
    faam_track_config tc[FAAM_MUX_MAX_TRACKS];
    faam_io io;
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
     * below, instead of reading the file from disk a second time. Freed
     * as each track's packetizer pass finishes (or on early error exit). */
    uint8_t *vbuf_store[FAAM_MUX_MAX_TRACKS] = {0};
    size_t vbuf_len_store[FAAM_MUX_MAX_TRACKS] = {0};

    memset(&tags, 0, sizeof(tags));
    tags.meta.struct_size = sizeof(tags.meta);

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
    while (!fail && (opt = getopt_long(fargc, filtered, "i:o:hH", long_options, NULL)) != -1) {
        unsigned long long v;
        switch (opt) {
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
        case OPT_BRAND:
            if (!strcasecmp(optarg, "m4b")) is_m4b = true;
            else if (strcasecmp(optarg, "m4a")) fail = bad_value(errbuf, sizeof(errbuf), "--brand", optarg, "use m4a or m4b");
            break;
        case OPT_WIDTH:
            if (!parse_uint(optarg, 65535, &v) || v == 0) fail = bad_value(errbuf, sizeof(errbuf), "--width", optarg, "1-65535 pixels");
            else width = (uint32_t)v;
            break;
        case OPT_HEIGHT:
            if (!parse_uint(optarg, 65535, &v) || v == 0) fail = bad_value(errbuf, sizeof(errbuf), "--height", optarg, "1-65535 pixels");
            else height = (uint32_t)v;
            break;
        case OPT_ROTATION:
            if (!parse_uint(optarg, 270, &v) || v % 90) fail = bad_value(errbuf, sizeof(errbuf), "--rotation", optarg, "0, 90, 180 or 270 degrees clockwise");
            else rotation = (uint32_t)v;
            break;
        case OPT_FPS:
            if (!parse_fps(optarg, &fps_num, &fps_den)) fail = bad_value(errbuf, sizeof(errbuf), "--fps", optarg, "n or n/d, up to 1000000");
            break;
        case OPT_ENCODER_DELAY:
            if (!parse_uint(optarg, UINT32_MAX, &v)) fail = bad_value(errbuf, sizeof(errbuf), "--encoder-delay", optarg, "a sample count");
            else { delay = (uint32_t)v; gapless_given = true; }
            break;
        case OPT_PADDING_DELAY:
            if (!parse_uint(optarg, UINT32_MAX, &v)) fail = bad_value(errbuf, sizeof(errbuf), "--padding-delay", optarg, "a sample count");
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
        if (cli_same_file(input_files[i], output_file)) {
            fprintf(stderr, "Error: the output %s is the same file as the input %s\n", output_file, input_files[i]);
            goto done;
        }
    }

    for (int i = 0; i < num_inputs; i++) {
        fin[i] = cli_fopen(input_files[i], "rb");
        if (!fin[i]) {
            fprintf(stderr, "Error opening %s\n", input_files[i]);
            goto done;
        }
    }

    faam_muxer_config_init(&cfg, sizeof(cfg));
    if (is_m4b) cfg.flags |= FAAM_MUXER_M4B;
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
    if (time_warn[0]) fprintf(stderr, "Warning: %s", time_warn);

    if (tags.given) cfg.metadata = &tags.meta;

    cfg.tracks = tc;
    cfg.num_tracks = (uint32_t)num_inputs;

    if (output_refused(output_file, overwrite)) goto done;
    fout = cli_fopen(output_file, "wb");
    if (!fout) {
        fprintf(stderr, "Error creating %s\n", output_file);
        goto done;
    }
    out_created = true;

    io = cli_faam_io(fout);

    st = faam_muxer_open(&cfg, &io, &m);
    if (st != FAAM_OK) {
        fprintf(stderr, "Error initializing muxer: %s\n", faam_strerror(st));
        goto done;
    }
    for (int i = 0; i < num_inputs; i++) faam_muxer_get_track_id(m, (uint32_t)i, &track_id[i]);

    for (int i = 0; i < num_inputs; i++) {
        if (tc[i].track_type == FAAM_TRACK_AUDIO) {
            if (!cli_fseek(fin[i], adts[i].start)) {
                fprintf(stderr, "Error reading %s\n", input_files[i]);
                goto done;
            }
            bool multi_block = false;
            st = mux_adts_frames(m, track_id[i], fin[i], &adts[i].params, &frames[i], &multi_block);
            if (multi_block) {
                fprintf(stderr, "Error: %s: a frame has more than one raw data block, which is not supported "
                        "(each frame is muxed as one 1024-sample unit)\n", input_files[i]);
                goto done;
            }
        } else {
            video_sink sink = { m, track_id[i], &frames[i], fps_num, fps_den, FAAM_OK };
            annexb_status as = annexb_split(tc[i].codec_id == FAAM_CODEC_H265 ? ANNEXB_H265 : ANNEXB_H264,
                                            vbuf_store[i], vbuf_len_store[i], video_sample, &sink);
            free(vbuf_store[i]);
            vbuf_store[i] = NULL;
            if (as == ANNEXB_NO_MEMORY) {
                fprintf(stderr, "Error: out of memory\n");
                goto done;
            }
            st = sink.st;
        }

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
    if (fclose(fout) != 0) {
        fout = NULL;
        fprintf(stderr, "Error writing %s\n", output_file);
        goto done;
    }
    fout = NULL;

    printf("Successfully muxed %d input(s) -> %s\n", num_inputs, output_file);
    ret = 0;

done:
    if (m) faam_muxer_close(&m);
    for (int i = 0; i < FAAM_MUX_MAX_TRACKS; i++) {
        if (fin[i]) fclose(fin[i]);
        free(vbuf_store[i]);
    }
    if (fout) fclose(fout);
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

    static struct option long_options[] = {
        {"output", required_argument, 0, 'o'},
        {"track", required_argument, 0, 't'},
        {"export-asc", required_argument, 0, OPT_EXPORT_ASC},
        {"overwrite", no_argument, 0, OPT_OVERWRITE},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "o:t:hH", long_options, NULL)) != -1) {
        unsigned long long v;
        switch (opt) {
        case 'o': output_file = optarg; break;
        case 't':
            if (!parse_uint(optarg, UINT32_MAX, &v) || v == 0) {
                fprintf(stderr, "Error: invalid --track value \"%s\" (a track ID, 1 or more)\n", optarg);
                return 1;
            }
            selected_track_id = (uint32_t)v;
            break;
        case OPT_EXPORT_ASC: export_asc = optarg; break;
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

    FILE *fin = cli_fopen(input_file, "rb");
    if (!fin) {
        fprintf(stderr, "Error opening %s\n", input_file);
        return 1;
    }

    faam_io io_in = cli_faam_io(fin);

    int ret = 1;
    FILE *fout = NULL;
    bool out_created = false;
    bool asc_created = false;
    bool write_failed = false;
    faam_demuxer *d = NULL;
    uint32_t num_tracks = 0;
    faam_track_info ti;
    faam_status st;
    uint8_t *frame = (uint8_t *)malloc(65536);

    if (!frame) {
        fprintf(stderr, "Error: out of memory\n");
        fclose(fin);
        return 1;
    }

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

    /* Opening an output truncates it, which --overwrite allows: an output that is the input, or
     * each other, would destroy what is still to be read. Checked on its own, ahead of --overwrite. */
    if (cli_same_file(input_file, output_file) || (export_asc && cli_same_file(input_file, export_asc))) {
        fprintf(stderr, "Error: an output file is the same file as the input %s\n", input_file);
        goto done;
    }
    if (export_asc && (!strcmp(output_file, export_asc) || cli_same_file(output_file, export_asc))) {
        fprintf(stderr, "Error: -o and --export-asc name the same file\n");
        goto done;
    }

    /* Both files are checked before either is written, so a refusal leaves nothing behind. */
    if (output_refused(output_file, overwrite) || (export_asc && output_refused(export_asc, overwrite)))
        goto done;

    bool is_adts_out = (strstr(output_file, ".aac") != NULL || strstr(output_file, ".adts") != NULL);
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
        printf("Exported codec extradata (%u bytes) to %s\n", cdata_len, export_asc);
    }

    fout = cli_fopen(output_file, "wb");
    if (!fout) {
        fprintf(stderr, "Error opening output %s\n", output_file);
        goto done;
    }
    out_created = true;

    uint32_t frame_cap = 65536;
    uint32_t frame_bytes = 0;
    static const uint8_t annexb_sc[4] = { 0x00, 0x00, 0x00, 0x01 };

    faam_frame_loc loc;
    while (faam_demuxer_next_frame_loc(d, &loc) == FAAM_OK) {
        if (loc.track_id != ti.track_id) {
            /* Skip frames not belonging to the extracted track */
            faam_demuxer_read_frame(d, NULL, 0, &frame_bytes);
            continue;
        }

        /* A video IDR frame easily exceeds any fixed size; the demuxer reports the
         * size it needs and stays on the frame, so grow the buffer and read again. */
        st = faam_demuxer_read_frame(d, frame, frame_cap, &frame_bytes);
        if (st == FAAM_ERR_OUTPUT_TOO_SMALL) {
            uint8_t *bigger = (uint8_t *)realloc(frame, frame_bytes);
            if (!bigger) {
                fprintf(stderr, "Error: out of memory\n");
                goto done;
            }
            frame = bigger;
            frame_cap = frame_bytes;
            st = faam_demuxer_read_frame(d, frame, frame_cap, &frame_bytes);
        }
        if (st != FAAM_OK) {
            fprintf(stderr, "Error reading a frame of track %u from %s: %s\n", ti.track_id, input_file, faam_strerror(st));
            goto done;
        }
        if (frame_bytes == 0) continue;

        if (ti.track_type == FAAM_TRACK_VIDEO) {
            /* Convert 4-byte BE length prefixed NALUs to Annex-B startcodes (00 00 00 01) */
            uint32_t pos = 0;
            while (pos + 4 <= frame_bytes) {
                uint32_t nal_len = ((uint32_t)frame[pos] << 24) | ((uint32_t)frame[pos+1] << 16) | ((uint32_t)frame[pos+2] << 8) | (uint32_t)frame[pos+3];
                pos += 4;
                if (pos + nal_len <= frame_bytes) {
                    write_failed |= fwrite(annexb_sc, 1, 4, fout) != 4;
                    write_failed |= fwrite(frame + pos, 1, nal_len, fout) != nal_len;
                    pos += nal_len;
                } else break;
            }
        } else {
            if (is_adts_out && ti.codec_id == FAAM_CODEC_AAC) {
                if (frame_bytes > ADTS_MAX_FRAME - ADTS_HEADER_SIZE) {
                    fprintf(stderr, "Error: a frame of track %u is too large for an ADTS header\n", ti.track_id);
                    goto done;
                }
                uint8_t adts[ADTS_HEADER_SIZE];
                adts_write_header(&adts_p, frame_bytes, adts);
                write_failed |= fwrite(adts, 1, sizeof(adts), fout) != sizeof(adts);
            }
            write_failed |= fwrite(frame, 1, frame_bytes, fout) != frame_bytes;
        }
        if (write_failed) break;
    }

    if (fclose(fout) != 0) write_failed = true;
    fout = NULL;
    if (write_failed) {
        fprintf(stderr, "Error writing %s\n", output_file);
        goto done;
    }

    printf("Successfully demuxed %s -> %s\n", input_file, output_file);
    ret = 0;

done:
    if (fout) fclose(fout);
    if (ret != 0) {
        if (out_created) cli_remove(output_file);
        if (asc_created) cli_remove(export_asc);
    }
    faam_demuxer_close(&d);
    free(frame);
    fclose(fin);
    return ret;
}

static int cmd_tag(int argc, char **argv)
{
    const char *filepath = NULL;
    tag_ctx t;
    faam_metadata *meta = &t.meta;
    memset(&t, 0, sizeof(t));
    meta->struct_size = sizeof(*meta);

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
        faam_metadata scratch;
        int saved_opterr = opterr;
        opterr = 0;
        optind = 1;
        while ((opt = getopt_long(argc, argv, "hH", long_options, NULL)) != -1) {
            if (opt == 'h') { print_usage(HELP_TAG); return 0; }
            if (opt == 'H') { print_usage('H'); return 0; }
            if (opt == OPT_CLEAR) {
                want_clear = true;
            } else if (opt == OPT_REMOVE) {
                memset(&scratch, 0, sizeof(scratch));
                if (!remove_metadata_field(&scratch, optarg)) {
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
        /* The rewrite replaces the whole tag list, so what is kept is only as good as this read:
         * any failure here must stop, not carry on with an empty list. */
        uint32_t num_tracks = 0;
        faam_status rd;
        /* Kept open until the update: the metadata strings are borrowed from it. */
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
        rd = faam_demuxer_get_metadata(d, meta);
        if (rd != FAAM_OK) {
            fprintf(stderr, "Error reading the existing tags of %s: %s (nothing was changed)\n", filepath, faam_strerror(rd));
            goto done;
        }
        /* Copied out of the demuxer's array so added tags can extend it. */
        uint32_t count = meta->num_custom_tags;
        meta->num_custom_tags = 0;
        meta->custom_tags = NULL;
        for (uint32_t i = 0; i < count; i++) {
            if (!tag_reserve(&t)) {
                fprintf(stderr, "Couldn't keep the existing tags (out of memory).\n");
                goto done;
            }
            faam_custom_tag *slot = &t.tags[meta->num_custom_tags];
            memset(slot, 0, sizeof(*slot));
            slot->struct_size = sizeof(*slot);
            if (faam_demuxer_get_custom_tag(d, i, slot) != FAAM_OK) {
                fprintf(stderr, "Error reading the existing tags of %s (nothing was changed)\n", filepath);
                goto done;
            }
            meta->num_custom_tags++;
        }
    }

    for (int i = 0; i < num_remove; i++) remove_metadata_field(meta, remove_names[i]);

    /* Pass 2: apply every value-setting flag on top of the (possibly
     * existing, possibly cleared) base metadata. */
    const char *err_msg = NULL;
    int opt;
    optind = 1;
    bool bad_option = false;
    while ((opt = getopt_long(argc, argv, "hH", long_options, NULL)) != -1) {
        if (opt >= OPT_TAG_FIRST && opt <= OPT_TAG_LAST) {
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

    faam_status st = faam_update_tags_stream(&io, meta, want_clear ? FAAM_TAG_UPDATE_CLEAR : 0);
    if (st != FAAM_OK) {
        fprintf(stderr, "Error updating tags on %s: %s\n", filepath, faam_strerror(st));
        goto done;
    }

    printf("Tags updated successfully on %s\n", filepath);
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
static bool parse_chapter_line(const char *line, faam_chapter *out, char *title)
{
    unsigned hh, mm, ss_i;
    int consumed = 0;
    if (sscanf(line, "%u:%u:%u%n", &hh, &mm, &ss_i, &consumed) != 3) return false;
    if (mm > 59 || ss_i > 59) return false;

    /* The fraction is a decimal fraction of a second, one to three digits: ".5" is 500 ms. */
    const char *rest = line + consumed;
    if (*rest != '.') return false;
    rest++;
    unsigned ms = 0, digits = 0;
    while (rest[digits] >= '0' && rest[digits] <= '9') {
        if (digits == 3) return false;
        ms = ms * 10 + (unsigned)(rest[digits] - '0');
        digits++;
    }
    if (digits == 0) return false;
    for (unsigned i = digits; i < 3; i++) ms *= 10;
    rest += digits;

    if (*rest != '\t') return false;
    rest++;

    size_t len = strlen(rest);
    while (len > 0 && (rest[len - 1] == '\n' || rest[len - 1] == '\r')) len--;
    if (len >= FAAM_CLI_TITLE_MAX) len = FAAM_CLI_TITLE_MAX - 1;
    memcpy(title, rest, len);
    title[len] = '\0';
    out->start_ms = ((uint64_t)hh * 3600 + (uint64_t)mm * 60 + ss_i) * 1000 + ms;
    out->title = title;
    return true;
}

static int cmd_chapter_import(const char *filepath, const char *chapters_path)
{
    FILE *cf = cli_fopen(chapters_path, "r");
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
        if (!parse_chapter_line(line, &chapters[count], titles[count])) {
            fprintf(stderr, "%s:%d: malformed chapter line (expected HH:MM:SS.mmm<TAB>Title)\n", chapters_path, lineno);
            bad = true;
            break;
        }
        chapters[count].struct_size = sizeof(chapters[count]);
        count++;
    }
    fclose(cf);

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

    printf("Successfully imported %u chapters into %s\n", count, filepath);
    return 0;
}

static int cmd_chapter_export(const char *filepath, const char *output_path, bool overwrite)
{
    FILE *f = cli_fopen(filepath, "rb");
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
    if (cli_same_file(filepath, output_path)) {
        fprintf(stderr, "Error: the output %s is the same file as the input %s\n", output_path, filepath);
        goto done;
    }
    if (output_refused(output_path, overwrite)) goto done;

    /* Chapter titles are borrowed from the demuxer; keep it open while writing. */
    of = cli_fopen(output_path, "w");
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
    if (fclose(of) != 0) write_failed = true;
    of = NULL;
    if (write_failed) {
        fprintf(stderr, "Error writing %s\n", output_path);
        cli_remove(output_path);
        goto done;
    }

    printf("Successfully exported %u chapters from %s to %s\n", count, filepath, output_path);
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
    while ((opt = getopt_long(sub_argc, sub_argv, "o:hH", long_options, NULL)) != -1) {
        switch (opt) {
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
        print_usage('h');
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
        fprintf(stderr, "FAAM - Freeware Advanced Audio Muxer\n");
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
        print_usage('h');
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
