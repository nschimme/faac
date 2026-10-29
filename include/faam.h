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
 * libfaam container API.
 *
 * Lightweight ISO BMFF (MP4/M4A/M4B) parser and builder. It operates strictly
 * over abstract faam_io stream callbacks (no file path dependency) and supports
 * video (H.264/AVC, H.265/HEVC) and audio (AAC) tracks, metadata, and chapters.
 *
 * Allocation uses overridable AllocMemory / FreeMemory macros in source builds.
 * Open owns all instance allocations; close releases them.
 *
 * Design summary:
 *   - Every struct the caller fills or receives starts with uint32_t struct_size.
 *     Callers set it to sizeof of their struct; the library accepts any size at or
 *     above the FAAM_*_BASELINE of that struct, reads only the fields that size
 *     covers and, for output structs, writes at most struct_size bytes and
 *     reports the bytes populated in struct_size. New fields are appended at or
 *     after the previous sizeof(struct); compiler padding is never reused, and
 *     appended members sit at offsets that are multiples of their size,
 *     in blocks of 8 bytes (faam_demuxer_config is 4-byte aligned: 4-byte
 *     members only). Callers check library
 *     support through the struct_size the library writes back (init functions
 *     store min(caller, library)). Absent input fields take their zero value. Baseline sizes,
 *     enum values, signatures and offsets of released fields stay fixed within
 *     a major version.
 *   - No struct is embedded in another and none holds an array of structs (fixed char
 *     arrays aside). Input lists (tracks, chapters, metadata entries) are a pointer and
 *     a count, strided by the struct_size of the first element, so a list is one array
 *     of one struct size. Output lists are a count getter plus a getter by index.
 *   - Every open call takes a config struct (the demuxer's may be NULL for
 *     defaults), so a knob can be added later without changing a signature.
 *   - Compile-time options (muxer-video, muxer-fragmented) change what a call
 *     accepts, never a layout; faam_get_library_info() reports them.
 *   - Independent handles may be used concurrently; one handle is owned by one thread
 *     at a time. The library has no global mutable state.
 *
 * Status contract:
 *   - A NULL pointer, NULL io or out parameter, or an otherwise invalid
 *     argument returns FAAM_ERR_INVALID_ARGUMENT.
 *   - A callback the operation needs but the io leaves NULL returns
 *     FAAM_ERR_UNSUPPORTED, as does content or a call the library cannot handle; a
 *     feature compiled out of this build returns FAAM_ERR_NOT_BUILT. Each function
 *     below says which callbacks it needs; they are checked before any byte is read
 *     or written.
 *   - An allocation that failed returns
 *     FAAM_ERR_NO_MEMORY.
 *   - On failure open functions set *out to NULL
 *     before anything else, so the pointer is always safe to pass to close.
 *
 * I/O contract (faam_io):
 *   - read may deliver fewer bytes than asked; the library calls it again
 *     until the count is met. 0 means end of data. A negative value, or one
 *     larger than asked, is a failure and reports FAAM_ERR_IO_READ, from the
 *     demuxer and from the in-place tag/chapter updates alike (the demuxer
 *     treats 0 in the middle of a structure as a truncated file instead: it
 *     then opens with whatever it parsed, possibly no tracks).
 *   - write must return exactly the requested byte count. Anything else,
 *     fewer bytes, a negative value or more than asked, is
 *     FAAM_ERR_IO_WRITE; the muxer's error is sticky and every later call
 *     returns it.
 *   - seek returns false on failure; the muxer reports that as FAAM_ERR_IO_WRITE,
 *     sticky like a failed write (FAAM_ERR_UNSUPPORTED means no seek callback at
 *     all). tell returns the current offset.
 */

#ifndef FAAM_H
#define FAAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define FAAM_VERSION_MAJOR 3
#define FAAM_VERSION_MINOR 0
#define FAAM_VERSION_PATCH 0
#define FAAM_STR_(x) #x
#define FAAM_STR(x) FAAM_STR_(x)
#define FAAM_VERSION_STRING \
    FAAM_STR(FAAM_VERSION_MAJOR) "." FAAM_STR(FAAM_VERSION_MINOR) "." FAAM_STR(FAAM_VERSION_PATCH)
#define FAAM_VERSION_HEX \
    ((FAAM_VERSION_MAJOR << 16) | (FAAM_VERSION_MINOR << 8) | FAAM_VERSION_PATCH)

/* Windows exports from the DLL build only (FAAM_BUILDING). No dllimport: the API
 * has no data, so DLL and static consumers link the same way with no define. */
#ifndef FAAMAPI
# if defined(_WIN32)
#  ifdef FAAM_BUILDING
#   define FAAMAPI __declspec(dllexport)
#  else
#   define FAAMAPI
#  endif
# elif defined(__GNUC__) && (__GNUC__ >= 4)
#  define FAAMAPI __attribute__((visibility("default")))
# else
#  define FAAMAPI
# endif
#endif

typedef struct faam_demuxer faam_demuxer;
typedef struct faam_muxer   faam_muxer;

