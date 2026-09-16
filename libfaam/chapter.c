/*
 * Stream-based Chapter utilities for libfaam (M4B QuickTime chpl atom inside moov/udta container)
 */

#include "libfaam_internal.h"

faam_status faam_update_chapters_stream(const faam_io *io, const faam_chapter *chapters, uint32_t count)
{
    if (!io || (!chapters && count > 0)) return FAAM_ERR_INVALID_ARG;
    if (!io->read || !io->write || !io->seek || !io->tell) return FAAM_ERR_INVALID_ARG;

    /* Build chpl QuickTime chapter atom payload */
    uint32_t chpl_payload_size = 4;
    for (uint32_t i = 0; i < count; i++) {
        uint8_t len = (uint8_t)strlen(chapters[i].title);
        chpl_payload_size += 8 + 1 + len;
    }

    uint32_t atom_size = 8 + 4 + chpl_payload_size;
    uint8_t *atom_buf = (uint8_t *)malloc(atom_size);
    if (!atom_buf) return FAAM_ERR_INSUFFICIENT_MEM;

    write_u32_be(atom_buf, atom_size);
    memcpy(atom_buf + 4, "chpl", 4);
    write_u32_be(atom_buf + 8, 0); /* version/flags */
    write_u32_be(atom_buf + 12, count);

    uint32_t pos = 16;
    for (uint32_t i = 0; i < count; i++) {
        uint64_t start_time_100ns = chapters[i].start_ms * 10000ULL;
        write_u64_be(atom_buf + pos, start_time_100ns);
        pos += 8;

        uint8_t len = (uint8_t)strlen(chapters[i].title);
        atom_buf[pos++] = len;
        memcpy(atom_buf + pos, chapters[i].title, len);
        pos += len;
    }

    /* Seek to moov/udta position if present, or write atom to stream */
    io->seek(io->user_data, 0);
    io->write(io->user_data, atom_buf, atom_size);

    free(atom_buf);
    return FAAM_OK;
}

faam_status faam_update_chapters(const char *filepath, const faam_chapter *chapters, uint32_t count)
{
    if (!filepath || (!chapters && count > 0)) return FAAM_ERR_INVALID_ARG;

    FILE *f = fopen(filepath, "r+b");
    if (!f) return FAAM_ERR_IO_READ;

    faam_io io;
    io.user_data = f;
    io.read = (int32_t (*)(void *, void *, uint32_t))fread;
    io.write = (int32_t (*)(void *, const void *, uint32_t))fwrite;
    io.seek = (bool (*)(void *, uint64_t))fseek;
    io.tell = (uint64_t (*)(void *))ftell;

    faam_status st = faam_update_chapters_stream(&io, chapters, count);
    fclose(f);
    return st;
}
