/*
 * Wrapper delegating mp4write to libfaam muxer over abstract stream I/O
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define access _access
#include "charset.h"
#else
#include <unistd.h>
#endif
#include "cli_common.h"

#include "mp4write.h"

#ifdef HAVE_LIBFAAM
#include "faam.h"

static faam_muxer *g_muxer = NULL;
static faam_metadata g_metadata;
static faam_custom_tag *g_custom_tags;
static uint32_t g_custom_capacity;
static uint16_t g_sample_size = 16;
static faam_muxer_config g_cfg;
static faam_track_config g_track_cfg;
static FILE *g_file = NULL;
static faam_io g_io;

static bool file_flush_cb(void *user_data) { return fflush((FILE *)user_data) == 0; }

static int32_t file_read_cb(void *user_data, void *buf, uint32_t bytes) {
    return (int32_t)fread(buf, 1, bytes, (FILE *)user_data);
}

static int32_t file_write_cb(void *user_data, const void *buf, uint32_t bytes) {
    return (int32_t)fwrite(buf, 1, bytes, (FILE *)user_data);
}

static bool file_seek_cb(void *user_data, uint64_t offset) {
#ifdef _WIN32
    return _fseeki64((FILE *)user_data, (int64_t)offset, SEEK_SET) == 0;
#else
    return fseeko((FILE *)user_data, (off_t)offset, SEEK_SET) == 0;
#endif
}

static uint64_t file_tell_cb(void *user_data) {
#ifdef _WIN32
    return (uint64_t)_ftelli64((FILE *)user_data);
#else
    return (uint64_t)ftello((FILE *)user_data);
#endif
}
#endif

int mp4_open(const char *path, bool overwrite) {
#ifdef HAVE_LIBFAAM
#ifdef _WIN32
    if (!overwrite && win32_access_utf8(path, 0) == 0) return 1;
#else
    if (!overwrite && access(path, 0) == 0) return 1;
#endif
    g_file = cli_fopen(path, "wb");
    if (!g_file) return 1;

    g_io.user_data = g_file;
    g_io.read = file_read_cb;
    g_io.write = file_write_cb;
    g_io.seek = file_seek_cb;
    g_io.tell = file_tell_cb;
    g_io.flush = file_flush_cb;

    const uint8_t *codec_data_backup = g_track_cfg.codec_data;
    uint32_t codec_len_backup = g_track_cfg.codec_data_len;

    faam_muxer_config_init(&g_cfg, sizeof(g_cfg));
    g_cfg.metadata = &g_metadata;

    memset(&g_track_cfg, 0, sizeof(g_track_cfg));
    g_track_cfg.struct_size = sizeof(g_track_cfg);
    g_track_cfg.track_type = FAAM_TRACK_AUDIO;
    g_track_cfg.codec_id = FAAM_CODEC_AAC;
    g_track_cfg.timescale = 44100;
    g_track_cfg.codec_data = codec_data_backup;
    g_track_cfg.codec_data_len = codec_len_backup;
    return 0;
#else
    (void)path; (void)overwrite;
    return 1;
#endif
}

void mp4_set_creation_time(uint32_t t) {
#ifdef HAVE_LIBFAAM
    g_cfg.creation_time = t;
    if (g_muxer) faam_muxer_set_creation_time(g_muxer, t);
#else
    (void)t;
#endif
}

void mp4_set_format(uint32_t samplerate, uint32_t channels, uint32_t bits) {
#ifdef HAVE_LIBFAAM
    g_track_cfg.timescale = samplerate;
    g_track_cfg.sample_rate = samplerate;
    g_track_cfg.channels = channels;
    g_sample_size = (uint16_t)bits;
#else
    (void)samplerate; (void)channels; (void)bits;
#endif
}

void mp4_set_constant_rate(bool constant) {
#ifdef HAVE_LIBFAAM
    g_cfg.constant_rate = constant;
#else
    (void)constant;
#endif
}

void mp4_set_decoder_config(const uint8_t *asc, unsigned long size) {
#ifdef HAVE_LIBFAAM
    g_track_cfg.codec_data = asc;
    g_track_cfg.codec_data_len = (uint32_t)size;
#else
    (void)asc; (void)size;
#endif
}

void mp4_set_encoder(const char *value) {
#ifdef HAVE_LIBFAAM
    g_metadata.encoder = value;
#else
    (void)value;
#endif
}

void mp4_set_tag(mp4_tag_id_t id, const char *value) {
#ifdef HAVE_LIBFAAM
    if (!value) return;
    switch (id) {
    case MP4TAG_ARTIST: g_metadata.artist = value; break;
    case MP4TAG_ARTISTSORT: g_metadata.artist_sort = value; break;
    case MP4TAG_TITLE: g_metadata.title = value; break;
    case MP4TAG_ALBUM: g_metadata.album = value; break;
    case MP4TAG_ALBUMSORT: g_metadata.album_sort = value; break;
    case MP4TAG_ALBUMARTIST: g_metadata.album_artist = value; break;
    case MP4TAG_ALBUMARTISTSORT: g_metadata.album_artist_sort = value; break;
    case MP4TAG_COMPOSER: g_metadata.composer = value; break;
    case MP4TAG_COMPOSERSORT: g_metadata.composer_sort = value; break;
    case MP4TAG_YEAR: g_metadata.year = value; break;
    case MP4TAG_COMMENT: g_metadata.comment = value; break;
    default: break;
    }
#else
    (void)id; (void)value;
#endif
}

void mp4_set_genre(uint16_t genre) {
#ifdef HAVE_LIBFAAM
    g_metadata.genre_code = genre;
#else
    (void)genre;
#endif
}

void mp4_set_language(const char *lang) {
#ifdef HAVE_LIBFAAM
    memset(g_track_cfg.language, 0, sizeof(g_track_cfg.language));
    if (lang && strlen(lang) >= 3) memcpy(g_track_cfg.language, lang, 3);
#else
    (void)lang;
#endif
}

void mp4_set_compilation(bool flag) {
#ifdef HAVE_LIBFAAM
    g_metadata.compilation = flag;
#else
    (void)flag;
#endif
}

void mp4_set_track(uint16_t num, uint16_t total) {
#ifdef HAVE_LIBFAAM
    g_metadata.track_num = num; g_metadata.track_total = total;
#else
    (void)num; (void)total;
#endif
}

void mp4_set_disc(uint16_t num, uint16_t total) {
#ifdef HAVE_LIBFAAM
    g_metadata.disc_num = num; g_metadata.disc_total = total;
#else
    (void)num; (void)total;
#endif
}

void mp4_set_cover(const uint8_t *data, uint32_t size) {
#ifdef HAVE_LIBFAAM
    g_metadata.cover_art = data; g_metadata.cover_bytes = size;
#else
    (void)data; (void)size;
#endif
}

void mp4_set_gapless(uint32_t priming, uint32_t padding, uint64_t original_samples) {
#ifdef HAVE_LIBFAAM
    g_cfg.gapless.encoder_delay = priming;
    g_cfg.gapless.end_padding = padding;
    g_cfg.gapless.total_samples = original_samples;
#else
    (void)priming; (void)padding; (void)original_samples;
#endif
}

int mp4_add_custom_tag(const char *name, const char *value) {
#ifdef HAVE_LIBFAAM
    if (!name || !value) return -1;
    uint32_t idx = g_metadata.num_custom_tags;
    if (idx == g_custom_capacity) {
        uint32_t capacity = g_custom_capacity ? g_custom_capacity * 2 : 8;
        if (capacity < g_custom_capacity || capacity > UINT32_MAX / sizeof(*g_custom_tags)) return -1;
        faam_custom_tag *tags = (faam_custom_tag *)realloc(g_custom_tags, (size_t)capacity * sizeof(*tags));
        if (!tags) return -1;
        g_custom_tags = tags;
        g_custom_capacity = capacity;
    }
    char *copy = (char *)malloc(strlen(value) + 1);
    if (!copy) return -1;
    memcpy(copy, value, strlen(value) + 1);
    g_custom_tags[idx].name = name;
    g_custom_tags[idx].value = copy;
    g_metadata.custom_tags = g_custom_tags;
    g_metadata.num_custom_tags++;
    return 0;
#else
    (void)name; (void)value;
    return -1;
#endif
}

int mp4_write_frame(const uint8_t *data, uint32_t size, uint32_t samples) {
#ifdef HAVE_LIBFAAM
    if (!g_muxer) {
        faam_muxer_config_add_track(&g_cfg, &g_track_cfg, NULL);

        if (faam_muxer_open(&g_cfg, &g_io, &g_muxer) != FAAM_OK) return -1;
        if (faam_muxer_set_audio_sample_size(g_muxer, 1, g_sample_size) != FAAM_OK) return -1;
    }
    return faam_muxer_write_frame(g_muxer, 1, data, size, samples, true) == FAAM_OK ? 0 : -1;
#else
    (void)data; (void)size; (void)samples;
    return -1;
#endif
}

int mp4_finish(void) {
#ifdef HAVE_LIBFAAM
    if (g_muxer) {
        faam_muxer_set_gapless(g_muxer, &g_cfg.gapless);
        return faam_muxer_finalize(g_muxer) == FAAM_OK ? 0 : 1;
    }
#endif
    return 1;
}

int mp4_close(void) {
#ifdef HAVE_LIBFAAM
    if (g_muxer) {
        faam_muxer_close(&g_muxer);
        g_muxer = NULL;
    }
    for (uint32_t i = 0; i < g_metadata.num_custom_tags; i++) free((void *)g_custom_tags[i].value);
    free(g_custom_tags);
    g_custom_tags = NULL;
    g_custom_capacity = 0;
    g_metadata.custom_tags = NULL;
    g_metadata.num_custom_tags = 0;
    if (g_file) {
        int result = fclose(g_file);
        g_file = NULL;
        return result != 0;
    }
#endif
    return 0;
}

static inline void get_info_helper(faam_muxer_info *info) {
    memset(info, 0, sizeof(*info));
    info->struct_size = sizeof(*info);
#ifdef HAVE_LIBFAAM
    if (g_muxer) faam_muxer_get_info(g_muxer, 1, info);
#endif
}

uint32_t mp4_frame_count(void) {
    faam_muxer_info info; get_info_helper(&info); return info.frame_count;
}

uint64_t mp4_sample_count(void) {
    faam_muxer_info info; get_info_helper(&info); return info.duration_ticks;
}

uint32_t mp4_max_bitrate(void) {
    faam_muxer_info info; get_info_helper(&info); return info.max_bitrate;
}

uint32_t mp4_avg_bitrate(void) {
    faam_muxer_info info; get_info_helper(&info); return info.avg_bitrate;
}

uint32_t mp4_max_frame_size(void) {
    faam_muxer_info info; get_info_helper(&info); return info.max_frame_size;
}