/*
 * Result codes. All values are negative except FAAM_OK and FAAM_END_OF_STREAM, so a
 * caller can test `status < 0` for failure. FAAM_END_OF_STREAM (+1) is not an error.
 * The FAAM_STATUS_MAX sentinel pins the enum to a 32-bit underlying type for a stable
 * ABI under -fshort-enums.
 */
typedef enum faam_status {
    FAAM_OK                   = 0,
    FAAM_ERR_INVALID_ARGUMENT = -1,
    FAAM_ERR_UNSUPPORTED      = -2, /* A required io callback is NULL, or the call or content is not supported */
    FAAM_ERR_NO_MEMORY        = -3, /* An allocation failed */
    FAAM_ERR_OUTPUT_TOO_SMALL = -4, /* *out_len reports the required output capacity */
    FAAM_ERR_IO_READ          = -5, /* A read callback failed or a read ended short of data the caller needs */
    FAAM_ERR_IO_WRITE         = -6, /* I/O write failure */
    FAAM_ERR_BAD_CONTAINER    = -7, /* Invalid MP4 atom structure or missing track */
    FAAM_ERR_NO_TRACK         = -8, /* No matching video/audio track found */
    FAAM_ERR_NOT_BUILT        = -9, /* The feature is compiled out of this build (see faam_library_info.flags) */
    FAAM_END_OF_STREAM        = 1, /* Not an error: no more frames */
    FAAM_STATUS_MAX           = 0x7fffffff
} faam_status;

/* Offset just past the named member: the smallest struct size the library accepts. */
#define FAAM_STRUCT_END(type, member) (offsetof(type, member) + sizeof(((type *)0)->member))

/* Public flag constants are enums; combinable masks retain fixed-width storage.
 * MAX sentinels pin enum widths and are not valid flags or cover types. */

/* faam_library_info.flags */
enum faam_library_flag {
    FAAM_LIB_VIDEO = 1u << 0, /* muxer-video: H.264/H.265 tracks, Annex-B, B-frame offsets */
    FAAM_LIB_FRAGMENTED = 1u << 1, /* muxer-fragmented: fragmented MP4 recording and demuxing */
    FAAM_LIB_FLAG_MAX = 0x7fffffff
};

/* Global library metadata; set struct_size to sizeof(faam_library_info) before
 * the call; it is updated to the bytes populated. version/copyright are
 * library-owned; do not free them. */
typedef struct faam_library_info {
    uint32_t                struct_size;

    const char             *version;      /* library version string   (library-owned) */
    const char             *copyright;    /* library copyright string (library-owned) */

    uint32_t                max_tracks;   /* Tracks one muxer writes or one demuxer holds */
    uint32_t                flags;        /* FAAM_LIB_* bits of this build */
} faam_library_info;
#define FAAM_LIBRARY_INFO_BASELINE FAAM_STRUCT_END(faam_library_info, flags)

FAAMAPI faam_status faam_get_library_info(faam_library_info *out_info);

typedef enum faam_track_type {
    FAAM_TRACK_AUDIO = 1,
    FAAM_TRACK_VIDEO = 2,
    FAAM_TRACK_OTHER = 3,         /* Demuxer only: text, timecode, metadata and any other handler */
    FAAM_TRACK_MAX = 0x7fffffff
} faam_track_type;

typedef enum faam_codec_id {
    FAAM_CODEC_GENERIC = 0,       /* Demuxer only: a sample entry libfaam does not know (see faam_track_info.fourcc); the muxer rejects it */
    FAAM_CODEC_AAC     = 1,
    FAAM_CODEC_H264    = 2,
    FAAM_CODEC_H265    = 3,
    FAAM_CODEC_MAX     = 0x7fffffff
} faam_codec_id;

/* Abstract Stream I/O for embedded platforms (SPIFFS, SDMMC, RAM, network). Initialize with
 * faam_io_init(); a missing optional callback is NULL. The callbacks return a signed 32-bit
 * count, so the library never asks one read or write for more than INT32_MAX bytes: a caller
 * input that would need that is refused with FAAM_ERR_INVALID_ARGUMENT before any I/O. */
typedef struct faam_io {
    uint32_t struct_size;
    void    *user_data;
    int32_t (*read)(void *user_data, void *buf, uint32_t bytes_to_read);
    int32_t (*write)(void *user_data, const void *buf, uint32_t bytes_to_write);
    bool    (*seek)(void *user_data, uint64_t offset);
    uint64_t(*tell)(void *user_data);
    bool    (*flush)(void *user_data); /* Optional; false reports an I/O failure */
} faam_io;
#define FAAM_IO_BASELINE FAAM_STRUCT_END(faam_io, flush)

/* Zero-initialize *io and stamp struct_size with min(caller_size, sizeof). Pass sizeof(*io),
 * then set the callbacks. */
FAAMAPI faam_status faam_io_init(faam_io *io, uint32_t caller_size);

