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
 * Design summary:
 *   - Every struct the caller fills or receives starts with uint32_t struct_size.
 *     Callers set it to sizeof of their struct; the library accepts any size at or
 *     above the FAAM_*_BASELINE of that struct, reads only the fields that size
 *     covers and, for output structs, writes at most struct_size bytes. New
 *     fields are only ever appended after the last baseline field, explicit
 *     padding is never reused, and a field an older caller cannot know about
 *     takes its zero value. Enum values, signatures and offsets of released
 *     fields never change within a major version.
 *   - No struct is embedded in another and none holds an array of structs (fixed char
 *     arrays aside). Input lists
 *     (tracks, chapters, custom tags) are a pointer and a count, strided by the
 *     struct_size of the first element, so a list is one array of one struct
 *     size. Output lists are read one element at a time by index.
 *   - Every open or sizing call takes a config struct (the demuxer's may be NULL for
 *     defaults), so a knob can be added later without changing a signature.
 *   - Compile-time options (muxer-video, muxer-fragmented) change what a call
 *     accepts, never a layout; faam_get_library_info() reports them.
 *
 * Status contract:
 *   - A NULL pointer, NULL io or out parameter, or an otherwise invalid
 *     argument returns FAAM_ERR_INVALID_ARG.
 *   - A callback the operation needs but the io leaves NULL returns
 *     FAAM_ERR_UNSUPPORTED, as does content or a call the library cannot handle; a
 *     feature compiled out of this build returns FAAM_ERR_NOT_BUILT. Each function
 *     below says which callbacks it needs; they are checked before any byte is read
 *     or written.
 *   - A caller arena that is too small, or an allocation that failed, returns
 *     FAAM_ERR_INSUFFICIENT_MEM.
 *   - On failure init/open functions set *out_muxer / *out_demuxer to NULL
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

#ifndef FAAMAPI
# if defined(_WIN32)
#  define FAAMAPI __declspec(dllexport)
# elif defined(__GNUC__) && (__GNUC__ >= 4)
#  define FAAMAPI __attribute__((visibility("default")))
# else
#  define FAAMAPI
# endif
#endif

typedef struct faam_demuxer faam_demuxer;
typedef struct faam_muxer   faam_muxer;

typedef enum faam_status {
    FAAM_OK                   = 0,
    FAAM_ERR_INVALID_ARG      = -1,
    FAAM_ERR_BAD_CONTAINER    = -2, /* Invalid MP4 atom structure or missing track */
    FAAM_ERR_IO_READ          = -3, /* A read callback failed or a read ended short of data the caller needs */
    FAAM_ERR_IO_WRITE         = -4, /* I/O write failure */
    FAAM_ERR_INSUFFICIENT_MEM = -5, /* Provided memory arena is too small, or an allocation failed */
    FAAM_ERR_NO_TRACK         = -6, /* No matching video/audio track found */
    FAAM_ERR_UNSUPPORTED      = -7, /* A required io callback is NULL, or the call or content is not supported */
    FAAM_ERR_OUTPUT_TOO_SMALL = -8, /* *out_len reports the required output capacity */
    FAAM_ERR_NOT_BUILT        = -10, /* The feature is compiled out of this build (see faam_library_info.features) */
    FAAM_END_OF_STREAM        = 1,   /* Not an error: no more frames */
    FAAM_STATUS_MAX           = 0x7fffffff
} faam_status;

/* Offset just past the named member: the size of a struct as first released. */
#define FAAM_STRUCT_END(type, member) (offsetof(type, member) + sizeof(((type *)0)->member))

/* faam_library_info.features */
#define FAAM_FEATURE_VIDEO      0x1 /* muxer-video: H.264/H.265 tracks, Annex-B, B-frame offsets */
#define FAAM_FEATURE_FRAGMENTED 0x2 /* muxer-fragmented: fragmented MP4 recording and demuxing */

/* Global library metadata; set struct_size to sizeof(faam_library_info) before the call. */
typedef struct faam_library_info {
    uint32_t                struct_size;
    uint32_t                max_tracks;   /* Tracks one muxer writes or one demuxer holds */
    uint32_t                features;     /* FAAM_FEATURE_* bits of this build */
    uint32_t                reserved0;
    const char             *version;
    const char             *copyright;
} faam_library_info;
#define FAAM_LIBRARY_INFO_BASELINE FAAM_STRUCT_END(faam_library_info, copyright)

