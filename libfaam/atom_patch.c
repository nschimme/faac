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
 * Shared in-place MP4 atom resize logic for libfaam's stream-based tag/chapter
 * rewriters (tag.c, chapter.c).
 *
 * The tricky part isn't finding an atom -- it's growing one safely. Writing a
 * bigger payload at the same file offset silently overwrites whatever follows
 * it (more moov content, or mdat sample data). Fixing that requires shifting
 * every trailing byte forward, and, if moov precedes mdat in the file
 * (faststart layout), correcting every stco/co64 chunk offset since those are
 * absolute file positions that just moved.
 */

#include "endian.h"
#include "libfaam_internal.h"

#define FAAM_ATOM_TAIL_CHUNK 65536

/* 1: header read, 0: no complete header here (EOF or failed seek), -1: the read callback failed.
 * *wide is set for a 64-bit size header, whose size field is 16 bytes into the atom's start
 * rather than 8, so none of the in-place size arithmetic below applies to it. */
static int read_atom_hdr(const faam_io *io, uint64_t offset, char type[4], uint64_t *size, bool *wide) {
    uint8_t hdr[8];
    if (!io->seek(io->user_data, offset)) return 0;
    int32_t got = faam_io_read_full(io, hdr, 8);
    if (got != 8) return got < 0 ? -1 : 0;
    memcpy(type, hdr + 4, 4);
    uint32_t sz32 = read_u32_be(hdr);
    *wide = sz32 == 1;
    if (sz32 == 1) {
        uint8_t ext[8];
        got = faam_io_read_full(io, ext, 8);
        if (got != 8) return got < 0 ? -1 : 0;
        *size = read_u64_be(ext);
    } else {
        *size = sz32;
    }
    return 1;
}

faam_status faam_atom_scan_top(const faam_io *io, faam_atom_ref *moov, faam_atom_ref *mdat, uint64_t *file_size) {
    moov->offset = moov->size = 0;
    mdat->offset = mdat->size = 0;
    uint64_t pos = 0;
    for (;;) {
        char type[4];
        uint64_t size;
        bool wide;
        int r = read_atom_hdr(io, pos, type, &size, &wide);
        if (r < 0) return FAAM_ERR_IO_READ;
        if (!r || size < 8) break;
        if (size < (wide ? 16u : 8u) || size > UINT64_MAX - pos) return FAAM_ERR_BAD_CONTAINER;
        if (memcmp(type, "moov", 4) == 0) {
            /* Growing moov rewrites its 32-bit size field, which a wide header does not have. */
            if (wide) return FAAM_ERR_UNSUPPORTED;
            moov->offset = pos; moov->size = size;
        }
        else if (memcmp(type, "mdat", 4) == 0) { mdat->offset = pos; mdat->size = size; }
        pos += size;
    }
    *file_size = pos;
    return FAAM_OK;
}

int faam_atom_child_at(const faam_io *io, uint64_t pos, uint64_t end, char type[4], faam_atom_ref *out) {
    if (pos > end || end - pos < 8) return 0;
    uint64_t size;
    bool wide;
    int r = read_atom_hdr(io, pos, type, &size, &wide);
    if (r <= 0) return r;
    if (size < 8 || size > end - pos) return 0;
    if (wide) return -2;
    out->offset = pos;
    out->size = size;
    return 1;
}

int faam_atom_find_child(const faam_io *io, uint64_t range_start, uint64_t range_end, const char name[4], faam_atom_ref *out) {
    uint64_t pos = range_start;
    while (pos <= range_end && range_end - pos >= 8) {
        char type[4];
        uint64_t size;
        bool wide;
        int r = read_atom_hdr(io, pos, type, &size, &wide);
        if (r < 0) return -1;
        if (!r || size < 8 || size > range_end - pos) break;
        if (memcmp(type, name, 4) == 0) {
            if (wide) return -2;
            out->offset = pos;
            out->size = size;
            return 1;
        }
        pos += size;
    }
    return 0;
}

/* trak/mdia/minf/stbl are the only containers on the path down to stco/co64;
 * recursing into everything (e.g. stsd's opaque sample entries) risks
 * misparsing atom-shaped garbage in a leaf box's payload. */
static bool is_offset_table_container(const char type[4]) {
    static const char *const containers[] = { "trak", "mdia", "minf", "stbl" };
    for (size_t i = 0; i < sizeof(containers) / sizeof(containers[0]); i++) {
        if (memcmp(type, containers[i], 4) == 0) return true;
    }
    return false;
}