/* Gapless audio parameters (iTunSMPB atom and the audio track's edit list), in samples
 * of the audio track's timescale (for HE-AAC the core rate: one frame is 1024).
 * Progressive muxer: when total_samples is 0 and a delay or padding is set, finalize
 * derives it as the track's total sample duration minus delay and padding; if that is
 * not positive the programme is empty and the edit list gets a zero segment length. The
 * edit list needs encoder_delay > 0. It is
 * written, identical, into every audio track unless a track config carries its own. A
 * fragmented muxer writes it only when total_samples is given at open. The demuxer fills
 * this from iTunSMPB, else from the edit list of the first audio track that has one
 * (media time, segment length and the media past it); the getters report all zeros when
 * neither exists. */
typedef struct faam_gapless_info {
    uint32_t struct_size;
    uint32_t encoder_delay;    /* Leading priming samples to discard; defaults to 0, caller supplies codec delay */
    uint32_t end_padding;      /* Trailing zero-padding samples to discard */
    uint64_t total_samples;    /* Original unpadded PCM sample count, 0 if unknown */
} faam_gapless_info;
#define FAAM_GAPLESS_INFO_BASELINE FAAM_STRUCT_END(faam_gapless_info, total_samples)

/* Chapter Metadata Entry */
typedef struct faam_chapter {
    uint32_t struct_size;
    uint64_t start_ms;         /* Chapter start time in milliseconds */
    const char *title;         /* Must not be NULL; at most 255 bytes are stored. Borrowed through
                                * finalize/update; demux output lives until close */
} faam_chapter;
#define FAAM_CHAPTER_BASELINE FAAM_STRUCT_END(faam_chapter, title)

/* faam_track_config.flags */
enum faam_track_flag {
    FAAM_TRACK_ANNEXB = 0x1, /* Video frames arrive as Annex-B (start-code) access units */
    FAAM_TRACK_INBAND_PARAMS = 0x2, /* Video: the samples repeat the parameter sets (avc3/hev1 sample
                                       * entry); codec_data is still required for the avcC/hvcC box */
    FAAM_TRACK_FLAG_MAX = 0x7fffffff
};

/* Track configuration parameters for muxer initialization. Initialize with faam_track_config_init(). */
typedef struct faam_track_config {
    uint32_t        struct_size;
    faam_track_type track_type;       /* FAAM_TRACK_AUDIO or FAAM_TRACK_VIDEO */
    faam_codec_id   codec_id;         /* FAAM_CODEC_AAC, FAAM_CODEC_H264, FAAM_CODEC_H265 */
    uint32_t        track_id;         /* Track identifier (0 for auto-assign; see faam_muxer_get_track_id) */
    uint32_t        timescale;        /* Track timescale (e.g., sample rate for audio, 90000 for video) */
    uint32_t        sample_rate;      /* Audio sample rate in Hz (audio tracks) */
    uint32_t        channels;         /* Audio channel count (audio tracks) */
    uint32_t        flags;            /* FAAM_TRACK_* bits */
    uint32_t        codec_data_len;   /* Length of codec extradata in bytes, at most FAAM_CODEC_DATA_MAX */
    uint16_t        width;            /* Video frame width in pixels (video tracks) */
    uint16_t        height;           /* Video frame height in pixels (video tracks) */
    uint16_t        rotation_degrees; /* Video display rotation: 0, 90, 180 or 270 clockwise */
    uint16_t        audio_sample_size; /* Audio: bits per sample of the source PCM, 0 for the default 16;
                                        * informational, it does not alter encoded audio. Ignored for video */
    char            language[4];      /* ISO 639-2/T; the first three characters count, letters are stored
                                       * lowercase, NUL-terminated input shorter than 3 characters or any
                                       * non-letter makes it und */
    const uint8_t  *codec_data;       /* Decoder config (AAC ASC / avcC / hvcC payload) */
    const struct faam_gapless_info *gapless; /* Per-track priming/padding for this track's edit list; NULL: the muxer's */
} faam_track_config;
#define FAAM_TRACK_CONFIG_BASELINE FAAM_STRUCT_END(faam_track_config, gapless)
#define FAAM_CODEC_DATA_MAX 1024

/* Zero-initialize *track and stamp struct_size with min(caller_size, sizeof). Pass
 * sizeof(*track), then set the fields the track needs. */
FAAMAPI faam_status faam_track_config_init(faam_track_config *track, uint32_t caller_size);

/* FAAM_TRACK_ANNEXB (video only, needs the muxer-video build option): write_frame takes one
 * access unit with 00 00 01 / 00 00 00 01 start codes and stores it as 4-byte length-prefixed
 * NAL units; the caller's buffer is never modified and nothing is allocated. For H.264 the
 * track may then omit codec_data (NULL, 0): the avcC is built from the first SPS and the
 * first PPS that follows it, so the first access unit must carry them, and finalize fails
 * with FAAM_ERR_INVALID_ARGUMENT if none was seen. width and height are still required.
 * H.265 needs a caller-supplied hvcC. */

/* faam_track_info.flags */
enum faam_track_info_flag {
    FAAM_TRACK_INFO_INBAND_PARAMS = 0x1, /* avc3/hev1 sample entry */
    FAAM_TRACK_INFO_FLAG_MAX = 0x7fffffff
};

