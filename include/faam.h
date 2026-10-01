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
 * FAAM - FAAC/FAAD Media Manipulator API
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * Lightweight ISO BMFF (MP4/M4A/M4B/MP4V) container parser and builder.
 * Operates strictly over abstract faam_io stream callbacks (zero file path dependency).
 * Supports video (H.264/AVC, H.265/HEVC) and audio (AAC) tracks, metadata, and chapters.
 */

#ifndef FAAM_H
#define FAAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define FAAM_VERSION_MAJOR 1
#define FAAM_VERSION_MINOR 0
#define FAAM_VERSION_PATCH 0
#define FAAM_STR_(x) #x
#define FAAM_STR(x) FAAM_STR_(x)
#define FAAM_VERSION_STRING \
    FAAM_STR(FAAM_VERSION_MAJOR) "." FAAM_STR(FAAM_VERSION_MINOR) "." FAAM_STR(FAAM_VERSION_PATCH)

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
    FAAM_ERR_IO_READ          = -3, /* I/O read failure or unexpected EOF */
    FAAM_ERR_IO_WRITE         = -4, /* I/O write failure */
    FAAM_ERR_INSUFFICIENT_MEM = -5, /* Provided memory arena is too small */
    FAAM_ERR_NO_TRACK         = -6, /* No matching video/audio track found */
    FAAM_ERR_UNSUPPORTED      = -7, /* Feature not supported or disabled at compile time */
    FAAM_ERR_OUTPUT_TOO_SMALL = -8, /* *out_len reports the required output capacity */
    FAAM_END_OF_STREAM        = -9,
    FAAM_STATUS_MAX           = 0x7fffffff
} faam_status;

/* Global library metadata; set struct_size to sizeof(faam_library_info) before the call. */
typedef struct faam_library_info {
    uint32_t                struct_size;
    const char             *version;
    const char             *copyright;
} faam_library_info;

FAAMAPI faam_status faam_get_library_info(faam_library_info *out);

typedef enum faam_track_type {
    FAAM_TRACK_AUDIO = 1,
    FAAM_TRACK_VIDEO = 2,
    FAAM_TRACK_MAX = 0x7fffffff
} faam_track_type;

typedef enum faam_codec_id {
    FAAM_CODEC_GENERIC = 0,
    FAAM_CODEC_AAC     = 1,
    FAAM_CODEC_H264    = 2,
    FAAM_CODEC_H265    = 3,
    FAAM_CODEC_MAX     = 0x7fffffff
} faam_codec_id;

/* Abstract Stream I/O for embedded platforms (SPIFFS, SDMMC, RAM, network) */
typedef struct faam_io {
    void    *user_data;
    int32_t (*read)(void *user_data, void *buf, uint32_t bytes_to_read);
    int32_t (*write)(void *user_data, const void *buf, uint32_t bytes_to_write);
    bool    (*seek)(void *user_data, uint64_t offset);
    uint64_t(*tell)(void *user_data);
    bool    (*flush)(void *user_data); /* Optional; false reports an I/O failure */
} faam_io;

/* Gapless audio parameters (corresponds to iTunSMPB atom) */
typedef struct faam_gapless_info {
    uint32_t encoder_delay;    /* Leading priming samples to discard; defaults to 0, caller supplies codec delay */
    uint32_t end_padding;      /* Trailing zero-padding samples to discard */
    uint64_t total_samples;    /* Original unpadded PCM sample count */
} faam_gapless_info;

/* Chapter Metadata Entry */
typedef struct faam_chapter {
    uint64_t start_ms;         /* Chapter start time in milliseconds */
    const char *title;        /* Borrowed through finalize/update; demux output lives until close */
} faam_chapter;

/* faam_track_config.flags */
#define FAAM_TRACK_ANNEXB 0x1 /* Video frames arrive as Annex-B (start-code) access units */

/* Track configuration parameters for muxer initialization */
typedef struct faam_track_config {
    uint32_t        struct_size;
    faam_track_type track_type;       /* FAAM_TRACK_AUDIO or FAAM_TRACK_VIDEO */
    faam_codec_id   codec_id;         /* FAAM_CODEC_AAC, FAAM_CODEC_H264, FAAM_CODEC_H265, etc. */
    uint32_t        track_id;         /* 1-based track identifier (0 for auto-assign) */
    uint32_t        timescale;        /* Track timescale (e.g., sample rate for audio, 90000 for video) */
    uint16_t        width;            /* Video frame width in pixels (video tracks) */
    uint16_t        height;           /* Video frame height in pixels (video tracks) */
    uint32_t        sample_rate;      /* Audio sample rate in Hz (audio tracks) */
    uint32_t        channels;         /* Audio channel count (audio tracks) */
    char            language[4];     /* ISO 639-2/T, empty defaults to und */
    const uint8_t  *codec_data;       /* Decoder config (AAC ASC / avcC / hvcC payload), at most 256 bytes */
    uint32_t        codec_data_len;   /* Length of codec extradata in bytes */
    uint32_t        flags;            /* FAAM_TRACK_* bits */
} faam_track_config;

