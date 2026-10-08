/*
 * libFuzzer harness for faam_update_tags_stream and faam_update_chapters_stream.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "faam.h"

typedef struct {
    uint8_t *buf;
    size_t size;
    size_t cap;
    size_t pos;
} rw_mem_stream;

static int32_t rw_read(void *user_data, void *buf, uint32_t bytes_to_read)
{
    rw_mem_stream *s = (rw_mem_stream *)user_data;
    if (s->pos >= s->size) return 0;
    size_t rem = s->size - s->pos;
    uint32_t to_copy = (uint32_t)(rem < bytes_to_read ? rem : bytes_to_read);
    memcpy(buf, s->buf + s->pos, to_copy);
    s->pos += to_copy;
    return (int32_t)to_copy;
}

static int32_t rw_write(void *user_data, const void *buf, uint32_t bytes_to_write)
{
    rw_mem_stream *s = (rw_mem_stream *)user_data;
    if (s->pos + bytes_to_write > s->cap) {
        size_t new_cap = (s->pos + bytes_to_write) * 2 + 1024;
        if (new_cap > 10 * 1024 * 1024) return -1; /* Limit in-memory allocation */
        uint8_t *p = realloc(s->buf, new_cap);
        if (!p) return -1;
        s->buf = p;
        s->cap = new_cap;
    }
    if (s->pos > s->size) memset(s->buf + s->size, 0, s->pos - s->size);
    memcpy(s->buf + s->pos, buf, bytes_to_write);
    s->pos += bytes_to_write;
    if (s->pos > s->size) s->size = s->pos;
    return (int32_t)bytes_to_write;
}

static bool rw_seek(void *user_data, uint64_t offset)
{
    rw_mem_stream *s = (rw_mem_stream *)user_data;
    if (offset > 10 * 1024 * 1024) return false;
    s->pos = (size_t)offset;
    return true;
}

static uint64_t rw_tell(void *user_data)
{
    rw_mem_stream *s = (rw_mem_stream *)user_data;
    return (uint64_t)s->pos;
}

/* Track count of a file the demuxer accepts, or -1. Only files that carry a track are held to
 * the "still readable after an edit" rule: damaged input that happens to open with no tracks
 * can legitimately become unreadable once an edit makes its structure consistent. */
static int tracks_of(const faam_io *io, rw_mem_stream *s)
{
    s->pos = 0;
    faam_demuxer *d = NULL;
    if (faam_demuxer_open(NULL, io, &d) != FAAM_OK || !d) return -1;
    uint32_t n = 0;
    faam_status st = faam_demuxer_get_num_tracks(d, &n, NULL);
    faam_demuxer_close(&d);
    return st == FAAM_OK ? (int)n : -1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 16) return 0;

    /* Use data[0] as a configuration byte */
    uint8_t cfg_byte = data[0];
    const uint8_t *file_data = data + 1;
    size_t file_len = size - 1;

    rw_mem_stream ms;
    ms.cap = file_len + 4096;
    ms.buf = malloc(ms.cap);
    if (!ms.buf) return 0;
    memcpy(ms.buf, file_data, file_len);
    ms.size = file_len;
    ms.pos = 0;

    faam_io io = { sizeof(faam_io), &ms, rw_read, rw_write, rw_seek, rw_tell, NULL };

    /* Construct fuzz-derived metadata */
    char title_buf[32];
    snprintf(title_buf, sizeof(title_buf), "FuzzTitle_%u", cfg_byte);

    faam_metadata meta;
    memset(&meta, 0, sizeof(meta)); meta.struct_size = sizeof(meta);
    meta.title = title_buf;
    meta.artist = (cfg_byte & 1) ? "FuzzArtist" : NULL;
    meta.genre_code = (cfg_byte & 2) ? 17 : 0;
    meta.compilation = (cfg_byte & 4) != 0;

    int tracks = tracks_of(&io, &ms);
    ms.pos = 0;
    faam_status st = faam_update_tags_stream(&io, &meta, 0);
    if (st == FAAM_OK && tracks > 0 && tracks_of(&io, &ms) != tracks) abort();

    /* Construct fuzz-derived chapters */
    faam_chapter chaps[2];
    for (uint32_t i = 0; i < 2; i++) chaps[i].struct_size = sizeof(chaps[i]);
    chaps[0].start_ms = 0;
    chaps[0].title = "Intro";
    chaps[1].start_ms = (cfg_byte * 100);
    chaps[1].title = "Part 2";

    ms.pos = 0;
    tracks = tracks_of(&io, &ms);
    ms.pos = 0;
    st = faam_update_chapters_stream(&io, chaps, 2, 0);
    /* Chapters add a track of their own, so only losing one is wrong. */
    if (st == FAAM_OK && tracks > 0 && tracks_of(&io, &ms) < tracks) abort();

    free(ms.buf);
    return 0;
}
