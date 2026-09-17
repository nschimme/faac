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

/* Byte swap & Endian utilities */
#if defined(__has_builtin)
#  if __has_builtin(__builtin_bswap16) && __has_builtin(__builtin_bswap32) && __has_builtin(__builtin_bswap64)
#    define FAAM_BSWAP16 __builtin_bswap16
#    define FAAM_BSWAP32 __builtin_bswap32
#    define FAAM_BSWAP64 __builtin_bswap64
#  endif
#elif defined(__GNUC__)
#  define FAAM_BSWAP16 __builtin_bswap16
#  define FAAM_BSWAP32 __builtin_bswap32
#  define FAAM_BSWAP64 __builtin_bswap64
#elif defined(_MSC_VER)
#  define FAAM_BSWAP16 _byteswap_ushort
#  define FAAM_BSWAP32 _byteswap_ulong
#  define FAAM_BSWAP64 _byteswap_uint64
#endif

#ifndef FAAM_BSWAP16
static inline uint16_t FAAM_BSWAP16(uint16_t x) {
    return (uint16_t)((x >> 8) | (x << 8));
}
#endif

#ifndef FAAM_BSWAP32
static inline uint32_t FAAM_BSWAP32(uint32_t x) {
    return (x >> 24) | ((x >> 8) & 0xff00) | ((x << 8) & 0xff0000) | (x << 24);
}
#endif

#ifndef FAAM_BSWAP64
static inline uint64_t FAAM_BSWAP64(uint64_t x) {
    return ((x >> 56) & 0x00000000000000FFULL) |
           ((x >> 40) & 0x000000000000FF00ULL) |
           ((x >> 24) & 0x0000000000FF0000ULL) |
           ((x >> 8)  & 0x00000000FF000000ULL) |
           ((x << 8)  & 0x000000FF00000000ULL) |
           ((x << 24) & 0x0000FF0000000000ULL) |
           ((x << 40) & 0x00FF000000000000ULL) |
           ((x << 56) & 0xFF00000000000000ULL);
}
#endif

#if (defined(WORDS_BIGENDIAN) && WORDS_BIGENDIAN) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#  define FAAM_IS_BIG_ENDIAN 1
#else
#  define FAAM_IS_BIG_ENDIAN 0
#endif

static inline uint16_t read_u16_be(const uint8_t *b) {
#if FAAM_IS_BIG_ENDIAN
    uint16_t v;
    memcpy(&v, b, sizeof(v));
    return v;
#else
    uint16_t v;
    memcpy(&v, b, sizeof(v));
    return FAAM_BSWAP16(v);
#endif
}

static inline uint32_t read_u32_be(const uint8_t *b) {
#if FAAM_IS_BIG_ENDIAN
    uint32_t v;
    memcpy(&v, b, sizeof(v));
    return v;
#else
    uint32_t v;
    memcpy(&v, b, sizeof(v));
    return FAAM_BSWAP32(v);
#endif
}

static inline uint64_t read_u64_be(const uint8_t *b) {
#if FAAM_IS_BIG_ENDIAN
    uint64_t v;
    memcpy(&v, b, sizeof(v));
    return v;
#else
    uint64_t v;
    memcpy(&v, b, sizeof(v));
    return FAAM_BSWAP64(v);
#endif
}

static inline void write_u16_be(uint8_t *b, uint16_t val) {
#if FAAM_IS_BIG_ENDIAN
    memcpy(b, &val, sizeof(val));
#else
    uint16_t v = FAAM_BSWAP16(val);
    memcpy(b, &v, sizeof(v));
#endif
}

static inline void write_u32_be(uint8_t *b, uint32_t val) {
#if FAAM_IS_BIG_ENDIAN
    memcpy(b, &val, sizeof(val));
#else
    uint32_t v = FAAM_BSWAP32(val);
    memcpy(b, &v, sizeof(v));
#endif
}

static inline void write_u64_be(uint8_t *b, uint64_t val) {
#if FAAM_IS_BIG_ENDIAN
    memcpy(b, &val, sizeof(val));
#else
    uint64_t v = FAAM_BSWAP64(val);
    memcpy(b, &v, sizeof(v));
#endif
}

#endif /* LIBFAAM_INTERNAL_H */