/* FAAM_TRACK_ANNEXB (video only, needs the muxer-video build option): write_frame takes one
 * access unit with 00 00 01 / 00 00 00 01 start codes and stores it as 4-byte length-prefixed
 * NAL units; the caller's buffer is never modified and nothing is allocated. For H.264 the
 * track may then omit codec_data (NULL, 0): the avcC is built from the first SPS and the
 * first PPS that follows it, so the first access unit must carry them, and finalize fails
 * with FAAM_ERR_INVALID_ARG if none was seen. width and height are still required.
 * H.265 needs a caller-supplied hvcC. */

/* Information about a parsed track in a container */
typedef struct faam_track_info {
    uint32_t        struct_size;
    uint32_t        track_id;
    faam_track_type track_type;
    faam_codec_id   codec_id;
    uint32_t        timescale;
    uint16_t        width;            /* Video width in pixels */
    uint16_t        height;           /* Video height in pixels */
    uint32_t        sample_rate;      /* Audio sample rate in Hz */
    uint32_t        channels;         /* Audio channels */
    uint32_t        total_frames;     /* Frame/sample count; 0 for fragmented files (not known without scanning) */
    uint64_t        total_duration;   /* Total duration in timescale units; fragmented files: from mehd, 0 if the writer never finalized */
    uint32_t        max_frame_bytes;
    char            language[4];
} faam_track_info;

/* Frame location metadata returned by streaming demuxer */
typedef struct faam_frame_loc {
    uint32_t track_id;         /* Track identifier for this frame */
    uint64_t file_offset;      /* Byte offset of payload frame in stream */
    uint32_t frame_bytes;      /* Length of the payload frame in bytes */
    uint32_t duration_ticks;   /* Frame duration (DTS delta) in timescale ticks */
    int32_t  cts_offset;       /* PTS - DTS in timescale ticks (ctts); 0 for audio and I/P-only video */
    bool     is_keyframe;      /* True if sync sample / keyframe / IDR frame */
} faam_frame_loc;

/* Custom key-value tag entry */
typedef struct faam_custom_tag {
    const char *name;
    const char *value;
} faam_custom_tag;

/* Strings, artwork, arrays, and their contents are borrowed until finalize/update.
 * Demuxer output pointers remain valid until that demuxer is closed. */
typedef struct faam_metadata {
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
    uint16_t genre_code;
    bool compilation;
    uint16_t track_num;
    uint16_t track_total;
    uint16_t disc_num;
    uint16_t disc_total;
    const uint8_t *cover_art;
    uint32_t cover_bytes;
    const faam_custom_tag *custom_tags;
    uint32_t num_custom_tags;
} faam_metadata;


/* --- Stream Demuxer API (MP4/M4A/MP4V -> Demuxer) --- */

/* The demuxer needs read and seek. It keeps only moov in memory; samples of progressive
 * files are indexed up front, fragmented files (muxer-fragmented option) are read one
 * moof at a time and a fragment whose data was cut short is ignored, so a crashed
 * recording yields exactly its completed fragments. */

FAAMAPI faam_status faam_demuxer_get_state_size(uint32_t *state_bytes);

FAAMAPI faam_status faam_demuxer_init(void *mem_buf, uint32_t mem_bytes,
                                      const faam_io *io,
                                      faam_demuxer **out_demuxer);

FAAMAPI faam_status faam_demuxer_open(const faam_io *io, faam_demuxer **out_demuxer);
FAAMAPI void faam_demuxer_close(faam_demuxer **d);

FAAMAPI faam_status faam_demuxer_get_num_tracks(faam_demuxer *d, uint32_t *out_num_tracks);

/* Set out_info->struct_size to sizeof(faam_track_info) before the call. */
FAAMAPI faam_status faam_demuxer_get_track_info(faam_demuxer *d, uint32_t track_index,
                                                faam_track_info *out_info);

FAAMAPI faam_status faam_demuxer_get_codec_data(faam_demuxer *d, uint32_t track_id,
                                                uint8_t *out_buf, uint32_t buf_cap,
                                                uint32_t *out_len);

FAAMAPI faam_status faam_demuxer_get_gapless(faam_demuxer *d, faam_gapless_info *out_gapless);

FAAMAPI faam_status faam_demuxer_get_metadata(faam_demuxer *d, faam_metadata *out_meta);

FAAMAPI faam_status faam_demuxer_get_chapters(faam_demuxer *d, faam_chapter *out_chapters,
                                               uint32_t cap, uint32_t *out_count);

FAAMAPI faam_status faam_demuxer_next_frame_loc(faam_demuxer *d, faam_frame_loc *out_loc);

FAAMAPI faam_status faam_demuxer_read_frame(faam_demuxer *d,
                                            uint8_t *out_frame, uint32_t frame_cap,
                                            uint32_t *frame_bytes);


/* --- Stream Muxer API (Muxer -> MP4/M4A/M4B/MP4V) --- */