/* Information about a parsed track in a container. Set struct_size to sizeof before the call. */
typedef struct faam_track_info {
    uint32_t        struct_size;
    uint32_t        track_id;         /* The tkhd track id */
    faam_track_type track_type;
    faam_codec_id   codec_id;
    uint32_t        timescale;
    uint32_t        sample_rate;      /* Audio sample rate in Hz */
    uint32_t        channels;         /* Audio channels; 0 when the config does not say (e.g. USAC) */
    uint32_t        total_frames;     /* Frame/sample count; 0 for fragmented files (not known without scanning) */
    uint64_t        total_duration;   /* Total duration in timescale units; fragmented files: from mehd, 0 if the writer never finalized */
    uint32_t        max_frame_bytes;  /* Progressive: the largest sample of the track. Fragmented: the largest
                                       * seen so far, 0 until the first fragment is loaded */
    uint32_t        fourcc;           /* Sample entry type, big-endian fourcc ('mp4a', 'avc1', 'Opus', ...); 0 if none */
    uint32_t        flags;            /* FAAM_TRACK_INFO_* bits */
    uint16_t        width;            /* Video width in pixels */
    uint16_t        height;           /* Video height in pixels */
    uint16_t        rotation_degrees; /* tkhd matrix rotation: 0, 90, 180 or 270 clockwise; other matrices read 0 */
    char            language[4];
} faam_track_info;
#define FAAM_TRACK_INFO_BASELINE FAAM_STRUCT_END(faam_track_info, language)

/* Frame location metadata returned by streaming demuxer. Set struct_size to sizeof before the call. */
typedef struct faam_frame_loc {
    uint32_t struct_size;
    uint32_t track_id;         /* Track identifier for this frame */
    uint64_t file_offset;      /* Byte offset of payload frame in stream */
    uint32_t frame_bytes;      /* Length of the payload frame in bytes */
    uint32_t duration_ticks;   /* Frame duration (DTS delta) in timescale ticks */
    int32_t  cts_offset;       /* PTS - DTS in timescale ticks (ctts); 0 for audio and I/P-only video */
    uint32_t flags;            /* FAAM_FRAME_* bits (FAAM_FRAME_KEYFRAME: sync sample / keyframe / IDR) */
} faam_frame_loc;
#define FAAM_FRAME_LOC_BASELINE FAAM_STRUCT_END(faam_frame_loc, flags)

/* Native ilst keys are four bytes, including bytes such as 0xa9 (not UTF-8).
 * FAAM_FOURCC(0xa9, 'l', 'y', 'r') is the interoperable lyrics key. */
#define FAAM_FOURCC(a, b, c, d) \
    (((uint32_t)(uint8_t)(a) << 24) | ((uint32_t)(uint8_t)(b) << 16) | \
     ((uint32_t)(uint8_t)(c) << 8) | (uint32_t)(uint8_t)(d))

/* Common ISO/QuickTime ilst keys. Generic metadata callers may still use any
 * FourCC; these names keep common keys consistent across the library, frontend,
 * and C tests. Freeform entries also require a name (and optionally a mean). */
#define FAAM_METADATA_KEY_TITLE                 FAAM_FOURCC(0xa9, 'n', 'a', 'm')
#define FAAM_METADATA_KEY_TITLE_SORT            FAAM_FOURCC('s', 'o', 'n', 'm')
#define FAAM_METADATA_KEY_ARTIST                FAAM_FOURCC(0xa9, 'A', 'R', 'T')
#define FAAM_METADATA_KEY_ARTIST_SORT           FAAM_FOURCC('s', 'o', 'a', 'r')
#define FAAM_METADATA_KEY_ALBUM                 FAAM_FOURCC(0xa9, 'a', 'l', 'b')
#define FAAM_METADATA_KEY_ALBUM_SORT            FAAM_FOURCC('s', 'o', 'a', 'l')
#define FAAM_METADATA_KEY_ALBUM_ARTIST          FAAM_FOURCC('a', 'A', 'R', 'T')
#define FAAM_METADATA_KEY_ALBUM_ARTIST_SORT     FAAM_FOURCC('s', 'o', 'a', 'a')
#define FAAM_METADATA_KEY_COMPOSER              FAAM_FOURCC(0xa9, 'w', 'r', 't')
#define FAAM_METADATA_KEY_COMPOSER_SORT         FAAM_FOURCC('s', 'o', 'c', 'o')
#define FAAM_METADATA_KEY_GENRE                 FAAM_FOURCC(0xa9, 'g', 'e', 'n')
#define FAAM_METADATA_KEY_GENRE_CODE            FAAM_FOURCC('g', 'n', 'r', 'e')
#define FAAM_METADATA_KEY_YEAR                  FAAM_FOURCC(0xa9, 'd', 'a', 'y')
#define FAAM_METADATA_KEY_COMMENT               FAAM_FOURCC(0xa9, 'c', 'm', 't')
#define FAAM_METADATA_KEY_ENCODER               FAAM_FOURCC(0xa9, 't', 'o', 'o')
#define FAAM_METADATA_KEY_COMPILATION           FAAM_FOURCC('c', 'p', 'i', 'l')
#define FAAM_METADATA_KEY_TRACK                 FAAM_FOURCC('t', 'r', 'k', 'n')
#define FAAM_METADATA_KEY_DISC                  FAAM_FOURCC('d', 'i', 's', 'k')
#define FAAM_METADATA_KEY_COVER_ART             FAAM_FOURCC('c', 'o', 'v', 'r')
#define FAAM_METADATA_KEY_LYRICS                FAAM_FOURCC(0xa9, 'l', 'y', 'r')
#define FAAM_METADATA_KEY_GROUPING              FAAM_FOURCC(0xa9, 'g', 'r', 'p')
#define FAAM_METADATA_KEY_COPYRIGHT             FAAM_FOURCC('c', 'p', 'r', 't')
#define FAAM_METADATA_KEY_DESCRIPTION           FAAM_FOURCC('d', 'e', 's', 'c')
#define FAAM_METADATA_KEY_LONG_DESCRIPTION      FAAM_FOURCC('l', 'd', 'e', 's')
#define FAAM_METADATA_KEY_WORK                  FAAM_FOURCC(0xa9, 'w', 'r', 'k')
#define FAAM_METADATA_KEY_MOVEMENT              FAAM_FOURCC(0xa9, 'm', 'v', 'n')
#define FAAM_METADATA_KEY_BPM                   FAAM_FOURCC('t', 'm', 'p', 'o')
#define FAAM_METADATA_KEY_MOVEMENT_NUMBER       FAAM_FOURCC(0xa9, 'm', 'v', 'i')
#define FAAM_METADATA_KEY_MOVEMENT_TOTAL        FAAM_FOURCC(0xa9, 'm', 'v', 'c')
#define FAAM_METADATA_KEY_SHOW_WORK_MOVEMENT    FAAM_FOURCC('s', 'h', 'w', 'm')
#define FAAM_METADATA_KEY_FREEFORM              FAAM_FOURCC('-', '-', '-', '-')

