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
 * Stream-based Tagging utilities for libfaam (iTunes ilst metadata atom writer)
 */

#include "endian.h"
#include "libfaam_internal.h"

/* meta header, version/flags and the 33-byte mdir handler that precedes ilst. */
#define MFULL_PREFIX 45

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

static faam_status read_atom(const faam_io *io, uint64_t offset, uint32_t size,
                             uint8_t **out)
{
    uint8_t *atom = (uint8_t *)AllocMemory(size);
    if (!atom) return FAAM_ERR_NO_MEMORY;
    if (!io->seek(io->user_data, offset) ||
        faam_io_read_full(io, atom, size) != (int32_t)size) {
        FreeMemory(atom);
        return FAAM_ERR_IO_READ;
    }
    *out = atom;
    return FAAM_OK;
}

static faam_status append_bytes(uint8_t **buffer, uint32_t *length,
                                uint32_t *capacity, const uint8_t *data, uint32_t size)
{
    /* Atom lengths are 32-bit, so neither addition nor capacity growth may wrap. */
    if (size > UINT32_MAX - *length) return FAAM_ERR_UNSUPPORTED;
    uint32_t needed = *length + size;
    if (needed > *capacity) {
        uint32_t grown = *capacity ? *capacity : 256;
        while (grown < needed) {
            if (grown > UINT32_MAX / 2) { grown = needed; break; }
            grown *= 2;
        }
        uint8_t *tmp = (uint8_t *)faam_grow_memory(*buffer, *length, grown);
        if (!tmp) return FAAM_ERR_NO_MEMORY;
        *buffer = tmp;
        *capacity = grown;
    }
    memcpy(*buffer + *length, data, size);
    *length = needed;
    return FAAM_OK;
}