FAAMAPI faam_status faam_get_library_info(faam_library_info *out);

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

/* Abstract Stream I/O for embedded platforms (SPIFFS, SDMMC, RAM, network). Set struct_size to
 * sizeof(faam_io); a missing optional callback is NULL. */
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

/* Gapless audio parameters (iTunSMPB atom and the audio track's edit list), in samples
 * of the audio track's timescale (for HE-AAC the core rate: one frame is 1024).
 * Progressive muxer: when total_samples is 0 and a delay or padding is set, finalize
 * derives it as the track's total sample duration minus delay and padding; if that is
 * not positive the programme is empty and the edit list gets a zero segment length, as
 * master's writer does. The edit list needs encoder_delay > 0. It is
 * written, identical, into every audio track unless a track config carries its own. A
 * fragmented muxer writes it only when total_samples is given at init. The demuxer fills
 * this from iTunSMPB, else from the edit list of the first audio track that has one
 * (media time, segment length and the media past it); the getters report all zeros when
 * neither exists. */
typedef struct faam_gapless_info {
    uint32_t struct_size;
    uint32_t encoder_delay;    /* Leading priming samples to discard; defaults to 0, caller supplies codec delay */
    uint32_t end_padding;      /* Trailing zero-padding samples to discard */
    uint32_t reserved0;
    uint64_t total_samples;    /* Original unpadded PCM sample count, 0 if unknown */
} faam_gapless_info;
#define FAAM_GAPLESS_INFO_BASELINE FAAM_STRUCT_END(faam_gapless_info, total_samples)

/* Chapter Metadata Entry */
typedef struct faam_chapter {
    uint32_t struct_size;
    uint32_t reserved0;
    uint64_t start_ms;         /* Chapter start time in milliseconds */
    const char *title;         /* Must not be NULL; at most 255 bytes are stored. Borrowed through
                                * finalize/update; demux output lives until close */
} faam_chapter;
#define FAAM_CHAPTER_BASELINE FAAM_STRUCT_END(faam_chapter, title)

/* faam_track_config.flags */
#define FAAM_TRACK_ANNEXB         0x1 /* Video frames arrive as Annex-B (start-code) access units */
#define FAAM_TRACK_INBAND_PARAMS  0x2 /* Video: the samples repeat the parameter sets (avc3/hev1 sample
                                       * entry); codec_data is still required for the avcC/hvcC box */

/* Track configuration parameters for muxer initialization. Set struct_size to sizeof. */
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
    uint16_t        reserved0;
    char            language[4];      /* ISO 639-2/T; the first three characters count, letters are stored
                                       * lowercase, NUL-terminated input shorter than 3 characters or any
                                       * non-letter makes it und */
    const uint8_t  *codec_data;       /* Decoder config (AAC ASC / avcC / hvcC payload) */
    const struct faam_gapless_info *gapless; /* Per-track priming/padding for this track's edit list; NULL: the muxer's */
} faam_track_config;
#define FAAM_TRACK_CONFIG_BASELINE FAAM_STRUCT_END(faam_track_config, gapless)
#define FAAM_CODEC_DATA_MAX 1024

/* FAAM_TRACK_ANNEXB (video only, needs the muxer-video build option): write_frame takes one
 * access unit with 00 00 01 / 00 00 00 01 start codes and stores it as 4-byte length-prefixed
 * NAL units; the caller's buffer is never modified and nothing is allocated. For H.264 the
 * track may then omit codec_data (NULL, 0): the avcC is built from the first SPS and the
 * first PPS that follows it, so the first access unit must carry them, and finalize fails
 * with FAAM_ERR_INVALID_ARG if none was seen. width and height are still required.
 * H.265 needs a caller-supplied hvcC. */

/* faam_track_info.flags */
#define FAAM_TRACK_INFO_INBAND_PARAMS 0x1 /* avc3/hev1 sample entry */

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
    uint16_t        reserved0;
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
    bool     is_keyframe;      /* True if sync sample / keyframe / IDR frame */
    uint8_t  reserved0[3];
} faam_frame_loc;
#define FAAM_FRAME_LOC_BASELINE FAAM_STRUCT_END(faam_frame_loc, is_keyframe)

