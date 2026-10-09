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
 * Stream-based Chapter / Bookmark management for libfaam (QuickTime chpl atom)
 */

#include "endian.h"
#include "libfaam_internal.h"

faam_status faam_update_chapters_stream(const faam_io *caller_io, const faam_chapter *chapters, uint32_t count, uint32_t flags)
{
    faam_io loaded;
    const faam_io *io = &loaded;
    if (flags || !caller_io || (count && !chapters) || count > 255 || !faam_io_load(&loaded, caller_io)) return FAAM_ERR_INVALID_ARG;
    if (count && chapters->struct_size < FAAM_CHAPTER_BASELINE) return FAAM_ERR_INVALID_ARG;
    for (uint32_t c = 0; c < count; c++)
        if (!faam_chapter_at(chapters, c)->title) return FAAM_ERR_INVALID_ARG;
    if (!io->read || !io->write || !io->seek || !io->tell) return FAAM_ERR_UNSUPPORTED;

    faam_atom_ref moov, mdat;
    uint64_t file_size = 0;
    faam_status scan = faam_atom_scan_top(io, &moov, &mdat, &file_size);
    if (scan != FAAM_OK) return scan;
    if (moov.size < 8) return FAAM_ERR_BAD_CONTAINER;

    faam_atom_ref udta = { 0, 0 }, chpl = { 0, 0 };
    int found = faam_atom_find_child(io, moov.offset + 8, moov.offset + moov.size, "udta", &udta);
    if (found > 0)
        found = faam_atom_find_child(io, udta.offset + 8, udta.offset + udta.size, "chpl", &chpl);
    if (found == -2) return FAAM_ERR_UNSUPPORTED;
    if (found < 0) return FAAM_ERR_IO_READ;

    uint32_t chpl_cap = 17 + count * 265;
    uint8_t *chpl_buf = (uint8_t *)AllocMemory(chpl_cap);
    if (!chpl_buf) return FAAM_ERR_INSUFFICIENT_MEM;

    uint32_t chpl_len = 17;
    write_u32_be(chpl_buf + 8, 1U << 24); /* ver/flags */
    write_u32_be(chpl_buf + 12, 0);
    chpl_buf[16] = (uint8_t)count;

    for (uint32_t c = 0; c < count; c++) {
        uint64_t start_time = faam_chapter_at(chapters, c)->start_ms * 10000ULL; /* 100ns units */
        write_u64_be(chpl_buf + chpl_len, start_time);
        chpl_len += 8;

        size_t tlen = strlen(faam_chapter_at(chapters, c)->title);
        if (tlen > 255) tlen = 255;
        chpl_buf[chpl_len++] = (uint8_t)tlen;
        memcpy(chpl_buf + chpl_len, faam_chapter_at(chapters, c)->title, tlen);
        chpl_len += (uint32_t)tlen;
    }

    write_u32_be(chpl_buf, chpl_len);
    memcpy(chpl_buf + 4, "chpl", 4);

    faam_status rc;
    if (chpl.size >= 8) {
        uint64_t ancestors[2] = { udta.offset, moov.offset };
        rc = faam_atom_resize(io, chpl.offset, chpl.size, chpl_buf, chpl_len,
                               ancestors, 2, &moov, &mdat, file_size);
    } else {
        rc = faam_atom_append_udta_child(io, &udta, &moov, &mdat, file_size, chpl_buf, chpl_len);
    }

    FreeMemory(chpl_buf);
    return rc;
}