static bool shift_chunk_offsets(const faam_io *io, uint64_t box_offset, uint64_t box_size,
                               uint32_t header_size, uint32_t entry_size, int64_t delta,
                               uint64_t from, bool apply) {
    if (box_size < header_size + 8) return false;
    uint8_t hdr[8];
    if (!io->seek(io->user_data, box_offset + header_size) || faam_io_read_full(io, hdr, 8) != 8) return false;
    uint32_t count = read_u32_be(hdr + 4);
    if (count > (box_size - header_size - 8) / entry_size) return false;
    uint64_t entries_off = box_offset + header_size + 8;

    for (uint32_t i = 0; i < count; i++) {
        uint8_t buf[8];
        uint64_t off = entries_off + (uint64_t)i * entry_size;
        if (!io->seek(io->user_data, off) || faam_io_read_full(io, buf, entry_size) != (int32_t)entry_size) return false;
        if (!apply) continue;
        uint64_t val = (entry_size == 8) ? read_u64_be(buf) : read_u32_be(buf);
        /* Only data behind the insertion moved; an mdat ahead of it (moov last) stays put. */
        if (val < from) continue;
        /* Chunk offsets wrap modulo 2^64 like the rest of the file arithmetic; a signed add
         * on a corrupt offset would be undefined behavior. */
        val += (uint64_t)delta;
        if (entry_size == 8) write_u64_be(buf, val); else write_u32_be(buf, (uint32_t)val);
        if (!io->seek(io->user_data, off) || io->write(io->user_data, buf, entry_size) != (int32_t)entry_size) return false;
    }
    return true;
}

static bool fixup_chunk_offsets(const faam_io *io, uint64_t start, uint64_t end,
                                int64_t delta, uint64_t from, bool apply) {
    uint64_t pos = start;
    while (pos <= end && end - pos >= 8) {
        char type[4];
        uint64_t size;
        bool wide;
        if (read_atom_hdr(io, pos, type, &size, &wide) != 1 ||
            size < (wide ? 16u : 8u) || size > end - pos) return false;
        uint32_t header_size = wide ? 16u : 8u;

        if (memcmp(type, "stco", 4) == 0) {
            if (!shift_chunk_offsets(io, pos, size, header_size, 4, delta, from, apply)) return false;
        } else if (memcmp(type, "co64", 4) == 0) {
            if (!shift_chunk_offsets(io, pos, size, header_size, 8, delta, from, apply)) return false;
        } else if (is_offset_table_container(type)) {
            if (!fixup_chunk_offsets(io, pos + header_size, pos + size, delta, from, apply)) return false;
        }
        pos += size;
    }
    return true;
}

/* Fragments name their data by absolute file position when tfhd carries a base_data_offset
 * (ffmpeg's default) and mfra indexes every moof the same way. Growing moov moves all of
 * them, so each is moved with it; libfaam's own files use default-base-is-moof and need none.
 * The first pass only validates, so a layout it cannot follow is refused before anything is
 * written. */
static bool shift_fragment_offsets(const faam_io *io, uint64_t start, uint64_t file_size, int64_t delta, bool apply)
{
    uint64_t pos = start;
    while (file_size - pos >= 8) {
        char type[4];
        uint64_t size;
        bool wide;
        if (read_atom_hdr(io, pos, type, &size, &wide) != 1 || size < (wide ? 16u : 8u) || size > file_size - pos)
            return true; /* only moof and mfra matter; what follows an unreadable box is not ours to judge */
        uint32_t header = wide ? 16u : 8u;
        if (memcmp(type, "moof", 4) == 0) {
            for (uint64_t t = pos + header; file_size - t >= 8 && t < pos + size;) {
                char tt[4];
                uint64_t tsz;
                bool tw;
                if (read_atom_hdr(io, t, tt, &tsz, &tw) != 1 || tsz < 8 || tsz > pos + size - t) return false;
                char ft[4];
                uint64_t fsz;
                uint8_t b[12];
                if (memcmp(tt, "traf", 4) == 0 && read_atom_hdr(io, t + 8, ft, &fsz, &tw) == 1 && memcmp(ft, "tfhd", 4) == 0) {
                    if (fsz < 16 || !io->seek(io->user_data, t + 8 + 8) || faam_io_read_full(io, b, 4) != 4) return false;
                    if (b[3] & 1) {
                        uint64_t at = t + 8 + 16;
                        if (fsz < 24 || !io->seek(io->user_data, at) || faam_io_read_full(io, b, 8) != 8) return false;
                        write_u64_be(b, read_u64_be(b) + (uint64_t)delta);
                        if (apply && (!io->seek(io->user_data, at) || io->write(io->user_data, b, 8) != 8)) return false;
                    }
                }
                t += tsz;
            }
        } else if (memcmp(type, "mfra", 4) == 0) {
            for (uint64_t t = pos + header; file_size - t >= 8 && t < pos + size;) {
                char tt[4];
                uint64_t tsz;
                bool tw;
                if (read_atom_hdr(io, t, tt, &tsz, &tw) != 1 || tsz < 8 || tsz > pos + size - t) return false;
                if (memcmp(tt, "tfra", 4) == 0) {
                    uint8_t h[16];
                    if (tsz < 24 || !io->seek(io->user_data, t + 8) || faam_io_read_full(io, h, 16) != 16) return false;
                    uint32_t wide_times = h[0] == 1, lens = read_u32_be(h + 8), n = read_u32_be(h + 12);
                    uint32_t step = (wide_times ? 16 : 8) + ((lens >> 4 & 3) + 1) + ((lens >> 2 & 3) + 1) + ((lens & 3) + 1);
                    if (n > (tsz - 24) / step) return false;
                    for (uint32_t i = 0; i < n; i++) {
                        uint64_t at = t + 24 + (uint64_t)i * step + (wide_times ? 8 : 4);
                        uint8_t v[8];
                        uint32_t w = wide_times ? 8 : 4;
                        if (!io->seek(io->user_data, at) || faam_io_read_full(io, v, w) != (int32_t)w) return false;
                        if (wide_times) write_u64_be(v, read_u64_be(v) + (uint64_t)delta);
                        else write_u32_be(v, read_u32_be(v) + (uint32_t)delta);
                        if (apply && (!io->seek(io->user_data, at) || io->write(io->user_data, v, w) != (int32_t)w)) return false;
                    }
                }
                t += tsz;
            }
        }
        pos += size;
    }
    return true;
}