/* Custom key-value tag entry */
typedef struct faam_custom_tag {
    uint32_t struct_size;
    uint32_t reserved0;
    const char *name;
    const char *value;
    const char *mean;         /* Freeform namespace ('mean' atom), NULL = com.apple.iTunes */
} faam_custom_tag;
#define FAAM_CUSTOM_TAG_BASELINE FAAM_STRUCT_END(faam_custom_tag, mean)

/* faam_metadata.cover_type. AUTO detects PNG, GIF (GIF87a/GIF89a), and JPEG
 * (FF D8 FF) by signature, falling back to JPEG for anything else.
 * BMP is explicit only: a BM prefix is too weak for auto-detection. */
#define FAAM_COVER_AUTO 0
#define FAAM_COVER_JPEG 1
#define FAAM_COVER_PNG  2
#define FAAM_COVER_GIF  3
#define FAAM_COVER_BMP  4

/* Strings, artwork, arrays, and their contents are borrowed until finalize/update.
 * Demuxer output pointers remain valid until that demuxer is closed. From the demuxer,
 * custom_tags is NULL and num_custom_tags the count; read each with
 * faam_demuxer_get_custom_tag(). To the muxer and the update calls, custom_tags is an
 * array of faam_custom_tag strided by its first element's struct_size. Set struct_size
 * to sizeof(faam_metadata) in both directions. */
typedef struct faam_metadata {
    uint32_t struct_size;
    uint32_t cover_bytes;
    const char *title;
    const char *title_sort;
    const char *artist;
    const char *artist_sort;
    const char *album;
    const char *album_sort;
    const char *album_artist;
    const char *album_artist_sort;
    const char *composer;
    const char *composer_sort;
    const char *genre_str;
    const char *year;
    const char *comment;
    const char *encoder;
    const uint8_t *cover_art;
    const faam_custom_tag *custom_tags;
    uint32_t num_custom_tags;
    uint16_t genre_code;
    uint16_t track_num;
    uint16_t track_total;
    uint16_t disc_num;
    uint16_t disc_total;
    bool     compilation;
    uint8_t  cover_type;       /* FAAM_COVER_*; demux reports the declared type:
                               * 12 GIF, 13 JPEG, 14 PNG, 27 BMP. Other declared
                               * types use the same byte detection as AUTO. */
} faam_metadata;
#define FAAM_METADATA_BASELINE FAAM_STRUCT_END(faam_metadata, cover_type)


/* --- Stream Demuxer API (MP4/M4A/M4B -> Demuxer) --- */

/* Demuxer open parameters. struct_size is set by the caller; a NULL config means defaults. */
typedef struct faam_demuxer_config {
    uint32_t struct_size;
    uint32_t flags;            /* Reserved, 0 */
    uint32_t reserved0[2];
} faam_demuxer_config;
#define FAAM_DEMUXER_CONFIG_BASELINE FAAM_STRUCT_END(faam_demuxer_config, reserved0)

/* The demuxer needs read and seek (FAAM_ERR_UNSUPPORTED otherwise; FAAM_ERR_NOT_BUILT for a
 * fragmented file in a build without muxer-fragmented; a read callback that
 * returns a negative value during init gives FAAM_ERR_IO_READ). A source without a moov
 * opens with zero tracks. It keeps only moov in memory; samples of progressive
 * files are indexed up front, fragmented files (muxer-fragmented option) are read one
 * moof at a time and a fragment whose data was cut short is ignored, so a crashed
 * recording yields exactly its completed fragments. A file may hold more tracks than
 * max_tracks: the demuxer holds the first max_tracks and counts the rest (see
 * faam_demuxer_get_num_tracks). Tracks are addressed by their tkhd track id or by index. */

FAAMAPI faam_status faam_demuxer_get_state_size(const faam_demuxer_config *cfg, uint32_t *state_bytes);

FAAMAPI faam_status faam_demuxer_init(void *mem_buf, uint32_t mem_bytes,
                                      const faam_demuxer_config *cfg,
                                      const faam_io *io,
                                      faam_demuxer **out_demuxer);

