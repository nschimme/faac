/*
 * libFuzzer harness for libfaam demuxer standalone over in-memory faam_io.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "faam.h"

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
} mem_stream;

static int32_t mem_read(void *user_data, void *buf, uint32_t bytes_to_read)
{
    mem_stream *s = (mem_stream *)user_data;
    if (s->pos >= s->size) return 0;
    size_t rem = s->size - s->pos;
    uint32_t to_copy = (uint32_t)(rem < bytes_to_read ? rem : bytes_to_read);
    memcpy(buf, s->data + s->pos, to_copy);
    s->pos += to_copy;
    return (int32_t)to_copy;
}

static bool mem_seek(void *user_data, uint64_t offset)
{
    mem_stream *s = (mem_stream *)user_data;
    if (offset > s->size) return false;
    s->pos = (size_t)offset;
    return true;
}

static uint64_t mem_tell(void *user_data)
{
    mem_stream *s = (mem_stream *)user_data;
    return (uint64_t)s->pos;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    mem_stream ms = { data, size, 0 };
    faam_io io = { sizeof(faam_io), &ms, mem_read, NULL, mem_seek, mem_tell, NULL };

    faam_demuxer *d = NULL;
    if (faam_demuxer_open(NULL, &io, &d) != FAAM_OK || !d) {
        return 0;
    }

    uint32_t num_tracks = 0;
    (void)faam_demuxer_get_num_tracks(d, &num_tracks, NULL);

    char brand[5];
    (void)faam_demuxer_get_major_brand(d, brand);

    faam_gapless_info gapless; gapless.struct_size = sizeof(gapless);
    (void)faam_demuxer_get_track_gapless(d, 0, &gapless);

    faam_metadata meta = { .struct_size = sizeof(meta) };
    (void)faam_demuxer_get_metadata(d, &meta);

    faam_chapter chapters[16];

    for (uint32_t i = 0; i < 16; i++) chapters[i].struct_size = sizeof(chapters[i]);
    uint32_t chapter_count = 0;
    if (faam_demuxer_get_num_chapters(d, &chapter_count) == FAAM_OK) {
        for (uint32_t i = 0; i < chapter_count && i < 16; i++)
            (void)faam_demuxer_get_chapter(d, i, &chapters[i]);
    }

    for (uint32_t i = 0; i < meta.num_custom_tags; i++) {
        faam_custom_tag tag = { .struct_size = sizeof(tag) };
        (void)faam_demuxer_get_custom_tag(d, i, &tag);
    }

    for (uint32_t i = 0; i < num_tracks && i < 8; i++) {
        faam_track_info ti;
        ti.struct_size = sizeof(ti);
        if (faam_demuxer_get_track_info(d, i, &ti) == FAAM_OK) {
            uint8_t codec_buf[256];
            uint32_t codec_len = 0;
            (void)faam_demuxer_get_codec_data(d, ti.track_id, codec_buf, sizeof(codec_buf), &codec_len);
        }
    }

    faam_frame_loc loc; loc.struct_size = sizeof(loc);
    uint8_t frame_buf[4096];
    int frame_count = 0;

    while (faam_demuxer_next_frame_loc(d, &loc) == FAAM_OK && frame_count < 128) {
        uint32_t read_bytes = 0;
        uint32_t cap = loc.frame_bytes < sizeof(frame_buf) ? loc.frame_bytes : sizeof(frame_buf);
        (void)faam_demuxer_read_frame(d, frame_buf, cap, &read_bytes);
        frame_count++;
    }

    faam_demuxer_close(&d);
    return 0;
}