/* Moves [start, start+len) forward by delta through buf, from the end backward so a destination
 * that overlaps its source is only written after that source has been read. */
faam_status faam_io_move_forward(const faam_io *io, uint8_t *buf, uint32_t cap,
                                 uint64_t start, uint64_t len, uint64_t delta)
{
    for (uint64_t remaining = len; remaining;) {
        uint32_t chunk = (uint32_t)(remaining < cap ? remaining : cap);
        uint64_t src = start + (remaining - chunk);
        if (!io->seek(io->user_data, src) || faam_io_read_full(io, buf, chunk) != (int32_t)chunk)
            return FAAM_ERR_IO_READ;
        if (!io->seek(io->user_data, src + delta) || io->write(io->user_data, buf, chunk) != (int32_t)chunk)
            return FAAM_ERR_IO_WRITE;
        remaining -= chunk;
    }
    return FAAM_OK;
}

faam_status faam_atom_offsets_check(const faam_io *io, const faam_atom_ref *moov, const faam_atom_ref *mdat,
                                    uint64_t file_size)
{
    if (!moov || moov->size == 0) return FAAM_OK;
    if (mdat && mdat->size > 0 && mdat->offset >= moov->offset &&
        !fixup_chunk_offsets(io, moov->offset + 8, moov->offset + moov->size, 0, 0, false))
        return FAAM_ERR_BAD_CONTAINER;
    if (!shift_fragment_offsets(io, moov->offset + moov->size, file_size, 0, false)) return FAAM_ERR_BAD_CONTAINER;
    return FAAM_OK;
}