FAAMAPI faam_status faam_demuxer_open(const faam_demuxer_config *cfg, const faam_io *io,
                                      faam_demuxer **out_demuxer);
FAAMAPI faam_status faam_demuxer_close(faam_demuxer **d);

/* *out_held is the number of tracks the demuxer holds (valid indices for get_track_info);
 * *out_total, when not NULL, is the number of tracks in the file, which exceeds out_held
 * when the file has more than max_tracks. */
FAAMAPI faam_status faam_demuxer_get_num_tracks(faam_demuxer *d, uint32_t *out_held, uint32_t *out_total);

/* Set out_info->struct_size to sizeof(faam_track_info) before the call. */
FAAMAPI faam_status faam_demuxer_get_track_info(faam_demuxer *d, uint32_t track_index,
                                                faam_track_info *out_info);

FAAMAPI faam_status faam_demuxer_get_codec_data(faam_demuxer *d, uint32_t track_id,
                                                uint8_t *out_buf, uint32_t buf_cap,
                                                uint32_t *out_len);

/* The ftyp major brand as a NUL-terminated string, padding spaces kept; empty without an ftyp. */
FAAMAPI faam_status faam_demuxer_get_major_brand(faam_demuxer *d, char out_brand[5]);

/* Reports gapless parameters for one track; track_id 0 means the first audio track. The
 * first audio track gets the file's iTunSMPB when present; every track otherwise uses its
 * own edit list, with delay, padding and total in that track's timescale units. All zeros
 * when neither exists. Set out_gapless->struct_size to sizeof. */
FAAMAPI faam_status faam_demuxer_get_track_gapless(faam_demuxer *d, uint32_t track_id, faam_gapless_info *out_gapless);

FAAMAPI faam_status faam_demuxer_get_metadata(faam_demuxer *d, faam_metadata *out_meta);

/* Custom tags beyond the count in faam_metadata are not reachable; index from 0. Set
 * out_tag->struct_size to sizeof. The strings live until the demuxer is closed. */
FAAMAPI faam_status faam_demuxer_get_custom_tag(faam_demuxer *d, uint32_t index, faam_custom_tag *out_tag);

/* Up to 255 chapters exist (the Nero chpl count byte). Set out_chapter->struct_size to sizeof. */
FAAMAPI faam_status faam_demuxer_get_num_chapters(faam_demuxer *d, uint32_t *out_count);
FAAMAPI faam_status faam_demuxer_get_chapter(faam_demuxer *d, uint32_t index, faam_chapter *out_chapter);

/* Returns FAAM_END_OF_STREAM (a positive, non-error status) after the last frame. Frames of
 * all held tracks come interleaved in file order; faam_frame_loc.track_id says which. */
FAAMAPI faam_status faam_demuxer_next_frame_loc(faam_demuxer *d, faam_frame_loc *out_loc);

FAAMAPI faam_status faam_demuxer_read_frame(faam_demuxer *d,
                                            uint8_t *out_frame, uint32_t frame_cap,
                                            uint32_t *frame_bytes);


/* --- Stream Muxer API (Muxer -> MP4/M4A/M4B) --- */

/* faam_muxer_config.flags */
#define FAAM_MUXER_M4B           0x1 /* Write M4B brand headers (progressive audio-only files;
                                      * video and fragmented files get the isom brand) */
#define FAAM_MUXER_CONSTANT_RATE 0x2 /* esds maxBitrate equals avgBitrate */

/* Muxer open parameters. Fill with faam_muxer_config_init(), then set the fields you need.
 * tracks are read at init. chapters and metadata are borrowed until finalize: a progressive file
 * reads the metadata then, so that struct may still be filled in until finalize (set its
 * struct_size before finalize); a fragmented file writes both at init. tracks and chapters are
 * arrays strided by their first element's struct_size. */
