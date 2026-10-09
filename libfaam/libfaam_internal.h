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
 * Internal header for libfaam
 */

#ifndef LIBFAAM_INTERNAL_H
#define LIBFAAM_INTERNAL_H

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "faam.h"

/* Memory management macros; ports override them with -D or a force-included
 * header (embedded PSRAM / fast internal SRAM) */
#ifndef AllocMemory
#define AllocMemory(size) malloc(size)
#endif
#ifndef FreeMemory
#define FreeMemory(block) free(block)
#endif
#ifndef AllocMemoryFast
#define AllocMemoryFast(size) malloc(size)
#endif
#ifndef FreeMemoryFast
#define FreeMemoryFast(block) free(block)
#endif
#ifndef ReallocMemory
#define ReallocMemory(block, size) realloc(block, size)
#endif

#define FAAM_MAX_TRACKS 8
#define FAAM_CODEC_INLINE 256 /* codec config bytes a muxer track holds inline */

typedef struct {
    uint64_t offset;
    uint32_t size;
    uint32_t duration;
    int32_t cts_offset;
    bool is_keyframe;
} faam_sample;

typedef struct {
    uint32_t count;
    uint32_t delta;
} faam_stts_entry;

typedef struct {
    uint32_t count;
    int32_t offset;
} faam_ctts_entry;

typedef struct {
    faam_track_info info;
    uint8_t *codec_data;     /* heap, exact size */
    uint32_t codec_data_len;
    faam_sample *samples;
    uint32_t total_frames;   /* in the loaded table: the whole file, or the current fragment */
    uint32_t current_frame;
    uint32_t samples_cap;
    uint64_t elst_media_time;
    uint64_t elst_segment_duration;
    uint64_t media_duration; /* mdhd duration in track timescale units */
    bool has_elst;
} faam_demuxer_track;

typedef struct faam_owned_string {
    struct faam_owned_string *next;
    char text[];
} faam_owned_string;

typedef struct {
    uint32_t track_id;
    uint32_t duration;
    uint32_t size;
    uint32_t flags;
} faam_trex;

struct faam_demuxer {
    bool heap_owned;
    faam_status error;
    faam_owned_string *strings;
    faam_custom_tag *custom_tags;
    faam_io io;

    faam_gapless_info gapless;
    bool has_gapless;
    char major_brand[4]; /* ftyp major brand, all zero without an ftyp */

    uint64_t elst_media_time;
    uint64_t elst_segment_duration;
    bool has_elst;

    faam_metadata metadata;
    uint8_t *cover_art_owned; /* heap copy backing metadata.cover_art; the ilst-source
                                * buffer it was parsed from is freed right after init() */
    faam_chapter *chapters;  /* heap, sized by the chpl atom (at most 255) */
    uint32_t num_chapters;

    faam_demuxer_track tracks[FAAM_MAX_TRACKS];
    uint32_t num_tracks;
    uint32_t total_tracks;   /* trak boxes in the file, num_tracks of them held */
    uint32_t movie_timescale;

    /* Fragmented files: samples arrive one moof at a time, so only the
     * current fragment's table is in memory. */
    bool fragmented;
    bool frag_done;
    uint64_t frag_pos;       /* next top-level box to scan for a moof */
    uint64_t mehd_duration;  /* movie timescale; 0 when the writer never finalized */
    faam_trex trex[FAAM_MAX_TRACKS];
    uint32_t num_trex;
};