faam_status faam_atom_resize(const faam_io *io,
                              uint64_t atom_offset, uint64_t old_size,
                              const uint8_t *new_atom, uint32_t new_size,
                              const uint64_t *ancestor_offsets, int num_ancestors,
                              const faam_atom_ref *moov, const faam_atom_ref *mdat,
                              uint64_t file_size)
{
    if (new_size > INT32_MAX) return FAAM_ERR_INVALID_ARGUMENT; /* one write of it must fit the callback's int32_t */
    if (new_size <= old_size) {
        uint64_t diff = old_size - new_size;
        if (!io->seek(io->user_data, atom_offset)) return FAAM_ERR_IO_WRITE;
        if (io->write(io->user_data, new_atom, new_size) != (int32_t)new_size) return FAAM_ERR_IO_WRITE;

        if (diff >= 8) {
            uint8_t free_box[8];
            write_u32_be(free_box, (uint32_t)diff);
            memcpy(free_box + 4, "free", 4);
            if (io->write(io->user_data, free_box, 8) != 8) return FAAM_ERR_IO_WRITE;
        } else if (diff > 0) {
            /* Too little slack for a free atom; fold it into this atom's own
             * declared size instead (parsers skip the leftover trailing bytes). */
            uint8_t szbuf[4];
            write_u32_be(szbuf, (uint32_t)old_size);
            if (!io->seek(io->user_data, atom_offset) || io->write(io->user_data, szbuf, 4) != 4) return FAAM_ERR_IO_WRITE;
        }
        return FAAM_OK;
    }

    uint64_t delta = new_size - old_size;

    /* mdat only moves if it starts at/after the atom being grown. When mdat
     * comes first (the common non-faststart layout this project's own muxer
     * produces), nothing in moov's sample tables is affected. A file can hold
     * data on both sides (chapter samples trail moov), so offsets are shifted
     * one by one against the insertion point. */
    if (mdat && mdat->size > 0 && mdat->offset >= atom_offset && moov && moov->size > 0) {
        /* Validate every table and box before writing any offset, so a later bad
         * layout cannot leave earlier chunk offsets partially adjusted. */
        uint64_t from = atom_offset + old_size;
        if (!fixup_chunk_offsets(io, moov->offset + 8, moov->offset + moov->size,
                                 (int64_t)delta, from, false))
            return FAAM_ERR_BAD_CONTAINER;
        if (!fixup_chunk_offsets(io, moov->offset + 8, moov->offset + moov->size,
                                 (int64_t)delta, from, true))
            return FAAM_ERR_IO_WRITE;
    }

    if (moov && moov->size > 0 && atom_offset >= moov->offset && atom_offset <= moov->offset + moov->size) {
        uint64_t frags = moov->offset + moov->size;
        if (!shift_fragment_offsets(io, frags, file_size, (int64_t)delta, false))
            return FAAM_ERR_BAD_CONTAINER;
        if (!shift_fragment_offsets(io, frags, file_size, (int64_t)delta, true))
            return FAAM_ERR_IO_WRITE;
    }

    uint64_t tail_start = atom_offset + old_size;
    uint64_t tail_len = (file_size > tail_start) ? (file_size - tail_start) : 0;
    if (tail_len > 0) {
        uint8_t *buf = (uint8_t *)AllocMemory(FAAM_ATOM_TAIL_CHUNK);
        if (!buf) return FAAM_ERR_NO_MEMORY;
        faam_status st = faam_io_move_forward(io, buf, FAAM_ATOM_TAIL_CHUNK, tail_start, tail_len, delta);
        FreeMemory(buf);
        if (st != FAAM_OK) return st;
    }

    /* Safe now: the tail has already been relocated past atom_offset+new_size. */
    if (!io->seek(io->user_data, atom_offset) || io->write(io->user_data, new_atom, new_size) != (int32_t)new_size)
        return FAAM_ERR_IO_WRITE;

    for (int i = 0; i < num_ancestors; i++) {
        uint8_t hdr[4];
        if (!io->seek(io->user_data, ancestor_offsets[i]) || faam_io_read_full(io, hdr, 4) != 4)
            return FAAM_ERR_IO_READ;
        write_u32_be(hdr, read_u32_be(hdr) + (uint32_t)delta);
        if (!io->seek(io->user_data, ancestor_offsets[i]) || io->write(io->user_data, hdr, 4) != 4)
            return FAAM_ERR_IO_WRITE;
    }

    return FAAM_OK;
}

/* A new child goes at the end of its container, so the children already there must end
 * exactly where the container does; one that overruns would take the insertion into
 * its own content. */
bool faam_atom_children_fill(const faam_io *io, uint64_t pos, uint64_t end)
{
    while (pos < end) {
        uint8_t hdr[8];
        if (end - pos < 8 || !io->seek(io->user_data, pos) || faam_io_read_full(io, hdr, 8) != 8) return false;
        uint32_t size = read_u32_be(hdr);
        /* A size of 0 ("to the end") leaves nowhere after it to append. */
        if (size < 8 || size > end - pos) return false;
        pos += size;
    }
    return pos == end;
}

faam_status faam_atom_append_udta_child(const faam_io *io, const faam_atom_ref *udta,
                                        const faam_atom_ref *moov, const faam_atom_ref *mdat,
                                        uint64_t file_size, const uint8_t *child, uint32_t child_len)
{
    const faam_atom_ref *parent = udta->size >= 8 ? udta : moov;
    if (!faam_atom_children_fill(io, parent->offset + 8, parent->offset + parent->size))
        return FAAM_ERR_BAD_CONTAINER;
    if (udta->size >= 8) {
        uint64_t ancestors[2] = { udta->offset, moov->offset };
        return faam_atom_resize(io, udta->offset + udta->size, 0, child, child_len,
                                ancestors, 2, moov, mdat, file_size);
    }
    /* Other muxers omit udta when there is nothing to put in it. */
    if (child_len > UINT32_MAX - 8) return FAAM_ERR_UNSUPPORTED;
    uint32_t wrap_len = 8 + child_len;
    uint8_t *wrap = (uint8_t *)AllocMemory(wrap_len);
    if (!wrap) return FAAM_ERR_NO_MEMORY;
    write_u32_be(wrap, wrap_len);
    memcpy(wrap + 4, "udta", 4);
    memcpy(wrap + 8, child, child_len);
    uint64_t ancestors[1] = { moov->offset };
    faam_status rc = faam_atom_resize(io, moov->offset + moov->size, 0, wrap, wrap_len,
                                      ancestors, 1, moov, mdat, file_size);
    FreeMemory(wrap);
    return rc;
}