typedef struct faam_muxer_config {
    uint32_t            struct_size;
    uint32_t            flags;           /* FAAM_MUXER_* bits */
    uint32_t            creation_time;   /* Unix seconds (1970 epoch); 0 is stored as 0 (unset). The file
                                          * stores the 1904 epoch, so from 2040 on the version-1 (64-bit)
                                          * header boxes are used instead of wrapping */
    uint32_t            fragment_ms;     /* 0: progressive file, moov written by finalize.
                                          * >0: fragmented MP4 (needs the muxer-fragmented build option),
                                          * see faam_muxer_init */
    uint32_t            num_tracks;      /* 1 to max_tracks */
    uint32_t            num_chapters;    /* Chapter count, at most 255 */
    const faam_track_config *tracks;     /* num_tracks entries */
    const faam_chapter *chapters;        /* Chapters list to inject; every title must be non-NULL */
    const faam_metadata *metadata;       /* May be NULL */
    const faam_gapless_info *gapless;    /* Priming/padding for iTunSMPB and the edit list; NULL: none */
} faam_muxer_config;
#define FAAM_MUXER_CONFIG_BASELINE FAAM_STRUCT_END(faam_muxer_config, gapless)

/* Zeroes cfg, sets struct_size to caller_size (at least FAAM_MUXER_CONFIG_BASELINE) and
 * leaves every other field at its default. */
FAAMAPI faam_status faam_muxer_config_init(faam_muxer_config *cfg, uint32_t caller_size);

/* Fragmented recording (cfg->fragment_ms > 0) is meant for crash-safe capture to SD:
 * ftyp and a moov with empty sample tables are written at init, then [moof][mdat]
 * fragments follow, each closed at the first video keyframe once fragment_ms of video
 * (audio-only files: of audio) has accumulated, or earlier when its sample index
 * (about 128 samples per second of fragment_ms, at least 64) or 1 GiB fills. A fragment's
 * moof is written when it closes, so a crash loses at most the open fragment and every
 * completed one stays playable. The sample index is carved from the caller's arena:
 * get_state_size is exact and no heap allocation happens while muxing. In this mode
 * video tracks need codec_data (the moov precedes any frame),
 * metadata, chapters and gapless are written at init from cfg (faam_muxer_update returns
 * FAAM_ERR_UNSUPPORTED afterwards), io.flush runs after every completed fragment, and
 * finalize closes the last fragment and records the total duration in mehd. B-frame
 * offsets travel in the trun boxes; no edit list shifts the first frame to t=0, so such
 * video starts at its first cts_offset. */
FAAMAPI faam_status faam_muxer_get_state_size(const faam_muxer_config *cfg, uint32_t *state_bytes);

/* Both modes need write, seek and tell in the io (read is not used; flush is
 * optional); a missing one is FAAM_ERR_UNSUPPORTED, and a fragmented config in a
 * build without muxer-fragmented is FAAM_ERR_NOT_BUILT. A progressive file is only complete
 * once finalize has written its moov: closing without finalize leaves a file with no moov
 * that cannot be recovered, while a fragmented file closed without finalize keeps every
 * completed fragment playable. */
FAAMAPI faam_status faam_muxer_init(void *mem_buf, uint32_t mem_bytes,
                                    const faam_muxer_config *cfg,
                                    const faam_io *io,
                                    faam_muxer **out_muxer);

/* The track ids in cfg->tracks order, after auto-assignment. */
FAAMAPI faam_status faam_muxer_get_track_id(const faam_muxer *m, uint32_t track_index, uint32_t *out_track_id);

/* faam_muxer_update_params.flags: the fields to apply */
#define FAAM_UPDATE_CREATION_TIME     0x1
#define FAAM_UPDATE_GAPLESS           0x2
#define FAAM_UPDATE_CODEC_DATA        0x4 /* track_id, codec_data, codec_data_len */
#define FAAM_UPDATE_LANGUAGE          0x8 /* track_id, language */
#define FAAM_UPDATE_AUDIO_SAMPLE_SIZE 0x10 /* track_id, audio_sample_size */

/* Late configuration for progressive files whose writer learns something only after the
 * first frame (an encoder's AudioSpecificConfig, the programme length). Fields not named
 * in flags are ignored. Returns FAAM_ERR_UNSUPPORTED on a fragmented muxer, whose moov is
 * already on disk, and FAAM_ERR_INVALID_ARG after finalize. codec_data is at most
 * FAAM_CODEC_DATA_MAX bytes and is copied; language follows the faam_track_config rule.
 * creation_time is Unix seconds, see faam_muxer_config. audio_sample_size is the source PCM
 * sample-size field of the faac container writer (AAC defaults to 16); informational, it
 * does not alter encoded audio. */