faam_status faam_update_metadata_stream(const faam_io *caller_io, const faam_metadata_entry *entries,
                                        uint32_t count, uint32_t flags)
{
    faam_io loaded;
    const faam_io *io = &loaded;
    if (!caller_io || !faam_io_load(&loaded, caller_io) || (flags & ~(uint32_t)FAAM_METADATA_UPDATE_CLEAR))
        return FAAM_ERR_INVALID_ARGUMENT;
    const faam_entry_list metadata = { .entries = entries, .count = count };
    const faam_entry_list *meta = &metadata;
    if (!io->read || !io->write || !io->seek || !io->tell) return FAAM_ERR_UNSUPPORTED;

    faam_status validation = faam_metadata_entries_validate(meta->entries, meta->count);
    if (validation != FAAM_OK) return validation;

    faam_atom_ref moov, mdat;
    uint64_t file_size = 0;
    faam_status scan = faam_atom_scan_top(io, &moov, &mdat, &file_size);
    if (scan != FAAM_OK) return scan;
    if (moov.size < 8) return FAAM_ERR_BAD_CONTAINER;

    faam_atom_ref udta = { 0, 0 }, meta_atom = { 0, 0 }, ilst = { 0, 0 };
    int found = faam_atom_find_child(io, moov.offset + 8, moov.offset + moov.size, "udta", &udta);
    if (found > 0)
        found = faam_atom_find_child(io, udta.offset + 8, udta.offset + udta.size, "meta", &meta_atom);

    if (meta_atom.size >= 12) {
        faam_atom_ref hdlr_atom = { 0, 0 };
        int hfound = faam_atom_find_child(io, meta_atom.offset + 12, meta_atom.offset + meta_atom.size, "hdlr", &hdlr_atom);
        if (hfound == -2) return FAAM_ERR_UNSUPPORTED;
        if (hfound > 0 && hdlr_atom.size >= 20) {
            uint8_t hhdr[4];
            if (!io->seek(io->user_data, hdlr_atom.offset + 16) || faam_io_read_full(io, hhdr, 4) != 4)
                return FAAM_ERR_IO_READ;
            if (!memcmp(hhdr, "mdta", 4))
                return FAAM_ERR_UNSUPPORTED;
        } else if (hfound < 0) {
            return hfound == -2 ? FAAM_ERR_UNSUPPORTED : FAAM_ERR_IO_READ;
        }
    }

    if (found > 0)
        found = faam_atom_find_child(io, meta_atom.offset + 12, meta_atom.offset + meta_atom.size, "ilst", &ilst);
    if (found == -2) return FAAM_ERR_UNSUPPORTED;
    if (found < 0) return FAAM_ERR_IO_READ;

    uint8_t *extra_buf = NULL;
    uint32_t extra_len = 0, extra_cap = 0;

    if (ilst.size >= 8 && !(flags & FAAM_METADATA_UPDATE_CLEAR)) {
        for (uint64_t pos = ilst.offset + 8; pos < ilst.offset + ilst.size;) {
            if (ilst.offset + ilst.size - pos < 8) { FreeMemory(extra_buf); return FAAM_ERR_BAD_CONTAINER; }
            uint8_t hdr[8];
            if (!io->seek(io->user_data, pos) || faam_io_read_full(io, hdr, 8) != 8) {
                FreeMemory(extra_buf); return FAAM_ERR_IO_READ;
            }
            uint32_t size = read_u32_be(hdr);
            if (size == 1) { FreeMemory(extra_buf); return FAAM_ERR_UNSUPPORTED; }
            if (size < 8 || size > ilst.offset + ilst.size - pos) { FreeMemory(extra_buf); return FAAM_ERR_BAD_CONTAINER; }

            uint8_t *atom = NULL;
            faam_status st = read_atom(io, pos, size, &atom);
            if (st != FAAM_OK) {
                FreeMemory(extra_buf); return st;
            }
            if (!faam_metadata_matches_atom(meta, atom, size))
                st = append_bytes(&extra_buf, &extra_len, &extra_cap, atom, size);
            FreeMemory(atom);
            if (st != FAAM_OK) {
                FreeMemory(extra_buf); return st;
            }

            pos += size;
        }
    }

    uint32_t ilst_len;
    faam_status build = faam_write_ilst(meta, NULL, extra_buf, extra_len, NULL, NULL, &ilst_len);
    if (build != FAAM_OK) { FreeMemory(extra_buf); return build; }
    /* The shared resizer cannot represent 1..7 leftover bytes as a sibling
     * free box. Keep ilst structurally valid by adding a small free child,
     * growing by eight bytes instead of absorbing stale bytes into ilst. */
    if (ilst.size > ilst_len && ilst.size - ilst_len < 8) {
        uint8_t padding[16] = {0};
        uint32_t length = 8 + (uint32_t)(ilst.size - ilst_len);
        write_u32_be(padding, length); memcpy(padding + 4, "free", 4);
        build = append_bytes(&extra_buf, &extra_len, &extra_cap, padding, length);
        if (build != FAAM_OK) { FreeMemory(extra_buf); return build; }
        if (ilst_len > UINT32_MAX - length) { FreeMemory(extra_buf); return FAAM_ERR_UNSUPPORTED; }
        ilst_len += length;
    }
    if (ilst_len > UINT32_MAX - MFULL_PREFIX) { FreeMemory(extra_buf); return FAAM_ERR_UNSUPPORTED; }
    /* A new meta needs its version/flags and handler ahead of the ilst; an existing
     * meta already has them, so the same buffer serves both by offset. */
    uint8_t *buf = (uint8_t *)AllocMemory(MFULL_PREFIX + ilst_len);
    if (!buf) { FreeMemory(extra_buf); return FAAM_ERR_NO_MEMORY; }
    uint8_t *ilst_buf = buf + MFULL_PREFIX;
    ilst_buffer buffer = {ilst_buf, 0};
    build = faam_write_ilst(meta, NULL, extra_buf, extra_len, append_ilst, &buffer, &ilst_len);
    FreeMemory(extra_buf);
    if (build != FAAM_OK) { FreeMemory(buf); return build; }

    uint64_t ancestors[3];
    faam_status rc;

    if (ilst.size >= 8) {
        ancestors[0] = meta_atom.offset;
        ancestors[1] = udta.offset;
        ancestors[2] = moov.offset;
        rc = faam_atom_resize(io, ilst.offset, ilst.size, ilst_buf, ilst_len,
                               ancestors, 3, &moov, &mdat, file_size);
    } else if (meta_atom.size >= 12) {
        /* No existing ilst: insert it as a new child at the end of meta's
         * current content instead of at meta_offset+12, so an existing hdlr
         * (or anything else already inside meta) isn't clobbered. */
        if (!faam_atom_children_fill(io, meta_atom.offset + 12, meta_atom.offset + meta_atom.size)) {
            FreeMemory(buf);
            return FAAM_ERR_BAD_CONTAINER;
        }
        ancestors[0] = meta_atom.offset;
        ancestors[1] = udta.offset;
        ancestors[2] = moov.offset;
        rc = faam_atom_resize(io, meta_atom.offset + meta_atom.size, 0, ilst_buf, ilst_len,
                               ancestors, 3, &moov, &mdat, file_size);
    } else {
        write_u32_be(buf, MFULL_PREFIX + ilst_len);
        memcpy(buf + 4, "meta", 4);
        write_u32_be(buf + 8, 0); /* version/flags */
        write_u32_be(buf + 12, 33);
        memcpy(buf + 16, "hdlr", 4);
        memset(buf + 20, 0, 8);
        memcpy(buf + 28, "mdirappl", 8);
        memset(buf + 36, 0, 9);
        rc = faam_atom_append_udta_child(io, &udta, &moov, &mdat, file_size, buf, MFULL_PREFIX + ilst_len);
    }

    FreeMemory(buf);
    return rc;
}