typedef struct {
    faam_track_config cfg;
    faam_gapless_info gapless; /* copy of *cfg.gapless */
    bool has_gapless;
    uint8_t codec_data[FAAM_CODEC_INLINE]; /* Annex-B avcC capture and every config that fits */
    uint8_t *codec_ext;      /* codec_data_len > FAAM_CODEC_INLINE: arena (fragmented) or heap */
    bool codec_ext_heap;
    uint32_t codec_data_len;
    uint32_t sps_len; /* Annex-B avcC capture: SPS parked in codec_data until the PPS arrives */

    uint32_t *sample_sizes;
    uint64_t *sample_offsets;
    uint32_t sample_count;
    uint32_t sample_capacity;

    faam_stts_entry *stts_entries;
    uint32_t stts_count;
    uint32_t stts_capacity;

    uint32_t *stss_entries; /* 1-based sample index of keyframes */
    uint32_t stss_count;
    uint32_t stss_capacity;

    /* Run-length ctts, allocated only once a non-zero offset arrives so
     * I/P-only streams cost nothing. */
    faam_ctts_entry *ctts_entries;
    uint32_t ctts_count;
    uint32_t ctts_capacity;
    int64_t min_pts;
    bool cts_negative;

    uint32_t max_frame_size;
    uint32_t max_bitrate;
    uint32_t avg_bitrate;
    struct {
        uint32_t max;
        uint32_t avg;
        uint64_t size;
        uint64_t samples;
    } bitrate_window;
    uint32_t last_frame_samples;
    uint16_t audio_sample_size;
    uint64_t total_bytes;
    uint64_t window_ticks;
    uint64_t frag_dts0; /* decode time at the start of the open fragment (tfdt) */
} faam_muxer_track;

typedef struct {
    uint32_t creation_time;
    bool constant_rate;
    bool is_m4b;
    faam_gapless_info gapless;
    const faam_metadata *metadata; /* the caller's, read when the ilst is written */
    const faam_chapter *chapters;
    uint32_t num_chapters;
} faam_muxer_settings;

struct faam_muxer {
    faam_io io;

    faam_muxer_settings cfg;
    uint32_t num_tracks;

    uint64_t mdat_pos;
    uint64_t mdat_size;

    uint8_t staging[1024];
    uint32_t staged_bytes;
    faam_status error;
    bool gapless_present;
    bool heap_owned;
    bool finalized;
    uint64_t file_bytes;

    /* Fragmented mode (fragment_ms > 0). The per-fragment sample index lives
     * in the caller's arena right behind tracks[], so nothing is allocated. */
    bool fragmented;
    bool frag_open;
    bool has_video;
    uint32_t fragment_ms;
    uint32_t frag_cap;       /* index entries */
    uint32_t frag_count;
    uint32_t frag_bytes;     /* mdat payload so far */
    uint32_t frag_reserve;   /* bytes held back ahead of mdat for the moof */
    uint32_t frag_seq;
    uint64_t frag_moof_pos;
    uint64_t mehd_pos;
    uint32_t *fi_off;        /* payload offset of each indexed sample */
    uint32_t *fi_dur;
    int32_t *fi_cts;         /* NULL unless the file has a video track */
    uint8_t *fi_flags;       /* bit 0: sync sample */
    uint8_t *fi_track;       /* index into tracks[] */
    faam_muxer_track tracks[];
};

typedef faam_status (*faam_bytes_writer)(void *user, const void *data, uint32_t bytes);
faam_status faam_write_ilst(const faam_metadata *metadata, const faam_gapless_info *gapless,
                            const uint8_t *smpb, uint32_t smpb_bytes,
                            faam_bytes_writer write, void *user, uint32_t *out_size);
faam_status faam_write_ilst_ext(const faam_metadata *metadata, const faam_gapless_info *gapless,
                                const uint8_t *smpb, uint32_t smpb_bytes,
                                const uint8_t *extra, uint32_t extra_bytes,
                                faam_bytes_writer write, void *user, uint32_t *out_size);

/* Endian utilities */
#include "endian.h"

/* Input structs from a caller: zero a library-sized copy, then take the bytes both sides know.
 * The caller's struct_size must be at least the baseline of the struct (checked by the caller). */
static inline void faam_copy_in(void *dst, size_t dst_size, const void *src, uint32_t src_size) {
    memset(dst, 0, dst_size);
    memcpy(dst, src, src_size < dst_size ? src_size : dst_size);
}

/* Output structs to a caller: write at most the caller's struct_size bytes. */
static inline void faam_copy_out(void *dst, uint32_t dst_size, const void *src, size_t src_size) {
    memcpy(dst, src, dst_size < src_size ? dst_size : src_size);
    memcpy(dst, &dst_size, sizeof(dst_size)); /* struct_size stays the caller's */
}

/* Element i of a caller's array of chapters or custom tags, strided by the first element's size. */
static inline const faam_chapter *faam_chapter_at(const faam_chapter *base, uint32_t i) {
    return (const faam_chapter *)((const uint8_t *)base + (size_t)i * base->struct_size);
}
static inline const faam_custom_tag *faam_custom_tag_at(const faam_custom_tag *base, uint32_t i) {
    return (const faam_custom_tag *)((const uint8_t *)base + (size_t)i * base->struct_size);
}

