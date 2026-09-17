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

    /* Locate chpl atom inside moov/udta */
    io->seek(io->user_data, 0);
    uint32_t cap = 65536;
    uint8_t *buf = (uint8_t *)malloc(cap);
    if (!buf) {
        free(atom_buf);
        return FAAM_ERR_INSUFFICIENT_MEM;
    }

    int32_t bytes = io->read(io->user_data, buf, cap);
    uint32_t chpl_offset = 0;

    if (bytes >= 32) {
        uint32_t pos = 0;
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
                        uint32_t u_sub = sub + 8;
                        uint32_t udta_end = sub + sub_size;
                        while (u_sub + 8 <= udta_end) {
                            uint32_t u_size = read_u32_be(buf + u_sub);
                            if (u_size < 8 || u_sub + u_size > udta_end) break;
                            if (memcmp(buf + u_sub + 4, "chpl", 4) == 0) {
                                chpl_offset = u_sub;
                                break;
                            }
                            u_sub += u_size;
                        }
                    }
                    sub += sub_size;
                }
            }
            pos += size;
        }
    }
    free(buf);

    if (chpl_offset > 0) {
        io->seek(io->user_data, chpl_offset);
        io->write(io->user_data, atom_buf, atom_size);
    }

    free(atom_buf);
    return FAAM_OK;
}