typedef struct faam_muxer_config {
    uint32_t            struct_size;
    uint32_t            creation_time;   /* MP4 creation time timestamp */
    bool                constant_rate;  /* esds maxBitrate equals avgBitrate */
    bool                is_m4b;          /* True to write M4B brand headers */
    faam_gapless_info   gapless;         /* Priming/padding metadata for iTunSMPB */
    const faam_metadata *metadata;       /* Borrowed, may be populated until finalize */
    const faam_chapter *chapters;        /* Chapters list to inject */
    uint32_t            num_chapters;    /* Chapter count, at most 255 (demuxer returns at most 64) */
    uint32_t            fragment_ms;     /* 0: progressive file, moov written by finalize.
                                          * >0: fragmented MP4 (needs the muxer-fragmented build option),
                                          * see faam_muxer_init */
    faam_track_config   tracks[8];       /* Up to 8 tracks (video / audio) */
    uint32_t            num_tracks;      /* Number of configured tracks */
} faam_muxer_config;

FAAMAPI faam_status faam_muxer_config_init(faam_muxer_config *cfg, uint32_t caller_size);

FAAMAPI faam_status faam_muxer_config_add_track(faam_muxer_config *cfg,
                                                 const faam_track_config *track,
                                                 uint32_t *out_track_id);

/* Fragmented recording (cfg->fragment_ms > 0) is meant for crash-safe capture to SD:
 * ftyp and a moov with empty sample tables are written at init, then [moof][mdat]
 * fragments follow, each closed at the first video keyframe once fragment_ms of video
 * (audio-only files: of audio) has accumulated, or earlier when its sample index
 * (about 128 samples per second of fragment_ms, at least 64) or 1 GiB fills. A fragment's
 * moof is written when it closes, so a crash loses at most the open fragment and every
 * completed one stays playable. The sample index is carved from the caller's arena:
 * get_state_size is exact and no heap allocation happens while muxing. In this mode
 * the io needs seek and tell, video tracks need codec_data (the moov precedes any frame),
 * metadata, chapters and gapless are written at init from cfg (the set_* calls return
 * FAAM_ERR_UNSUPPORTED afterwards), io.flush runs after every completed fragment, and
 * finalize closes the last fragment and records the total duration in mehd. B-frame
 * offsets travel in the trun boxes; no edit list shifts the first frame to t=0, so such
 * video starts at its first cts_offset. */
FAAMAPI faam_status faam_muxer_get_state_size(const faam_muxer_config *cfg, uint32_t *state_bytes);

FAAMAPI faam_status faam_muxer_init(void *mem_buf, uint32_t mem_bytes,
                                    const faam_muxer_config *cfg,
                                    const faam_io *io,
                                    faam_muxer **out_muxer);

FAAMAPI faam_status faam_muxer_set_creation_time(faam_muxer *m, uint32_t unix_time);
FAAMAPI faam_status faam_muxer_set_gapless(faam_muxer *m, const faam_gapless_info *gapless);

/* duration_ticks is the DTS delta to the next frame in track timescale units.
 * cts_offset is PTS - DTS (negative allowed) for B-frame video; pass 0 for audio and
 * I/P-only video, which then writes no ctts box. Non-zero offsets need the muxer-video
 * build option (FAAM_ERR_UNSUPPORTED otherwise) and are invalid on audio tracks. */
FAAMAPI faam_status faam_muxer_write_frame(faam_muxer *m,
                                           uint32_t track_id,
                                           const uint8_t *frame_buf, uint32_t frame_bytes,
                                           uint32_t duration_ticks,
                                           int32_t cts_offset,
                                           bool is_keyframe);

/* Requires working seek/tell callbacks; I/O/allocation failures remain sticky. */
FAAMAPI faam_status faam_muxer_finalize(faam_muxer *m);

FAAMAPI faam_status faam_muxer_open(const faam_muxer_config *cfg, const faam_io *io,
                                    faam_muxer **out_muxer);
FAAMAPI void faam_muxer_close(faam_muxer **m);

/* Preserve the source PCM sample-size field used by the faac container writer.
 * AAC defaults to 16; this informational field does not alter encoded audio. */
FAAMAPI faam_status faam_muxer_set_audio_sample_size(faam_muxer *m, uint32_t track_id, uint16_t bits);

/* Querying Muxer Statistics */
typedef struct faam_muxer_info {
    uint32_t struct_size;
    uint32_t frame_count;
    uint64_t duration_ticks;
    uint64_t file_bytes;
    uint32_t max_bitrate;
    uint32_t avg_bitrate;
    uint32_t max_frame_size;
} faam_muxer_info;

FAAMAPI faam_status faam_muxer_get_info(const faam_muxer *m, uint32_t track_id, faam_muxer_info *out_info);

FAAMAPI const char *faam_strerror(faam_status status);

/* Retrofit APIs return FAAM_ERR_UNSUPPORTED in embedded builds. */
FAAMAPI faam_status faam_update_tags_stream(const faam_io *io, const faam_metadata *meta);
FAAMAPI faam_status faam_update_chapters_stream(const faam_io *io, const faam_chapter *chapters, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif /* FAAM_H */
