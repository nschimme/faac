/*
 * FAAM - Freeware Advanced Audio/Video Muxer
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
 * Stream-based Tagging utilities for libfaam (iTunes ilst metadata atom writer)
 */

#include "libfaam_internal.h"

static inline void write_u32(uint8_t *b, uint32_t val) { write_u32_be(b, val); }

typedef struct {
    uint8_t *data;
    uint32_t pos;
} ilst_buffer;
static faam_status append_ilst(void *user, const void *data, uint32_t bytes) {
    ilst_buffer *buffer = (ilst_buffer *)user;
    memcpy(buffer->data + buffer->pos, data, bytes);
    buffer->pos += bytes;
    return FAAM_OK;
}

faam_status faam_update_tags_stream(const faam_io *io, const faam_metadata *meta)
{
    if (!io || !meta) return FAAM_ERR_INVALID_ARG;
    if (!io->read || !io->write || !io->seek || !io->tell) return FAAM_ERR_INVALID_ARG;

    faam_atom_ref moov, mdat;
    uint64_t file_size = 0;
    faam_atom_scan_top(io, &moov, &mdat, &file_size);
    if (moov.size < 8) return FAAM_ERR_BAD_CONTAINER;

    faam_atom_ref udta = { 0, 0 }, meta_atom = { 0, 0 }, ilst = { 0, 0 };
    faam_atom_find_child(io, moov.offset + 8, moov.offset + moov.size, "udta", &udta);
    if (udta.size >= 8)
        faam_atom_find_child(io, udta.offset + 8, udta.offset + udta.size, "meta", &meta_atom);
    if (meta_atom.size >= 12)
        faam_atom_find_child(io, meta_atom.offset + 12, meta_atom.offset + meta_atom.size, "ilst", &ilst);

    uint8_t *smpb = NULL;
    uint32_t smpb_len = 0;
    for (uint64_t pos = ilst.offset + 8; ilst.size >= 8 && pos + 8 <= ilst.offset + ilst.size;) {
        uint8_t hdr[8];
        if (!io->seek(io->user_data, pos) || io->read(io->user_data, hdr, 8) != 8)
            return FAAM_ERR_IO_READ;
        uint32_t size = read_u32_be(hdr);
        if (size < 8 || size > ilst.offset + ilst.size - pos) break;
        if (!memcmp(hdr + 4, "----", 4)) {
            uint8_t *atom = (uint8_t *)AllocMemory(size);
            if (!atom) return FAAM_ERR_INSUFFICIENT_MEM;
            if (!io->seek(io->user_data, pos) || io->read(io->user_data, atom, size) != (int32_t)size) {
                FreeMemory(atom); return FAAM_ERR_IO_READ;
            }
            bool apple = false, name = false;
            for (uint32_t off = 8; off + 12 <= size;) {
                uint32_t sub = read_u32_be(atom + off);
                if (sub < 12 || sub > size - off) break;
                if (!memcmp(atom + off + 4, "mean", 4) && sub == 28 &&
                    !memcmp(atom + off + 12, "com.apple.iTunes", 16)) apple = true;
                if (!memcmp(atom + off + 4, "name", 4) && sub == 20 &&
                    !memcmp(atom + off + 12, "iTunSMPB", 8)) name = true;
                off += sub;
            }
            if (apple && name) { smpb = atom; smpb_len = size; break; }
            FreeMemory(atom);
        }
        pos += size;
    }
    uint32_t ilst_len;
    faam_status build = faam_write_ilst(meta, NULL, smpb, smpb_len, NULL, NULL, &ilst_len);
    if (build != FAAM_OK) { FreeMemory(smpb); return build; }
    if (ilst_len > UINT32_MAX - 12) { FreeMemory(smpb); return FAAM_ERR_UNSUPPORTED; }
    uint8_t *ilst_buf = (uint8_t *)AllocMemory(ilst_len);
    if (!ilst_buf) { FreeMemory(smpb); return FAAM_ERR_INSUFFICIENT_MEM; }
    ilst_buffer buffer = {ilst_buf, 0};
    build = faam_write_ilst(meta, NULL, smpb, smpb_len, append_ilst, &buffer, &ilst_len);
    FreeMemory(smpb);
    if (build != FAAM_OK) { FreeMemory(ilst_buf); return build; }

    uint64_t ancestors[3];
    int n_anc = 0;
    faam_status rc;

    if (ilst.size >= 8) {
        ancestors[n_anc++] = meta_atom.offset;
        ancestors[n_anc++] = udta.offset;
        ancestors[n_anc++] = moov.offset;
        rc = faam_atom_resize(io, ilst.offset, ilst.size, ilst_buf, ilst_len,
                               ancestors, n_anc, &moov, &mdat, file_size);
    } else if (meta_atom.size >= 12) {
        /* No existing ilst: insert it as a new child at the end of meta's
         * current content instead of at meta_offset+12, so an existing hdlr
         * (or anything else already inside meta) isn't clobbered. */
        ancestors[n_anc++] = meta_atom.offset;
        ancestors[n_anc++] = udta.offset;
        ancestors[n_anc++] = moov.offset;
        rc = faam_atom_resize(io, meta_atom.offset + meta_atom.size, 0, ilst_buf, ilst_len,
                               ancestors, n_anc, &moov, &mdat, file_size);
    } else if (udta.size >= 8) {
        uint32_t wrap_len = 12 + ilst_len;
        uint8_t *wrap = (uint8_t *)AllocMemory(wrap_len);
        if (!wrap) {
            FreeMemory(ilst_buf);
            return FAAM_ERR_INSUFFICIENT_MEM;
        }
        write_u32(wrap, wrap_len);
        memcpy(wrap + 4, "meta", 4);
        write_u32(wrap + 8, 0); /* version/flags */
        memcpy(wrap + 12, ilst_buf, ilst_len);

        ancestors[n_anc++] = udta.offset;
        ancestors[n_anc++] = moov.offset;
        rc = faam_atom_resize(io, udta.offset + udta.size, 0, wrap, wrap_len,
                               ancestors, n_anc, &moov, &mdat, file_size);
        FreeMemory(wrap);
    } else {
        /* No udta atom at all in this moov. faam's own muxer always emits
         * one; a third-party file without any udta/meta scaffolding isn't
         * handled here (would need to synthesize udta itself). */
        rc = FAAM_ERR_BAD_CONTAINER;
    }

    FreeMemory(ilst_buf);
    return rc;
}