typedef struct faam_muxer_update_params {
    uint32_t struct_size;
    uint32_t flags;
    uint32_t track_id;
    uint32_t creation_time;
    uint32_t codec_data_len;
    uint16_t audio_sample_size;
    uint16_t reserved0;
    char     language[4];
    const uint8_t *codec_data;
    const faam_gapless_info *gapless;
} faam_muxer_update_params;
#define FAAM_MUXER_UPDATE_PARAMS_BASELINE FAAM_STRUCT_END(faam_muxer_update_params, gapless)

FAAMAPI faam_status faam_muxer_update(faam_muxer *m, const faam_muxer_update_params *params);

/* duration_ticks is the DTS delta to the next frame in track timescale units.
 * cts_offset is PTS - DTS (negative allowed) for B-frame video; pass 0 for audio and
 * I/P-only video, which then writes no ctts box. Non-zero offsets need the muxer-video
 * build option (FAAM_ERR_NOT_BUILT otherwise) and are invalid on audio tracks. */
#define FAAM_FRAME_KEYFRAME 0x1 /* Sync sample / IDR; audio frames are all keyframes, the flag is ignored there */

FAAMAPI faam_status faam_muxer_write_frame(faam_muxer *m,
                                           uint32_t track_id,
                                           const uint8_t *frame_buf, uint32_t frame_bytes,
                                           uint32_t duration_ticks,
                                           int32_t cts_offset,
                                           uint32_t frame_flags);

/* Progressive: writes the sample tables and moov, patching the mdat size, so it needs the
 * seek/tell checked at init. Fails with FAAM_ERR_INVALID_ARG (not sticky) when a chapter
 * title is NULL or a video track never obtained its codec_data. I/O and allocation
 * failures are sticky. */
FAAMAPI faam_status faam_muxer_finalize(faam_muxer *m);

FAAMAPI faam_status faam_muxer_open(const faam_muxer_config *cfg, const faam_io *io,
                                    faam_muxer **out_muxer);

/* Releases the muxer; *m is NULL afterwards. Returns the sticky I/O status, so a caller
 * that never finalized still learns whether a write failed. */
FAAMAPI faam_status faam_muxer_close(faam_muxer **m);

/* Querying Muxer Statistics. Set struct_size to sizeof before the call. */
typedef struct faam_muxer_info {
    uint32_t struct_size;
    uint32_t frame_count;
    uint64_t duration_ticks;
    uint64_t file_bytes;
    uint32_t max_bitrate;
    uint32_t avg_bitrate;
    uint32_t max_frame_size;
    uint32_t reserved0;
} faam_muxer_info;
#define FAAM_MUXER_INFO_BASELINE FAAM_STRUCT_END(faam_muxer_info, max_frame_size)

FAAMAPI faam_status faam_muxer_get_info(const faam_muxer *m, uint32_t track_id, faam_muxer_info *out_info);

FAAMAPI const char *faam_strerror(faam_status status);

/* Rewrite tags or chapters of a finished progressive file in place. They need read, write,
 * seek and tell (FAAM_ERR_UNSUPPORTED if any is NULL) and grow the file as needed,
 * keeping the chunk offsets of moov-first files valid. A moov without udta, or a udta
 * without meta/ilst (tags) or chpl (chapters), gets the missing atoms created. Chapter
 * count is at most 255 and titles must be non-NULL; metadata strings follow the usual
 * borrowing rules. FAAM_ERR_BAD_CONTAINER means the file has no moov; a moov or one of
 * the boxes being edited with a 64-bit size header is FAAM_ERR_UNSUPPORTED, and the file
 * is left as it was. */
#define FAAM_TAG_UPDATE_CLEAR 0x1 /* Drop all existing ilst atoms, including unmodeled ones */

FAAMAPI faam_status faam_update_tags_stream(const faam_io *io, const faam_metadata *meta, uint32_t flags);
/* flags is reserved and must be 0. */
FAAMAPI faam_status faam_update_chapters_stream(const faam_io *io, const faam_chapter *chapters, uint32_t count, uint32_t flags);

#ifdef __cplusplus
}
#endif

#endif /* FAAM_H */
