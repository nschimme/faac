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
#include "faac_endian.h"

typedef struct {
    uint64_t offset;
    uint32_t size;
    uint32_t duration;
} faam_sample;

typedef struct {
    uint32_t count;
    uint32_t delta;
} faam_stts_entry;

struct faam_demuxer {
    faam_io io;

    faam_asc_info asc_info;
    uint8_t asc_buf[64];
    uint32_t asc_len;

    faam_gapless_info gapless;
    bool has_gapless;

    /* Raw edts/elst edit-list entry, used as a gapless fallback (see
     * faam_parse_stream()) when no iTunSMPB tag is present -- e.g. files
     * produced by non-Apple encoders/muxers that only write the
     * standards-based edit list. */
    uint64_t elst_media_time;
    uint64_t elst_segment_duration;
    bool has_elst;

    faam_metadata metadata;
    faam_chapter chapters[64];
    uint32_t num_chapters;

    uint32_t sample_rate;
    uint32_t num_channels;
    uint64_t total_samples;
    uint32_t timescale;       /* mdhd: audio track's own (media) timescale */
    uint32_t movie_timescale; /* mvhd: movie timescale that elst segment_duration is expressed in */

    faam_sample *samples;
    uint32_t total_frames;
    uint32_t current_frame;

    uint64_t mdat_start_offset;
};

struct faam_muxer {
    faam_io io;

    faam_muxer_config cfg;
    uint8_t asc_buf[64];
    uint32_t asc_len;

    uint32_t sample_rate;
    uint32_t num_channels;
    uint32_t bits_per_sample;

    uint32_t frame_count;
    uint64_t sample_count;
    uint32_t max_bitrate;
    uint32_t avg_bitrate;
    uint16_t max_frame_size;

    uint64_t mdat_pos;
    uint64_t mdat_size;

    faam_sample *samples;
    uint32_t sample_capacity;

    faam_stts_entry *stts_entries;
    uint32_t stts_count;
    uint32_t stts_capacity;

    struct {
        uint32_t max;
        uint32_t avg;
        uint64_t size;
        uint64_t samples;
    } bitrate_window;
    uint32_t last_frame_samples;

    uint8_t *membuf;
    size_t mempos;
    size_t memcap;
    int mem_error;
};


#endif /* LIBFAAM_INTERNAL_H */