enum faam_metadata_data_type {
    FAAM_METADATA_IMPLICIT = 0,
    FAAM_METADATA_UTF8 = 1,
    FAAM_METADATA_UTF16 = 2, /* big endian */
    FAAM_METADATA_GIF = 12,
    FAAM_METADATA_JPEG = 13,
    FAAM_METADATA_PNG = 14,
    FAAM_METADATA_INTEGER = 21, /* big endian; payload specifies the width */
    FAAM_METADATA_BMP = 27,
    FAAM_METADATA_DATA_TYPE_MAX = 0x7fffffff
};
enum faam_metadata_entry_flag {
    FAAM_METADATA_REMOVE = 0x1, /* Remove this key; data must be NULL/0 */
    FAAM_METADATA_ENTRY_FLAG_MAX = 0x7fffffff
};

/* One ilst data value. Payload is exactly data_bytes bytes, without an implied
 * terminator; text is UTF-8 when data_type is FAAM_METADATA_UTF8. Other 24-bit
 * data types and binary payloads are supported without interpretation. locale
 * is the data box's 32-bit locale. For '----', name is required and mean defaults
 * to com.apple.iTunes; native keys require NULL mean/name. Identity is the key,
 * plus mean/name for '----' (not data type or locale). Repeated keys supply
 * multiple values; do not mix REMOVE and values for the same key.
 * Inputs are borrowed through finalize/update; getter output lives until close.
 * REMOVE entries are omitted when muxing (and suppress an automatic iTunSMPB
 * tag when that freeform key is explicitly supplied). */
typedef struct faam_metadata_entry {
    uint32_t struct_size;
    uint32_t key;
    uint32_t data_type;
    uint32_t locale;
    const void *data;
    uint32_t data_bytes;
    uint32_t flags;
    const char *mean;
    const char *name;
} faam_metadata_entry;
#define FAAM_METADATA_ENTRY_BASELINE FAAM_STRUCT_END(faam_metadata_entry, name)


/* --- Stream Demuxer API (MP4/M4A/M4B -> Demuxer) --- */

/* Demuxer open parameters. struct_size is set by the caller; a NULL config means defaults. */
typedef struct faam_demuxer_config {
    uint32_t struct_size;
    uint32_t flags;            /* Reserved, 0 */
} faam_demuxer_config;
#define FAAM_DEMUXER_CONFIG_BASELINE FAAM_STRUCT_END(faam_demuxer_config, flags)

/* Zero-initialize *config and stamp struct_size with min(caller_size, sizeof). Pass sizeof(*config). */
FAAMAPI faam_status faam_demuxer_config_init(faam_demuxer_config *config, uint32_t caller_size);

/* The demuxer needs read and seek (FAAM_ERR_UNSUPPORTED otherwise; FAAM_ERR_NOT_BUILT for a
 * fragmented file in a build without muxer-fragmented; a read callback that
 * returns a negative value during open gives FAAM_ERR_IO_READ). A source without a moov
 * opens with zero tracks. It keeps only moov in memory; samples of progressive
 * files are indexed up front, fragmented files (muxer-fragmented option) are read one
 * moof at a time and a fragment whose data was cut short is ignored, so a crashed
 * recording yields exactly its completed fragments. A file may hold more tracks than
 * max_tracks: the demuxer holds the first max_tracks and counts the rest (see
 * faam_demuxer_get_num_tracks). Tracks are addressed by their tkhd track id or by index. */

