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

static int32_t file_read_cb(void *user_data, void *buf, uint32_t bytes) {
    return (int32_t)fread(buf, 1, bytes, (FILE *)user_data);
}

static int32_t file_write_cb(void *user_data, const void *buf, uint32_t bytes) {
    return (int32_t)fwrite(buf, 1, bytes, (FILE *)user_data);
}

static bool file_seek_cb(void *user_data, uint64_t offset) {
    return fseek((FILE *)user_data, (long)offset, SEEK_SET) == 0;
}

static uint64_t file_tell_cb(void *user_data) {
    return (uint64_t)ftell((FILE *)user_data);
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

    if (meta->title[0]) ilst_len += append_data_box(ilst_buf + ilst_len, "\251nam", ITUNES_DATA_TEXT, meta->title, strlen(meta->title));
    if (meta->artist[0]) ilst_len += append_data_box(ilst_buf + ilst_len, "\251ART", ITUNES_DATA_TEXT, meta->artist, strlen(meta->artist));
    if (meta->album[0]) ilst_len += append_data_box(ilst_buf + ilst_len, "\251alb", ITUNES_DATA_TEXT, meta->album, strlen(meta->album));
    if (meta->album_artist[0]) ilst_len += append_data_box(ilst_buf + ilst_len, "aART", ITUNES_DATA_TEXT, meta->album_artist, strlen(meta->album_artist));
    if (meta->composer[0]) ilst_len += append_data_box(ilst_buf + ilst_len, "\251wrt", ITUNES_DATA_TEXT, meta->composer, strlen(meta->composer));
    if (meta->year[0]) ilst_len += append_data_box(ilst_buf + ilst_len, "\251day", ITUNES_DATA_TEXT, meta->year, strlen(meta->year));
    if (meta->comment[0]) ilst_len += append_data_box(ilst_buf + ilst_len, "\251cmt", ITUNES_DATA_TEXT, meta->comment, strlen(meta->comment));
    if (meta->encoder[0]) ilst_len += append_data_box(ilst_buf + ilst_len, "\251too", ITUNES_DATA_TEXT, meta->encoder, strlen(meta->encoder));

    write_u32(ilst_buf, ilst_len);
    memcpy(ilst_buf + 4, "ilst", 4);

    /* Locate ilst atom inside moov/udta/meta */
    uint32_t pos = 0;
    uint32_t ilst_offset = 0;
    uint32_t meta_offset = 0;
    uint32_t udta_offset = 0;

    while (pos + 8 <= (uint32_t)bytes) {
        uint32_t size = read_u32_be(buf + pos);
        if (size < 8 || pos + size > (uint32_t)bytes) break;

        if (memcmp(buf + pos + 4, "moov", 4) == 0) {
            uint32_t sub = pos + 8;
            uint32_t moov_end = pos + size;
            while (sub + 8 <= moov_end) {
                uint32_t sub_size = read_u32_be(buf + sub);
                if (sub_size < 8 || sub + sub_size > moov_end) break;

                if (memcmp(buf + sub + 4, "udta", 4) == 0) {
                    udta_offset = sub;
                    uint32_t u_sub = sub + 8;
                    uint32_t udta_end = sub + sub_size;
                    while (u_sub + 8 <= udta_end) {
                        uint32_t u_size = read_u32_be(buf + u_sub);
                        if (u_size < 8 || u_sub + u_size > udta_end) break;
                        if (memcmp(buf + u_sub + 4, "meta", 4) == 0) {
                            meta_offset = u_sub;
                            uint32_t m_sub = u_sub + 12;
                            uint32_t meta_end = u_sub + u_size;
                            while (m_sub + 8 <= meta_end) {
                                uint32_t m_size = read_u32_be(buf + m_sub);
                                if (m_size < 8 || m_sub + m_size > meta_end) break;
                                if (memcmp(buf + m_sub + 4, "ilst", 4) == 0) {
                                    ilst_offset = m_sub;
                                    break;
                                }
                                m_sub += m_size;
                            }
                        }
                        u_sub += u_size;
                    }
                }
                sub += sub_size;
            }
        }
        pos += size;
    }

    if (ilst_offset > 0) {
        io->seek(io->user_data, ilst_offset);
        io->write(io->user_data, ilst_buf, ilst_len);
    } else if (meta_offset > 0) {
        io->seek(io->user_data, meta_offset + 12);
        io->write(io->user_data, ilst_buf, ilst_len);
    } else if (udta_offset > 0) {
        uint8_t meta_wrap[16384 + 12];
        uint32_t meta_len = 12 + ilst_len;
        write_u32(meta_wrap, meta_len);
        memcpy(meta_wrap + 4, "meta", 4);
        write_u32(meta_wrap + 8, 0); /* flags */
        memcpy(meta_wrap + 12, ilst_buf, ilst_len);

        io->seek(io->user_data, udta_offset + 8);
        io->write(io->user_data, meta_wrap, meta_len);
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
    io.read = file_read_cb;
    io.write = file_write_cb;
    io.seek = file_seek_cb;
    io.tell = file_tell_cb;

    faam_status st = faam_update_tags_stream(&io, meta);
    fclose(f);
    return st;
}
