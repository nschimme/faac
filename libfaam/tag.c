/*
 * Stream-based Tagging utilities for libfaam (iTunes ilst metadata atom writer)
 */

#include "libfaam_internal.h"

#define ITUNES_DATA_BINARY 0
#define ITUNES_DATA_TEXT   1
#define ITUNES_DATA_UINT8  0x15
#define ITUNES_DATA_IMAGE  0x0d

static inline void write_u32(uint8_t *b, uint32_t val) {
    b[0] = (uint8_t)(val >> 24);
    b[1] = (uint8_t)(val >> 16);
    b[2] = (uint8_t)(val >> 8);
    b[3] = (uint8_t)val;
}

static uint32_t append_data_box(uint8_t *dst, const char *name, uint32_t type_code, const void *data, size_t len) {
    if (!name || !data || len == 0) return 0;
    uint32_t box_size = 8 + 16 + (uint32_t)len;

    /* Atom box header */
    write_u32(dst, box_size);
    memcpy(dst + 4, name, 4);

    /* Data box header */
    write_u32(dst + 8, 16 + (uint32_t)len);
    memcpy(dst + 12, "data", 4);
    write_u32(dst + 16, type_code);
    write_u32(dst + 20, 0);

    memcpy(dst + 24, data, len);
    return box_size;
}

faam_status faam_update_tags_stream(const faam_io *io, const faam_metadata *meta)
{
    if (!io || !meta) return FAAM_ERR_INVALID_ARG;
    if (!io->read || !io->write || !io->seek || !io->tell) return FAAM_ERR_INVALID_ARG;

    io->seek(io->user_data, 0);
    uint32_t cap = 65536;
    uint8_t *buf = (uint8_t *)malloc(cap);
    if (!buf) return FAAM_ERR_INSUFFICIENT_MEM;

    int32_t bytes = io->read(io->user_data, buf, cap);
    if (bytes < 32) {
        free(buf);
        return FAAM_ERR_BAD_CONTAINER;
    }

    /* Build new ilst atom payload */
    uint8_t ilst_buf[16384];
    uint32_t ilst_len = 8; /* reserve 8 bytes for ilst size and type */

    if (meta->title[0]) ilst_len += append_data_box(ilst_buf + ilst_len, "\xa9nam", ITUNES_DATA_TEXT, meta->title, strlen(meta->title));
    if (meta->artist[0]) ilst_len += append_data_box(ilst_buf + ilst_len, "\xa9ART", ITUNES_DATA_TEXT, meta->artist, strlen(meta->artist));
    if (meta->album[0]) ilst_len += append_data_box(ilst_buf + ilst_len, "\xa9alb", ITUNES_DATA_TEXT, meta->album, strlen(meta->album));
    if (meta->album_artist[0]) ilst_len += append_data_box(ilst_buf + ilst_len, "aART", ITUNES_DATA_TEXT, meta->album_artist, strlen(meta->album_artist));
    if (meta->composer[0]) ilst_len += append_data_box(ilst_buf + ilst_len, "\xa9wrt", ITUNES_DATA_TEXT, meta->composer, strlen(meta->composer));
    if (meta->year[0]) ilst_len += append_data_box(ilst_buf + ilst_len, "\xa9day", ITUNES_DATA_TEXT, meta->year, strlen(meta->year));
    if (meta->comment[0]) ilst_len += append_data_box(ilst_buf + ilst_len, "\xa9cmt", ITUNES_DATA_TEXT, meta->comment, strlen(meta->comment));
    if (meta->encoder[0]) ilst_len += append_data_box(ilst_buf + ilst_len, "\xa9too", ITUNES_DATA_TEXT, meta->encoder, strlen(meta->encoder));

    write_u32(ilst_buf, ilst_len);
    memcpy(ilst_buf + 4, "ilst", 4);

    /* Locate moov atom */
    uint32_t pos = 0;
    while (pos + 8 <= (uint32_t)bytes) {
        uint32_t size = read_u32_be(buf + pos);
        if (size < 8 || pos + size > (uint32_t)bytes) break;

        if (memcmp(buf + pos + 4, "moov", 4) == 0) {
            /* Seek to moov payload and commit updated ilst_buf */
            uint64_t ilst_pos = pos + 8;
            io->seek(io->user_data, ilst_pos);
            io->write(io->user_data, ilst_buf, ilst_len);
            break;
        }
        pos += size;
    }

    free(buf);
    return FAAM_OK;
}

faam_status faam_update_tags(const char *filepath, const faam_metadata *meta)
{
    if (!filepath || !meta) return FAAM_ERR_INVALID_ARG;

    FILE *f = fopen(filepath, "r+b");
    if (!f) return FAAM_ERR_IO_READ;

    faam_io io;
    io.user_data = f;
    io.read = (int32_t (*)(void *, void *, uint32_t))fread;
    io.write = (int32_t (*)(void *, const void *, uint32_t))fwrite;
    io.seek = (bool (*)(void *, uint64_t))fseek;
    io.tell = (uint64_t (*)(void *))ftell;

    faam_status st = faam_update_tags_stream(&io, meta);
    fclose(f);
    return st;
}