FAAMAPI faam_status faam_demuxer_open(const faam_demuxer_config *config, const faam_io *io,
                                      faam_demuxer **out);
FAAMAPI faam_status faam_demuxer_close(faam_demuxer **demuxer);

/* *out_held is the number of tracks the demuxer holds (valid indices for get_track_info);
 * *out_total, when not NULL, is the number of tracks in the file, which exceeds out_held
 * when the file has more than max_tracks. */
FAAMAPI faam_status faam_demuxer_get_num_tracks(const faam_demuxer *demuxer, uint32_t *out_held, uint32_t *out_total);

/* Set out_info->struct_size to sizeof(faam_track_info) before the call. */
FAAMAPI faam_status faam_demuxer_get_track_info(const faam_demuxer *demuxer, uint32_t track_index,
                                                faam_track_info *out_info);

/* Copies codec-specific decoder configuration: the AudioSpecificConfig, the avcC/hvcC
 * record, or for an mp4v (MPEG-4 Visual) track, which reports FAAM_CODEC_GENERIC, the
 * DecoderSpecificInfo (VOL configuration) from its esds. On
 * FAAM_ERR_OUTPUT_TOO_SMALL, *out_len is the required capacity; retry with that size.
 * A NULL out_buf with zero buf_cap queries the required size. */
FAAMAPI faam_status faam_demuxer_get_codec_data(const faam_demuxer *demuxer, uint32_t track_id,
                                                uint8_t *out_buf, uint32_t buf_cap,
                                                uint32_t *out_len);

/* The ftyp major brand as a NUL-terminated string, padding spaces kept; empty without an ftyp. */
FAAMAPI faam_status faam_demuxer_get_major_brand(const faam_demuxer *demuxer, char out_brand[5]);

/* Reports gapless parameters for one track; track_id 0 means the first audio track. The
 * first audio track gets the file's iTunSMPB when present; every track otherwise uses its
 * own edit list, with delay, padding and total in that track's timescale units. All zeros
 * when neither exists. Set out_gapless->struct_size to sizeof. */
FAAMAPI faam_status faam_demuxer_get_track_gapless(const faam_demuxer *demuxer, uint32_t track_id, faam_gapless_info *out_gapless);

/* Number of ilst values; read each with faam_demuxer_get_metadata_entry(). */
FAAMAPI faam_status faam_demuxer_get_num_metadata(const faam_demuxer *demuxer, uint32_t *out_count);
/* Set out_entry->struct_size to sizeof. Enumerates every supported ilst data child, including native, freeform and
 * repeated values. Unknown data types are returned as bytes; malformed/opaque
 * atoms without a usable data child are not enumerated. */
FAAMAPI faam_status faam_demuxer_get_metadata_entry(const faam_demuxer *demuxer, uint32_t index, faam_metadata_entry *out_entry);



/* Up to 255 chapters exist (the Nero chpl count byte). Set out_chapter->struct_size to sizeof. */
FAAMAPI faam_status faam_demuxer_get_num_chapters(const faam_demuxer *demuxer, uint32_t *out_count);
FAAMAPI faam_status faam_demuxer_get_chapter(const faam_demuxer *demuxer, uint32_t index, faam_chapter *out_chapter);

/* Returns the next frame's location without consuming it. Repeated calls return
 * the same location until faam_demuxer_read_frame() consumes or skips that frame.
 * Frames of all held tracks come interleaved in file order; track_id says which.
 * Returns FAAM_END_OF_STREAM after the last frame. */
FAAMAPI faam_status faam_demuxer_next_frame_loc(faam_demuxer *demuxer, faam_frame_loc *out_loc);

/* Reads and consumes the next frame. Set out_frame to NULL to skip it; otherwise
 * frame_cap must fit the whole frame. On FAAM_ERR_OUTPUT_TOO_SMALL, frame_bytes
 * receives the required capacity and the frame remains available for retry.
 * A read error also leaves the frame cursor unchanged. */
FAAMAPI faam_status faam_demuxer_read_frame(faam_demuxer *demuxer,
                                            uint8_t *out_frame, uint32_t frame_cap,
                                            uint32_t *frame_bytes);


/* --- Stream Muxer API (Muxer -> MP4/M4A/M4B) --- */

/* faam_muxer_config.flags */
enum faam_muxer_flag {
    FAAM_MUXER_M4B = 0x1, /* Write M4B brand headers (progressive audio-only files;
                                      * video and fragmented files get the isom brand) */
    FAAM_MUXER_CONSTANT_RATE = 0x2, /* esds maxBitrate equals avgBitrate */
    FAAM_MUXER_FASTSTART = 0x4, /* Progressive files: finalize moves moov in front of mdat, which
                                      * rewrites the whole file, so it needs io.read and is not for
                                      * flash recorders; invalid with fragment_ms */
    FAAM_MUXER_FLAG_MAX = 0x7fffffff
};