/* The library's copy of a caller's io; false when the caller's struct is below the baseline. */
static inline bool faam_io_load(faam_io *dst, const faam_io *src) {
    if (src->struct_size < FAAM_IO_BASELINE) return false;
    faam_copy_in(dst, sizeof(*dst), src, src->struct_size);
    dst->struct_size = sizeof(*dst);
    return true;
}

/* Reads until n bytes arrived, the source reports EOF (0) or fails. Returns the byte
 * count, or -1 when the read callback returned a negative value; sources may deliver
 * fewer bytes per call than asked. */
int32_t faam_io_read_full(const faam_io *io, void *buf, uint32_t n);

/* Shared atom-tree location/resize primitives (atom_patch.c), used by both
 * tag.c and chapter.c's in-place mp4 rewriters so the tail-shift / stco
 * correction logic that makes growing an atom safe exists in exactly one
 * place. */
typedef struct {
    uint64_t offset; /* absolute file offset of the atom's size field */
    uint64_t size;   /* atom's stated size (0 if not found) */
} faam_atom_ref;

/* Walks top-level boxes from file start via seek/read (no whole-file
 * buffering, no size cap) until a short read marks EOF. Fills moov and mdat
 * refs (size left 0 if absent) and *file_size with the resulting EOF offset.
 * FAAM_ERR_IO_READ when the read callback fails, FAAM_ERR_UNSUPPORTED for a moov with a
 * 64-bit size header. */
faam_status faam_atom_scan_top(const faam_io *io, faam_atom_ref *moov, faam_atom_ref *mdat, uint64_t *file_size);

/* Finds the first direct child box named `name` within byte range
 * [range_start, range_end) -- pass range_start/range_end as the container's
 * *content* bounds (i.e. already past its own box header, and past the
 * extra 4-byte version/flags for full boxes like "meta").
 * Returns 1 if found, 0 if absent, -1 when the read callback fails, -2 when the box
 * has a 64-bit size header (it cannot be resized in place). */
int faam_atom_find_child(const faam_io *io, uint64_t range_start, uint64_t range_end, const char name[4], faam_atom_ref *out);

/* Replaces the atom at [atom_offset, atom_offset+old_size) with new_atom[0..new_size)
 * (old_size == 0 means "insert a new child at atom_offset", used when the
 * target atom doesn't exist yet).
 *
 * If new_size <= old_size, patches in place and pads the leftover space with
 * a "free" atom (or, if the leftover is under 8 bytes, folds it into the
 * atom's own declared size).
 *
 * If new_size > old_size, this shifts every file byte from the old atom's
 * end through EOF forward by the size delta (chunked, back-to-front, so
 * source/destination ranges never unsafely overlap), bumps every ancestor's
 * 32-bit size field in `ancestor_offsets`, and -- only when `mdat` lies
 * *after* atom_offset (moov-before-mdat / faststart-style layout, where
 * growing something inside moov physically relocates mdat) -- walks moov's
 * trak/mdia/minf/stbl boxes and adds the delta to every stco/co64 chunk
 * offset, since those are absolute file offsets into mdat.
 */
faam_status faam_atom_resize(const faam_io *io,
                              uint64_t atom_offset, uint64_t old_size,
                              const uint8_t *new_atom, uint32_t new_size,
                              const uint64_t *ancestor_offsets, int num_ancestors,
                              const faam_atom_ref *moov, const faam_atom_ref *mdat,
                              uint64_t file_size);

/* Appends a complete atom to moov's udta, creating the udta (inside moov) when the
 * file has none. `udta` is the located atom (size 0 if absent). */
faam_status faam_atom_append_udta_child(const faam_io *io, const faam_atom_ref *udta,
                                        const faam_atom_ref *moov, const faam_atom_ref *mdat,
                                        uint64_t file_size, const uint8_t *child, uint32_t child_len);

/* True when the boxes in [pos, end) tile it exactly, so a child appended at `end` cannot land
 * inside one that overruns its container. */
bool faam_atom_children_fill(const faam_io *io, uint64_t pos, uint64_t end);

#endif /* LIBFAAM_INTERNAL_H */