/* Muxer open parameters. Fill with faam_muxer_config_init(), then set the fields you need.
 * tracks are read at open. chapters and metadata are borrowed until finalize; a fragmented file
 * writes both at open. A progressive file whose tags are not known yet supplies them later with
 * FAAM_UPDATE_METADATA. tracks, chapters and metadata are arrays strided by their first
 * element's struct_size. */
typedef struct faam_muxer_config {
    uint32_t            struct_size;
    uint32_t            flags;           /* FAAM_MUXER_* bits */
    uint32_t            creation_time;   /* Unix seconds (1970 epoch); 0 is stored as 0 (unset). The file
                                          * stores the 1904 epoch, so from 2040 on the version-1 (64-bit)
                                          * header boxes are used instead of wrapping */
    uint32_t            fragment_ms;     /* 0: progressive file, moov written by finalize.
                                          * >0: fragmented MP4 (needs the muxer-fragmented build option),
                                          * see faam_muxer_open */
    uint32_t            num_tracks;      /* 1 to max_tracks */
    uint32_t            num_chapters;    /* Chapter count, at most 255 */
    uint32_t            num_metadata;    /* Metadata entry count */
    const faam_track_config *tracks;     /* num_tracks entries */
    const faam_chapter *chapters;        /* Chapters list to inject; every title must be non-NULL */
    const faam_metadata_entry *metadata; /* num_metadata entries; NULL when 0 */
    const faam_gapless_info *gapless;    /* Priming/padding for iTunSMPB and the edit list; NULL: none */
} faam_muxer_config;
#define FAAM_MUXER_CONFIG_BASELINE FAAM_STRUCT_END(faam_muxer_config, gapless)

/* Zeroes config, sets struct_size to caller_size (at least FAAM_MUXER_CONFIG_BASELINE) and
 * leaves every other field at its default. */
FAAMAPI faam_status faam_muxer_config_init(faam_muxer_config *config, uint32_t caller_size);

/* Fragmented recording (config->fragment_ms > 0) is meant for crash-safe capture to SD:
 * ftyp and a moov with empty sample tables are written at open, then [moof][mdat]
 * fragments follow, each closed at the first video keyframe once fragment_ms of video
 * (audio-only files: of audio) has accumulated, or earlier when its sample index
 * (about 128 samples per second of fragment_ms, at least 64) or 1 GiB fills. A fragment's
 * moof is written when it closes, so a crash loses at most the open fragment and every
 * completed one stays playable. The sample index is allocated in separate blocks
 * at open; no allocation happens while muxing. In this mode
 * video tracks need codec_data (the moov precedes any frame),
 * metadata, chapters and gapless are written at open from config (faam_muxer_update returns
 * FAAM_ERR_UNSUPPORTED afterwards), io.flush runs after every completed fragment, and
 * finalize closes the last fragment and records the total duration in mehd. B-frame
 * offsets travel in the trun boxes; no edit list shifts the first frame to t=0, so such
 * video starts at its first cts_offset. */

/* Both modes need write, seek and tell in the io (read is only used by
 * FAAM_MUXER_FASTSTART; flush is optional); a missing one is FAAM_ERR_UNSUPPORTED, and a fragmented config in a
 * build without muxer-fragmented is FAAM_ERR_NOT_BUILT. A progressive file is only complete
 * once finalize has written its moov: closing without finalize leaves a file with no moov
 * that cannot be recovered, while a fragmented file closed without finalize keeps every
 * completed fragment playable. FAAM_MUXER_FASTSTART makes finalize copy the sample data
 * forward to make room for moov in front of it; the file is not a valid MP4 until that
 * copy ends, so an interruption during finalize loses the recording. */
FAAMAPI faam_status faam_muxer_open(const faam_muxer_config *config, const faam_io *io,
                                    faam_muxer **out);

/* The track ids in config->tracks order, after auto-assignment. */
FAAMAPI faam_status faam_muxer_get_track_id(const faam_muxer *muxer, uint32_t track_index, uint32_t *out_track_id);

/* faam_muxer_update_params.flags: the fields to apply */
enum faam_update_flag {
    FAAM_UPDATE_CREATION_TIME = 0x1,
    FAAM_UPDATE_GAPLESS = 0x2,
    FAAM_UPDATE_CODEC_DATA = 0x4, /* track_id, codec_data, codec_data_len */
    FAAM_UPDATE_LANGUAGE = 0x8, /* track_id, language */
    FAAM_UPDATE_METADATA = 0x10, /* metadata, num_metadata: replaces the config's list */
    FAAM_UPDATE_FLAG_MAX = 0x7fffffff
};

/* Late configuration for progressive files whose writer learns something only after the
 * first frame (an encoder's AudioSpecificConfig, the programme length). Fields not named
 * in flags are ignored. Returns FAAM_ERR_UNSUPPORTED on a fragmented muxer, whose moov is
 * already on disk, and FAAM_ERR_INVALID_ARGUMENT after finalize. codec_data is at most
 * FAAM_CODEC_DATA_MAX bytes and is copied; language follows the faam_track_config rule.
 * creation_time is Unix seconds, see faam_muxer_config. metadata is borrowed until finalize,
 * like the config's. */
typedef struct faam_muxer_update_params {
    uint32_t struct_size;
    uint32_t flags;
    uint32_t track_id;
    uint32_t creation_time;
    uint32_t codec_data_len;
    uint32_t num_metadata;
    char     language[4];
    const uint8_t *codec_data;
    const faam_gapless_info *gapless;
    const faam_metadata_entry *metadata;
} faam_muxer_update_params;
#define FAAM_MUXER_UPDATE_PARAMS_BASELINE FAAM_STRUCT_END(faam_muxer_update_params, metadata)

FAAMAPI faam_status faam_muxer_update(faam_muxer *muxer, const faam_muxer_update_params *params);

/* frame_bytes is at most INT32_MAX (FAAM_ERR_INVALID_ARGUMENT above it).
 * duration_ticks is the DTS delta to the next frame in track timescale units.
 * cts_offset is PTS - DTS (negative allowed) for B-frame video; pass 0 for audio and
 * I/P-only video, which then writes no ctts box. Non-zero offsets need the muxer-video
 * build option (FAAM_ERR_NOT_BUILT otherwise) and are invalid on audio tracks. */
enum faam_frame_flag {
    FAAM_FRAME_KEYFRAME = 1u << 0, /* Sync sample / IDR; audio frames are all keyframes, the flag is ignored there */
    FAAM_FRAME_FLAG_MAX = 0x7fffffff
};

FAAMAPI faam_status faam_muxer_write_frame(faam_muxer *muxer,
                                           uint32_t track_id,
                                           const uint8_t *frame_buf, uint32_t frame_bytes,
                                           uint32_t duration_ticks,
                                           int32_t cts_offset,
                                           uint32_t frame_flags);

/* Progressive: writes the sample tables and moov, patching the mdat size, so it needs the
 * seek/tell checked at open. Fails with FAAM_ERR_INVALID_ARGUMENT (not sticky) when a chapter
 * title is NULL or a video track never obtained its codec_data. I/O and allocation
 * failures are sticky. */
FAAMAPI faam_status faam_muxer_finalize(faam_muxer *muxer);

/* Releases the muxer; *muxer is NULL afterwards. Returns the sticky I/O status, so a caller
 * that never finalized still learns whether a write failed. */
FAAMAPI faam_status faam_muxer_close(faam_muxer **muxer);

/* Querying Muxer Statistics. Set struct_size to sizeof before the call. */
typedef struct faam_muxer_info {
    uint32_t struct_size;
    uint32_t frame_count;
    uint64_t duration_ticks;
    uint64_t file_bytes;
    uint32_t max_bitrate;
    uint32_t avg_bitrate;
    uint32_t max_frame_size;
} faam_muxer_info;
#define FAAM_MUXER_INFO_BASELINE FAAM_STRUCT_END(faam_muxer_info, max_frame_size)

FAAMAPI faam_status faam_muxer_get_info(const faam_muxer *muxer, uint32_t track_id, faam_muxer_info *out_info);

FAAMAPI const char *faam_strerror(faam_status status);

/* Rewrite tags or chapters of a finished progressive file in place. They need read, write,
 * seek and tell (FAAM_ERR_UNSUPPORTED if any is NULL) and grow the file as needed,
 * keeping the chunk offsets of moov-first files valid. A moov without udta, or a udta
 * without meta/ilst (tags) or chpl (chapters), gets the missing atoms created. Chapter
 * count is at most 255 and titles must be non-NULL; metadata payloads follow the usual
 * borrowing rules. FAAM_ERR_BAD_CONTAINER means the file has no moov; a moov or one of
 * the boxes being edited with a 64-bit size header is FAAM_ERR_UNSUPPORTED, and the file
 * is left as it was. A fragmented recording, finalized or cut short by a crash, can be
 * tagged too: its completed fragments stay playable. */

/* faam_update_metadata_stream flags */
enum faam_metadata_update_flag {
    FAAM_METADATA_UPDATE_CLEAR = 0x1, /* Drop all existing ilst atoms, including opaque ones */
    FAAM_METADATA_UPDATE_FLAG_MAX = 0x7fffffff
};

/* Patch only the supplied keys, preserving all other ilst atoms byte-for-byte.
 * All existing values of each supplied key are replaced by its entries, or
 * removed with FAAM_METADATA_REMOVE. CLEAR drops all old ilst atoms first.
 * count == 0 permits entries == NULL. No demux/read-modify-write is needed.
 * I/O and container restrictions are described above. Existing demuxers must
 * be closed/reopened before reading the modified file. */
FAAMAPI faam_status faam_update_metadata_stream(const faam_io *io, const faam_metadata_entry *entries,
                                               uint32_t count, uint32_t flags);
/* flags is reserved and must be 0. */
FAAMAPI faam_status faam_update_chapters_stream(const faam_io *io, const faam_chapter *chapters, uint32_t count, uint32_t flags);

#ifdef __cplusplus
}
#endif

#endif /* FAAM_H */
