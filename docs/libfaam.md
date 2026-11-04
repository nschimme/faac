# libfaam - MP4 muxer and demuxer library

## Contents

- [Overview](#overview)
- [Building and linking](#building-and-linking)
- [Supported content](#supported-content)
- [Using the API](#using-the-api)
  - [Stream I/O](#stream-io)
  - [Errors](#errors)
  - [Functions](#functions)
  - [Demuxer lifecycle](#demuxer-lifecycle)
  - [Muxer lifecycle](#muxer-lifecycle)
  - [Allocation and ownership](#allocation-and-ownership)
- [Metadata, gapless and chapters](#metadata-gapless-and-chapters)
- [Editing finished files](#editing-finished-files)
- [Codec data and language](#codec-data-and-language)
- [Video tracks](#video-tracks)
- [Recording to removable storage](#recording-to-removable-storage)
- [Moving the index to the front](#moving-the-index-to-the-front)
- [Files with many tracks](#files-with-many-tracks)
- [Reading MP4 for the FAAD decoder](#reading-mp4-for-the-faad-decoder)
- [Thread safety](#thread-safety)
- [Examples](#examples)
- [ABI compatibility](#abi-compatibility)

## Overview

libfaam reads and writes ISO BMFF files (MP4, M4A, M4B). You supply encoded access
units and decoder configuration; the library neither encodes nor decodes, and all
stream access goes through your callbacks. Include `<faam.h>` and link `libfaam`
(pkg-config module `faam`). The `faam` CLI in `frontend/` is documented in
`faam(1)`.

The library and header are built and installed whether or not the CLI is enabled.

## Building and linking

```sh
meson setup build-faam -Dfrontend=false
meson compile -C build-faam
meson install -C build-faam
cc -std=c11 -Wall -Wextra inspect.c -o inspect $(pkg-config --cflags --libs faam)
```

- Shared and static libraries are built by default; `-Ddefault_library` selects one.
- The `faam` tool is built by default; `-Dmuxer=false` leaves it out. Video and
  fragmented files are enabled by default (see [Supported content](#supported-content)).
- Custom prefix: add its pkg-config directory to `PKG_CONFIG_PATH`. Inside the source
  tree, link from the build directory and add `include/` to the include path.
- Static linking: `pkg-config --cflags --libs --static faam` plus a static-only
  install or your toolchain's archive selection.

### Without Meson

Compile `libfaam/demux.c`, `mux.c`, `metadata.c`, `faam_util.c`, `tag.c`,
`chapter.c` and `atom_patch.c` with `include/` and `common/` on the include path.
Define `FAAM_BUILDING` for a Windows DLL, and `FAAM_MUXER_VIDEO=1` and
`FAAM_MUXER_FRAGMENTED=1` to include those features (leave them undefined to omit
them). Install `include/faam.h` with the library.

## Supported content

`faam_get_library_info()` (set `struct_size` first) returns `version` and
`copyright` (static strings), `max_tracks` (tracks one muxer can write or one
demuxer can hold) and `flags` with `FAAM_LIB_*` bits.

| Feature | Option (default) | Runtime check | When absent |
|---|---|---|---|
| H.264/H.265, including Annex-B input and avc3/hev1 | `muxer-video` (true) | `FAAM_LIB_VIDEO` | Video muxing and nonzero CTS offsets return `FAAM_ERR_NOT_BUILT`. Video sample entries demux as `FAAM_CODEC_GENERIC` without sync/CTS parsing |
| Fragmented MP4 | `muxer-fragmented` (true) | `FAAM_LIB_FRAGMENTED` | Fragmented muxing and demuxing return `FAAM_ERR_NOT_BUILT` |

## Using the API

Every fallible call returns a `faam_status` and returns results through
out-parameters. Set `struct_size` on every input and output struct, including nested
pointer targets. The `flags` argument of `faam_update_chapters_stream()` is reserved
and must be 0.

### Stream I/O

`faam_io` holds `struct_size`, `user_data`, and the `read`, `write`, `seek`, `tell`
and optional `flush` callbacks. The library copies it; keep `user_data` alive until
close. Seek offsets are absolute byte offsets.

| Operation | Required callbacks |
|---|---|
| Demuxer open | `read`, `seek` |
| Muxer open (both modes) | `write`, `seek`, `tell` |
| Muxer with `FAAM_MUXER_FASTSTART` | `read`, `write`, `seek`, `tell` |
| Tag and chapter updates | `read`, `write`, `seek`, `tell` |

A missing required callback returns `FAAM_ERR_UNSUPPORTED` before any I/O.

- **`read`** may return a short count; the library retries. Zero means end of data.
  A negative count or one above the request is `FAAM_ERR_IO_READ`. During demuxer
  open, zero inside a structure counts as truncation: the file opens with whatever
  was parsed, possibly no tracks.
- **`write`** must return exactly the requested count. Any other count, or a failed
  muxer seek or flush, is `FAAM_ERR_IO_WRITE`. Muxer I/O failures are sticky.
- A single read or write request never exceeds `INT32_MAX` bytes;
  `faam_muxer_write_frame()` rejects a larger `frame_bytes` with
  `FAAM_ERR_INVALID_ARGUMENT`.
- **`flush`** runs at muxer finalize and at fragmented open and fragment completion.
- The demuxer never uses write or tell. The muxer uses read only for
  `FAAM_MUXER_FASTSTART`.

Save this adapter as `stdio_io.h` and include it before other headers, so the POSIX
declarations and large-file offsets are enabled:

```c
/* stdio_io.h */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#endif
#include <faam.h>
#include <stdio.h>
#include <limits.h>
#ifdef _WIN32
#define faam_fseek _fseeki64
#define faam_ftell _ftelli64
typedef __int64 faam_file_offset;
#else
#include <sys/types.h>
#define faam_fseek fseeko
#define faam_ftell ftello
typedef off_t faam_file_offset;
_Static_assert(sizeof(off_t) >= 8, "64-bit file offsets required");
#endif

static int32_t io_read(void *u, void *buf, uint32_t n)
{
    if (n > INT32_MAX) return -1;
    size_t got = fread(buf, 1, n, (FILE *)u);
    return ferror((FILE *)u) ? -1 : (int32_t)got;
}
static int32_t io_write(void *u, const void *buf, uint32_t n)
{
    if (n > INT32_MAX) return -1;
    return (int32_t)fwrite(buf, 1, n, (FILE *)u);
}
static bool io_seek(void *u, uint64_t off)
{
    if (off > INT64_MAX) return false;
    return faam_fseek((FILE *)u, (faam_file_offset)off, SEEK_SET) == 0;
}
static uint64_t io_tell(void *u) { return (uint64_t)faam_ftell((FILE *)u); }
static bool io_flush(void *u) { return fflush((FILE *)u) == 0; }
static faam_io stdio_io(FILE *f)
{
    faam_io io = { .struct_size = sizeof(io), .user_data = f,
        .read = io_read, .write = io_write, .seek = io_seek,
        .tell = io_tell, .flush = io_flush };
    return io;
}
```

### Errors

`FAAM_OK` is 0 and errors are negative, but `FAAM_END_OF_STREAM` is +1: test for it
explicitly in frame loops, and use `status < 0` to detect errors. `faam_strerror()`
returns a static, non-NULL string.

| Status | Value | Meaning |
|---|---|---|
| `FAAM_OK` | 0 | Success |
| `FAAM_ERR_INVALID_ARGUMENT` | -1 | NULL required pointer, invalid struct size, configuration, flags or call order |
| `FAAM_ERR_UNSUPPORTED` | -2 | Missing required callback, unsupported content or call, or in-place 64-bit box header |
| `FAAM_ERR_NO_MEMORY` | -3 | Allocation failed |
| `FAAM_ERR_OUTPUT_TOO_SMALL` | -4 | Output capacity too small; the length out-parameter reports the required size |
| `FAAM_ERR_IO_READ` | -5 | Failed read, or insufficient data for the operation |
| `FAAM_ERR_IO_WRITE` | -6 | Failed write, muxer seek or flush |
| `FAAM_ERR_BAD_CONTAINER` | -7 | Invalid atom structure or missing track; no `moov` for an in-place edit |
| `FAAM_ERR_NO_TRACK` | -8 | No matching track |
| `FAAM_ERR_NOT_BUILT` | -9 | Feature compiled out; check `faam_library_info.flags` |
| `FAAM_END_OF_STREAM` | 1 | No more frames; not an error |

- Init and open set the output handle to NULL before failing. Close takes the
  handle's address and clears it; check its status too.
- Muxer I/O and allocation failures are sticky, including at finalize and close.
  Argument errors are not.

### Functions

Output structs require `struct_size`.

```c
faam_status faam_get_library_info(faam_library_info *out_info);
faam_status faam_demuxer_config_init(faam_demuxer_config *config, uint32_t caller_size);
faam_status faam_io_init(faam_io *io, uint32_t caller_size);
faam_status faam_track_config_init(faam_track_config *track, uint32_t caller_size);
faam_status faam_demuxer_open(const faam_demuxer_config *config, const faam_io *io,
                                      faam_demuxer **out);
faam_status faam_demuxer_close(faam_demuxer **demuxer);
faam_status faam_demuxer_get_num_tracks(const faam_demuxer *demuxer, uint32_t *out_held, uint32_t *out_total);
faam_status faam_demuxer_get_track_info(const faam_demuxer *demuxer, uint32_t track_index,
                                                faam_track_info *out_info);
faam_status faam_demuxer_get_codec_data(const faam_demuxer *demuxer, uint32_t track_id,
                                                uint8_t *out_buf, uint32_t buf_cap,
                                                uint32_t *out_len);
faam_status faam_demuxer_get_major_brand(const faam_demuxer *demuxer, char out_brand[5]);
faam_status faam_demuxer_get_track_gapless(const faam_demuxer *demuxer, uint32_t track_id, faam_gapless_info *out_gapless);
faam_status faam_demuxer_get_num_metadata(const faam_demuxer *demuxer, uint32_t *out_count);
faam_status faam_demuxer_get_metadata_entry(const faam_demuxer *demuxer, uint32_t index, faam_metadata_entry *out_entry);
faam_status faam_demuxer_get_num_chapters(const faam_demuxer *demuxer, uint32_t *out_count);
faam_status faam_demuxer_get_chapter(const faam_demuxer *demuxer, uint32_t index, faam_chapter *out_chapter);
faam_status faam_demuxer_next_frame_loc(faam_demuxer *demuxer, faam_frame_loc *out_loc);
faam_status faam_demuxer_read_frame(faam_demuxer *demuxer,
                                            uint8_t *out_frame, uint32_t frame_cap,
                                            uint32_t *frame_bytes);
faam_status faam_muxer_config_init(faam_muxer_config *config, uint32_t caller_size);
faam_status faam_muxer_get_track_id(const faam_muxer *muxer, uint32_t track_index, uint32_t *out_track_id);
faam_status faam_muxer_update(faam_muxer *muxer, const faam_muxer_update_params *params);
faam_status faam_muxer_write_frame(faam_muxer *muxer,
                                           uint32_t track_id,
                                           const uint8_t *frame_buf, uint32_t frame_bytes,
                                           uint32_t duration_ticks,
                                           int32_t cts_offset,
                                           uint32_t frame_flags);
faam_status faam_muxer_finalize(faam_muxer *muxer);
faam_status faam_muxer_open(const faam_muxer_config *config, const faam_io *io,
                                    faam_muxer **out);
faam_status faam_muxer_close(faam_muxer **muxer);
faam_status faam_muxer_get_info(const faam_muxer *muxer, uint32_t track_id, faam_muxer_info *out_info);
const char *faam_strerror(faam_status status);
faam_status faam_update_metadata_stream(const faam_io *io, const faam_metadata_entry *entries, uint32_t count, uint32_t flags);
faam_status faam_update_chapters_stream(const faam_io *io, const faam_chapter *chapters, uint32_t count, uint32_t flags);
```

### Demuxer lifecycle

1. Open with a `faam_demuxer_config` from `faam_demuxer_config_init()` (`flags`
   stays 0), or NULL for defaults. Open retains `moov` and indexes progressive
   samples; no `moov` means zero tracks.
2. Get the held and total track counts, then query track info by index. Every other
   track call takes the returned `track_id`.
   - Codec data is copied into your buffer; NULL with capacity 0 queries the length,
     and a too-small buffer returns `FAAM_ERR_OUTPUT_TOO_SMALL` with the required
     length.
   - The major brand is a 5-byte NUL-terminated string with padding spaces kept, and
     is empty without `ftyp`.
3. Set `faam_frame_loc.struct_size` and call `next_frame_loc` to inspect the next
   sample without advancing. `flags` carries `FAAM_FRAME_KEYFRAME` for a sync sample
   (all audio samples are keyframes). `read_frame` reads and advances; a NULL buffer
   skips the frame, and a too-small buffer reports the needed size without advancing.
   Stop on `FAAM_END_OF_STREAM` and report negative statuses. The `faam_demuxer_get_*`
   queries take a const handle and never move the cursor.
4. Read tags with `faam_demuxer_get_num_metadata()` and the indexed getter.
5. Close the handle. It does not close your stream.

Track info includes timescale, sample rate, channels, total frames and duration,
maximum frame size, language, the big-endian sample-entry `fourcc`, flags,
width and height, and clockwise rotation (0, 90, 180 or 270; other matrices read 0).

- `FAAM_TRACK_OTHER` marks other handlers, including text, timecode and metadata.
- `FAAM_TRACK_INFO_INBAND_PARAMS` marks avc3/hev1 entries.
- Progressive duration is in track ticks. For fragmented files `total_frames` is 0,
  duration comes from `mehd` (0 before finalization), and the maximum frame size is
  the largest seen so far (0 before the first fragment loads).

### Muxer lifecycle

1. `faam_muxer_config_init(&cfg, sizeof cfg)`. Point `tracks` at an array of
   `faam_track_config` and set `num_tracks` (1 to `max_tracks`). Every element needs
   the same `struct_size`. Audio is `FAAM_TRACK_AUDIO` / `FAAM_CODEC_AAC`; video is
   `FAAM_TRACK_VIDEO` with `FAAM_CODEC_H264` or `FAAM_CODEC_H265`.
2. Optionally set `creation_time`, `fragment_ms`, `metadata` / `num_metadata`,
   `chapters` / `num_chapters` and `gapless`. A config is required; a NULL config is
   rejected. `flags` accepts only:
   - `FAAM_MUXER_M4B`: progressive audio-only M4B branding
   - `FAAM_MUXER_CONSTANT_RATE`: esds `maxBitrate` equals `avgBitrate`
   - `FAAM_MUXER_FASTSTART`: see [Moving the index to the front](#moving-the-index-to-the-front)

   Any other bit is `FAAM_ERR_INVALID_ARGUMENT`.
3. `faam_muxer_open()`. A track ID of 0 auto-assigns; read IDs back in config order
   with `faam_muxer_get_track_id()`. Explicit IDs must be unique. Progressive open
   writes ftyp and mdat. Video and fragmented files use `isom`; otherwise the brand
   is M4A or M4B.
4. Write one encoded access unit per `faam_muxer_write_frame()` call, in decode order
   per track; tracks may interleave. `duration_ticks` is the DTS delta and
   `cts_offset` is PTS minus DTS. `frame_bytes` must be nonzero. Pass
   `FAAM_FRAME_KEYFRAME` for video sync samples; audio ignores it.
5. Finalize to write the progressive moov and sample tables and patch mdat, or to
   close the last fragment. Repeating a successful finalize succeeds; writing or
   updating afterwards is invalid. `faam_muxer_get_info()` (set `struct_size`)
   returns `frame_count`, `duration_ticks`, `file_bytes`, max and average bitrate and
   `max_frame_size`. Close on every path and check its sticky status.

Notes:

- AAC: timescale and sample rate are the audio rate, with 1024 ticks per frame.
  HE-AAC uses the core rate, still 1024 ticks per frame.
- Audio track flags must be 0. GENERIC and OTHER tracks cannot be muxed.
- Video needs dimensions and decoder configuration; only progressive Annex-B H.264
  may derive it.
- Codec data is at most `FAAM_CODEC_DATA_MAX` (1024) bytes.
- Creation time is Unix seconds, 0 for unset. Version-1 time boxes avoid wrapping when
  the 1904-epoch value reaches 2040.

### Allocation and ownership

Open owns every instance allocation and close releases it.

- Muxer handles, tracks, fragment indexes and oversized codec configs are separate
  blocks.
- Progressive sample tables and demuxer indexes and strings allocate as needed.
- A fragmented muxer allocates its whole index at open and nothing while muxing.
- The demuxer builds its final sample index from views into the loaded `moov`
  buffer, avoiding duplicate allocations.

A source build can override `AllocMemory` and `FreeMemory` together with `-D` or a
force-included header (default `malloc`/`free`). Return `malloc`-aligned storage and
accept NULL in `FreeMemory`. Default builds grow buffers with libc `realloc`;
custom-allocator builds allocate, copy and free so growth stays in the custom
memory. There are no separate fast-memory or realloc hooks.

**Lifetimes**

- Tracks and codec data are read or copied at open.
- Progressive metadata and chapters are borrowed until finalize. Supply them at open,
  or bind metadata later with `FAAM_UPDATE_METADATA`. Fragmented metadata and
  chapters must be in the config; they are written at open.
- Strings, artwork and list contents must stay valid through finalize or update, as
  the header specifies.
- Demuxer output pointers live until close. Library information and error strings
  are static.
- Metadata output is read by index, never as an array pointer.

#### Running without a heap

A fixed pool can replace the system heap. Use the `pool_alloc` / `pool_free` bump
allocator from the [FAAC example](libfaac.md#running-without-a-heap) and build libfaam
with `-DAllocMemory=pool_alloc -DFreeMemory=pool_free`.

- Use one FAAM operation or instance at a time with this pool and serialize access.
- Reset only after close or a failed open or standalone update, once no pool-backed
  instance or borrowed output remains. If the pool is shared across libraries, reset
  only after every pool-backed handle is closed.
- Exhaustion returns `FAAM_ERR_NO_MEMORY`.
- A bump allocator never reuses freed blocks, so budget every allocation over the
  whole operation: temporary parsing buffers, and both old and new buffers during
  growth. Progressive tables grow with recording length, and fragmented demuxing
  allocates temporary buffers per fragment. Use a reclaiming pool for long or
  unbounded work.
- Fragmented muxing allocates everything at open and nothing while writing or
  finalizing, so a bounded static pool is practical there.
- I/O and PCM buffers, stack and the stream callback's storage are outside the pool.

## Metadata, gapless and chapters

### Gapless

`faam_gapless_info` holds `encoder_delay`, `end_padding` and `total_samples`, in
samples of the audio track timescale (the core rate for HE-AAC). You supply the
codec's priming.

- `cfg.gapless` provides iTunSMPB and every audio edit list; `track.gapless`
  overrides that track's edit list. The edit list needs a nonzero delay.
- Progressive finalize derives an unknown total from the coded duration minus
  delay and padding, saturating at 0 for an empty programme.
- Fragmented gapless data is written at open, and `total_samples` must be given for
  its edit list.
- `faam_demuxer_get_track_gapless()` takes a track ID, or 0 for the first audio
  track. iTunSMPB wins on the first audio track; otherwise each track uses its own
  edit list. Missing data reads as zeros.

### Metadata entries

Metadata is a pointer and count of `faam_metadata_entry`: a 4-byte `key`, 24-bit
`data_type`, 32-bit `locale`, `data` / `data_bytes`, `flags`, and optional freeform
`mean` / `name`. No per-field code is needed for new tags. Set all struct sizes and
zero other fields first.

- Lyrics use `FAAM_FOURCC(0xa9, 'l', 'y', 'r')`: the copyright byte in an atom key is
  one byte, **not** a UTF-8 character.
- UTF-8 payloads use `FAAM_METADATA_UTF8`; the length excludes the terminating NUL.
  Line breaks are preserved.
- Integers use `FAAM_METADATA_INTEGER` with big-endian bytes of the right width.
- Unknown 24-bit types, arbitrary binary data and locale values pass through without
  interpretation or validation.

| Key | Representation |
|---|---|
| `©nam`, `©ART`, `©alb`, sort names, lyrics, descriptions | UTF-8 |
| `tmpo`, `©mvi`, `©mvc` | big-endian integer, 2 bytes |
| `shwm`, `cpil` | integer, 1 byte |
| `trkn`, `disk` | implicit bytes: reserved u16, number u16, total u16, reserved u16, all big-endian |
| `covr` | raw image bytes; type JPEG 13, PNG 14, GIF 12 or BMP 27 |

- Podcast and TV tags (`stik`, `pcst`, `tvsh`) and future native keys use the same
  API even when the CLI has no named option for them. Year, comment and encoder are
  ordinary UTF-8 entries.
- Repeated entries carry multiple cover images or multiple values of a key.
  Repeated atoms and multiple data children inside one atom are both enumerated, in
  file order.
- A freeform entry has key `FAAM_FOURCC('-', '-', '-', '-')` and a nonempty `name`;
  NULL or empty `mean` defaults to `com.apple.iTunes`. Native entries must have NULL
  `mean` and `name`. MusicBrainz IDs, ISRC, ReplayGain and similar tags need no extra
  API; use the namespace and tag name each tool expects.

Reading: `faam_demuxer_get_num_metadata()` then `faam_demuxer_get_metadata_entry()` by
index. Payloads, names and namespaces stay valid until close. Payload lengths are
authoritative, so never treat binary or NUL-containing data as C strings. Opaque
atoms without a usable data child are not enumerated but survive unrelated edits.
Gapless data stays in its own API because it also drives track timing.

### Chapters

Each chapter has a `start_ms` and a non-NULL UTF-8 `title`, stored up to 255 bytes.
Read them with `faam_demuxer_get_num_chapters()` and `faam_demuxer_get_chapter()`
using an initialized output struct. Nero `chpl` allows at most 255 chapters;
QuickTime chapter tracks are not interpreted.

## Editing finished files

`faam_update_metadata_stream(io, entries, count, flags)` and
`faam_update_chapters_stream(io, chapters, count, flags)` edit finished progressive
files in place, with no remux. Supply only the entries to change; there is no need to
open a demuxer and copy existing tags. Close any open demuxer and reopen it before
reading the modified file. Only iTunes `ilst` metadata is handled; in-place editing
rejects `mdta`.

**Tags**

- With flags 0, each supplied key replaces **all** existing values of that key.
  Identity is the native 4-byte key, or key plus namespace and name for freeform
  tags; locale and data type do not affect it. Repeated input keys supply multiple
  replacement values.
- `FAAM_METADATA_REMOVE` with NULL data and zero length removes a key. Do not mix a
  removal and values for the same key. An empty payload without REMOVE writes an
  explicit empty value.
- Untouched atoms, including unknown atoms, artwork and iTunSMPB, are preserved
  byte-for-byte in their original order.
- `FAAM_METADATA_UPDATE_CLEAR` drops all existing `ilst` atoms first. Chapters stay
  intact, and chapter updates keep metadata.
- Invalid arguments and malformed `ilst` child lengths fail before anything is
  rewritten.

**Chapters and file layout**

- Chapter `flags` are reserved and must be 0; count 0 writes an empty chapter list.
- Missing `udta`, `meta`, `ilst` and `chpl` atoms are created.
- Growing `moov` adjusts moov-first chunk offsets; shrinking leaves free space.
- A 64-bit size header on `moov` or an edited box is unsupported and leaves the file
  unchanged. No `moov` is `FAAM_ERR_BAD_CONTAINER`.
- A fragmented recording, finalized or cut short by a crash, can be tagged too; its
  completed fragments stay playable.
- Edits are not transactional: an interrupted write can damage the file.

## Codec data and language

Set the informational PCM `audio_sample_size` in `faam_track_config` before open; 0
selects 16 bits. It does not change the encoded audio.

For late progressive configuration, fill `faam_muxer_update_params` (set `struct_size`
and `flags`) and call `faam_muxer_update()`:

| Flag | Fields applied |
|---|---|
| `FAAM_UPDATE_CREATION_TIME` | `creation_time` (Unix seconds) |
| `FAAM_UPDATE_GAPLESS` | `gapless` pointer |
| `FAAM_UPDATE_CODEC_DATA` | `track_id`, `codec_data`, `codec_data_len` (copied, at most `FAAM_CODEC_DATA_MAX`) |
| `FAAM_UPDATE_LANGUAGE` | `track_id`, `language` |
| `FAAM_UPDATE_METADATA` | `metadata`, `num_metadata` (borrowed until finalize) |

- Unselected fields are ignored. Fragmented update returns `FAAM_ERR_UNSUPPORTED`;
  update after finalize returns `FAAM_ERR_INVALID_ARGUMENT`.
- Language is the first three ISO 639-2/T letters, lowercased. Short input or any
  non-letter gives `und`.
- Any AAC object type is carried as opaque `codec_data`. xHE-AAC/USAC passes through
  the same way, untested end to end, and its channel count is not derived from the
  USAC configuration.

## Video tracks

Set width, height, timescale (often 90000), `rotation_degrees` and `codec_data` (the
avcC or hvcC payload).

- A nonzero `cts_offset` allows negative values for B-frames, needs `FAAM_LIB_VIDEO`
  and is invalid on audio. Progressive files write `ctts` only for nonzero offsets,
  and an edit list compensates the first offset.
- Set `FAAM_TRACK_INBAND_PARAMS` when samples repeat parameter sets; this selects
  avc3/hev1 and still needs `codec_data` for the configuration boxes.
- `FAAM_TRACK_ANNEXB` accepts `00 00 01` or `00 00 00 01` start codes and writes
  4-byte length-prefixed NAL units without modifying your buffer or allocating.
- Progressive H.264 may omit `codec_data` if the first access unit carries SPS then
  PPS to derive avcC; finalize fails if none was found. H.265 always needs a supplied
  hvcC. Dimensions are always required.

## Recording to removable storage

Set `fragment_ms` above 0 and check `FAAM_LIB_FRAGMENTED`. `faam_muxer_open()`
allocates the fragment index up front and writes ftyp and moov, then moof/mdat
fragments follow.

- A fragment closes at the first video keyframe after the requested duration
  (audio-only: accumulated audio duration), or earlier at the sample-index capacity
  (about 128 samples per second of `fragment_ms`, at least 64) or 1 GiB.
- Supply `flush` to persist every completed fragment; seek and tell are still
  required.
- A crash loses at most the open fragment. The demuxer ignores a fragment with
  truncated data and exposes completed ones.
- Finalize closes the last fragment and writes the `mehd` duration. Closing without
  finalize keeps completed fragments; a progressive file closed without finalize has
  no `moov` and cannot be recovered.
- Fragmented video needs `codec_data` at open. Metadata, chapters and gapless are
  fixed at open; update is unsupported. B-frame offsets go in `trun` without shifting
  the first presentation time to zero.

## Moving the index to the front

A progressive file ends with its `moov`, so a player fetching it over HTTP needs the
whole file or a range request before it can start. `FAAM_MUXER_FASTSTART` fixes that:
finalize writes `moov` at the end as usual, rebuilds it with the shifted chunk offsets
(`stco` entries that no longer fit 32 bits become `co64`), and copies the sample data
forward to make room in front.

- The io must provide `read` (`FAAM_ERR_UNSUPPORTED` otherwise), and the flag is
  invalid with `fragment_ms`.
- The copy rewrites the whole file: a second pass of I/O and flash wear, and the file
  is not a valid MP4 until it finishes, so a power cut during finalize loses the
  recording. A read failure during the copy is `FAAM_ERR_IO_READ` and, like a write
  failure, is sticky.
- Recorders writing to SD should leave it off and use fragmented files. The default
  writes `moov` last. Tag and chapter updates work on either layout.

## Files with many tracks

`faam_demuxer_get_num_tracks()` returns the held count and optionally the total.
Only indices below held are valid for `get_track_info`; the total can exceed
`faam_library_info.max_tracks`. Store each returned `track_id` and use it for codec
data and gapless queries; IDs are not indices.

- Generic sample entries keep their fourcc and readable payloads; other handlers use
  `FAAM_TRACK_OTHER`.
- An `mp4v` (MPEG-4 Part 2) video entry is a generic one that also reports width and
  height. `faam_demuxer_get_codec_data()` returns its esds DecoderSpecificInfo (the
  VOL header) for a decoder; the muxer does not accept it.
- `next_frame_loc` interleaves all held tracks in file order. Filter on its
  `track_id` and read or skip every frame to advance.

## Reading MP4 for the FAAD decoder

libfaam parses the container and [libfaad](libfaad.md) decodes its AAC access units.
This replaces the legacy `faad.h` / mp4ff sequence:

| Legacy step | Current calls |
|---|---|
| `mp4ff_open_read` | `faam_demuxer_open(NULL, &io, &demuxer)` with memory or stdio `faam_io` callbacks |
| Find the audio track | `faam_demuxer_get_num_tracks()` and `faam_demuxer_get_track_info()`; pick `codec_id == FAAM_CODEC_AAC` and match `track_id` for an explicit selection |
| `mp4ff_get_decoder_config` | `faam_demuxer_get_codec_data()` copies the track's AudioSpecificConfig (ASC) into your buffer |
| `NeAACDecOpen` + `NeAACDecInit2` | Init `faad_config`, set `stream_format = FAAD_STREAM_RAW`, then `faad_decoder_open(&cfg, asc, asc_len, &decoder)` |
| `mp4ff_read_sample` + decode loop | `faam_demuxer_next_frame_loc()`, `faam_demuxer_read_frame()`, `faad_decode_frame()`. Filter `loc.track_id`; skip other tracks with `read_frame(d, NULL, 0, &bytes)` |
| mp4ff gapless and metadata | `faam_demuxer_get_track_gapless()`, `faam_demuxer_get_num_metadata()`, `faam_demuxer_get_metadata_entry()` |

- A file may hold several tracks. The CLI's `faad --track <id>` selects one (`faad -i`
  lists them). An AAC codec ID alone does not guarantee a profile libfaad supports.
- `frontend/mp4read.c` shows a memory-backed adapter: it skips each frame to advance
  the demuxer, validates `file_offset` and `frame_bytes` against the input size, then
  feeds the original bytes to libfaad without a copy. With a file stream you can
  likewise read at `file_offset` yourself, but must still advance with `read_frame`.

**Gapless.** Delay and padding are in track-timescale samples (the core rate for
HE-AAC), so convert them to output samples and add libfaad's `decoder_delay` before
trimming; the conversion and the HE-AAC timescale rule are in
[Decoder delay](libfaad.md#decoder-delay) and `frontend/faad_main.c`. Query gapless
data by the selected track ID (see [Gapless](#gapless)).

**Tags, chapters and ownership.** Read them as in [Metadata entries](#metadata-entries)
and [Chapters](#chapters). Metadata, artwork and chapter titles are borrowed until
demuxer close, and the ASC is copied into your buffer so it need not outlive decoder
open. Keep the stream context alive through demuxer close (it does not close the
`FILE`), and close both handles and free your buffers on every exit path.

`m4a_decode.c` decodes the first AAC track to native-endian interleaved signed 16-bit
PCM, with no header or gapless trimming. It stops on demux or decode errors and
accepts recovered PCM. It needs `stdio_io.h` from [Stream I/O](#stream-io) for 64-bit
seeking.

```c
/* m4a_decode.c */
#include "stdio_io.h"
#include <faad3.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    FILE *in = NULL, *out = NULL;
    faam_demuxer *d = NULL;
    faad_decoder *dec = NULL;
    uint8_t *packet = NULL;
    void *pcm = NULL;
    uint32_t held = 0, id = 0, asc_len = 0, cap = 0;
    uint8_t asc[FAAM_CODEC_DATA_MAX];
    faam_status ms = FAAM_OK;
    faad_status ds = FAAD_OK;
    faad_config cfg;
    faad_stream_info info = { .struct_size = sizeof(info) };
    faam_frame_loc loc = { .struct_size = sizeof(loc) };
    int result = 1;
    if (argc != 3) {
        fprintf(stderr, "Usage: %s input.m4a output.pcm\n", argv[0]); return 1;
    }
    in = fopen(argv[1], "rb");
    if (!in) { perror(argv[1]); goto done; }
    faam_io io = stdio_io(in);
    if ((ms = faam_demuxer_open(NULL, &io, &d)) != FAAM_OK) goto done;
    if ((ms = faam_demuxer_get_num_tracks(d, &held, NULL)) != FAAM_OK) goto done;
    for (uint32_t i = 0; i < held; ++i) {
        faam_track_info t = { .struct_size = sizeof(t) };
        if ((ms = faam_demuxer_get_track_info(d, i, &t)) != FAAM_OK) goto done;
        if (t.track_type == FAAM_TRACK_AUDIO && t.codec_id == FAAM_CODEC_AAC) {
            id = t.track_id; break;
        }
    }
    if (!id) { fprintf(stderr, "No AAC track\n"); goto done; }
    if ((ms = faam_demuxer_get_codec_data(d, id, asc, sizeof(asc), &asc_len)) != FAAM_OK) goto done;
    if ((ds = faad_config_init(&cfg, sizeof(cfg))) != FAAD_OK) goto done;
    cfg.stream_format = FAAD_STREAM_RAW;
    if ((ds = faad_decoder_open(&cfg, asc, asc_len, &dec)) != FAAD_OK) goto done;
    if ((ds = faad_decoder_get_info(dec, &info)) != FAAD_OK) goto done;
    pcm = malloc(info.max_output_bytes);
    if (!pcm) { fprintf(stderr, "PCM allocation failed\n"); goto done; }
    out = fopen(argv[2], "wb");
    if (!out) { perror(argv[2]); goto done; }
    while ((ms = faam_demuxer_next_frame_loc(d, &loc)) == FAAM_OK) {
        uint32_t n = 0, consumed = 0, written = 0, flags = 0;
        if (loc.track_id != id) {
            if ((ms = faam_demuxer_read_frame(d, NULL, 0, &n)) != FAAM_OK) goto done;
            continue;
        }
        if (loc.frame_bytes > cap) {
            uint8_t *p = realloc(packet, loc.frame_bytes);
            if (!p) { fprintf(stderr, "Packet allocation failed\n"); goto done; }
            packet = p; cap = loc.frame_bytes;
        }
        if ((ms = faam_demuxer_read_frame(d, packet, cap, &n)) != FAAM_OK) goto done;
        ds = faad_decode_frame(dec, packet, n, &consumed,
                               pcm, info.max_output_bytes, &written, &flags);
        if (ds != FAAD_OK) goto done;
        if (flags & FAAD_FRAME_FORMAT_CHANGED)
            if ((ds = faad_decoder_get_info(dec, &info)) != FAAD_OK) goto done;
        if (fwrite(pcm, 1, written, out) != written) { perror("write"); goto done; }
    }
    if (ms == FAAM_END_OF_STREAM) result = 0;
done:
    if (ms < 0) fprintf(stderr, "%s\n", faam_strerror(ms));
    if (ds != FAAD_OK) fprintf(stderr, "%s\n", faad_strerror(ds));
    if (faad_decoder_close(&dec) != FAAD_OK) result = 1;
    if (faam_demuxer_close(&d) != FAAM_OK) result = 1;
    free(packet); free(pcm);
    if (in) fclose(in);
    if (out && fclose(out) != 0) { perror("close output"); result = 1; }
    return result;
}
```

For an installed build, compile with `pkg-config --cflags --libs faam faad3`. For
source-tree static archives built in `b/`:

```sh
cc -std=c11 -Wall -Wextra -Iinclude -Icommon \
  m4a_decode.c b/libfaam/libfaam.a b/libfaad/libfaad3.a -lm -o m4a_decode
```

Run `./m4a_decode input.m4a output.pcm`. On a little-endian host, compare with
`faad --no-gapless -f raw -o reference.pcm input.m4a`. The CLI writes little-endian
PCM, so byte-swap the example's output first on a big-endian host. Without
`--no-gapless` the CLI trims container priming, padding and decoder delay, so its
output differs.

## Thread safety

Serialize access to one handle. Separate handles are independent and callbacks run
on the calling thread. The library has no global mutable state.

## Examples

Each example uses `stdio_io.h` from [Stream I/O](#stream-io), prints errors to
stderr, and returns nonzero on failure.

**`inspect.c`** prints tracks, tags and chapters and counts frames per track:

```c
/* inspect.c */
#include "stdio_io.h"
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "Usage: %s file.m4a\n", argv[0]); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }

    faam_io io = stdio_io(f);
    faam_demuxer *d = NULL;
    faam_status st = faam_demuxer_open(NULL, &io, &d);
    if (st != FAAM_OK) {
        fprintf(stderr, "%s: %s\n", argv[1], faam_strerror(st));
        fclose(f);
        return 1;
    }

    uint32_t tracks = 0, total = 0;
    st = faam_demuxer_get_num_tracks(d, &tracks, &total);
    uint32_t *frames = calloc(tracks ? tracks : 1, sizeof(*frames));
    uint32_t *ids = calloc(tracks ? tracks : 1, sizeof(*ids));
    if (st != FAAM_OK || !frames || !ids) {
        free(frames); free(ids); faam_demuxer_close(&d); fclose(f);
        return 1;
    }
    printf("tracks: %u held, %u total\n", tracks, total);
    for (uint32_t t = 0; st == FAAM_OK && t < tracks; t++) {
        faam_track_info ti = { .struct_size = sizeof(ti) };
        st = faam_demuxer_get_track_info(d, t, &ti);
        if (st != FAAM_OK) break;
        ids[t] = ti.track_id;
        printf("track %u: %s, timescale %u, %u frames\n", ti.track_id,
               ti.track_type == FAAM_TRACK_VIDEO ? "video" :
               ti.track_type == FAAM_TRACK_AUDIO ? "audio" : "other", ti.timescale, ti.total_frames);
    }

    uint32_t meta_count = 0;
    if (st == FAAM_OK) st = faam_demuxer_get_num_metadata(d, &meta_count);
    for (uint32_t i = 0; st == FAAM_OK && i < meta_count; i++) {
        faam_metadata_entry entry = { .struct_size = sizeof(entry) };
        st = faam_demuxer_get_metadata_entry(d, i, &entry);
        if (st == FAAM_OK)
            printf("tag %08x: type %u, locale %u, %u bytes\n", entry.key,
                   entry.data_type, entry.locale, entry.data_bytes);
    }
    uint32_t n = 0;
    if (st == FAAM_OK) st = faam_demuxer_get_num_chapters(d, &n);
    for (uint32_t i = 0; st == FAAM_OK && i < n; i++) {
        faam_chapter chapter = { .struct_size = sizeof(chapter) };
        st = faam_demuxer_get_chapter(d, i, &chapter);
        if (st == FAAM_OK)
            printf("chapter %u: %llu ms %s\n", i + 1,
                   (unsigned long long)chapter.start_ms, chapter.title);
    }
    while (st == FAAM_OK) {
        faam_frame_loc loc = { .struct_size = sizeof(loc) };
        st = faam_demuxer_next_frame_loc(d, &loc);
        if (st != FAAM_OK) break;
        for (uint32_t t = 0; t < tracks; t++)
            if (ids[t] == loc.track_id) frames[t]++;
        uint32_t skipped;
        st = faam_demuxer_read_frame(d, NULL, 0, &skipped);
    }
    for (uint32_t t = 0; t < tracks; t++) printf("track %u: %u frames read\n", ids[t], frames[t]);

    free(frames); free(ids);
    faam_demuxer_close(&d);
    fclose(f);
    if (st < 0) fprintf(stderr, "%s\n", faam_strerror(st));
    return st < 0 ? 1 : 0;
}
```

**`mux_adts.c`** writes an ADTS AAC file to M4A with gapless information, tags and
chapters.

- The delay comes from your encoder: 1024 for libfaac AAC-LC, and for HE-AAC the
  core-rate value (1040 from libfaac); see [Gapless](#gapless). The program leaves
  `end_padding` at 0, so it does not trim the end.
- The loop assumes one raw data block of 1024 samples per ADTS frame and a constant
  configuration.
- It rebuilds only the ADTS-signaled ASC. Extensions such as explicit SBR need
  encoder-supplied codec data for full signaling.

```c
/* mux_adts.c */
#include "stdio_io.h"
#include <stdlib.h>
#include <string.h>

static const uint32_t rates[] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000,
                                  22050, 16000, 12000, 11025, 8000, 7350 };

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "Usage: %s in.aac out.m4a\n", argv[0]); return 1; }
    FILE *in = fopen(argv[1], "rb");
    if (!in) { perror(argv[1]); return 1; }
    if (faam_fseek(in, 0, SEEK_END) != 0) { fclose(in); return 1; }
    int64_t len = (int64_t)faam_ftell(in);
    if (faam_fseek(in, 0, SEEK_SET) != 0 || len < 0 || (uint64_t)len > SIZE_MAX) {
        fclose(in); return 1;
    }
    uint8_t *adts = len > 7 ? malloc((size_t)len) : NULL;
    if (!adts || fread(adts, 1, (size_t)len, in) != (size_t)len) {
        fprintf(stderr, "%s: cannot read\n", argv[1]);
        free(adts);
        fclose(in);
        return 1;
    }
    fclose(in);

    unsigned aot = (adts[2] >> 6) + 1, sr = (adts[2] >> 2) & 15;
    unsigned ch = ((adts[2] & 1) << 2) | (adts[3] >> 6);
    if (adts[0] != 0xFF || (adts[1] & 0xF0) != 0xF0 || sr > 12 || !ch) {
        fprintf(stderr, "%s: not ADTS\n", argv[1]);
        free(adts);
        return 1;
    }
    uint8_t asc[2] = { (uint8_t)((aot << 3) | (sr >> 1)), (uint8_t)(((sr & 1) << 7) | (ch << 3)) };

    FILE *out = fopen(argv[2], "wb");
    if (!out) { perror(argv[2]); free(adts); return 1; }
    faam_io io = stdio_io(out);

    const uint8_t track_index[8] = {0, 0, 0, 1, 0, 10, 0, 0};
    faam_metadata_entry entries[] = {
        { .struct_size = sizeof(faam_metadata_entry), .key = FAAM_FOURCC(0xa9, 'n', 'a', 'm'),
          .data_type = FAAM_METADATA_UTF8, .data = "Title", .data_bytes = 5 },
        { .struct_size = sizeof(faam_metadata_entry), .key = FAAM_FOURCC(0xa9, 'A', 'R', 'T'),
          .data_type = FAAM_METADATA_UTF8, .data = "Artist", .data_bytes = 6 },
        { .struct_size = sizeof(faam_metadata_entry), .key = FAAM_FOURCC('t', 'r', 'k', 'n'),
          .data_type = FAAM_METADATA_IMPLICIT, .data = track_index, .data_bytes = sizeof(track_index) },
        { .struct_size = sizeof(faam_metadata_entry), .key = FAAM_FOURCC('-', '-', '-', '-'),
          .mean = "org.example", .name = "Source", .data_type = FAAM_METADATA_UTF8,
          .data = "ADTS", .data_bytes = 4 }
    };

    faam_chapter chapters[] = {
        { .struct_size = sizeof(faam_chapter), .start_ms = 0, .title = "Intro" },
        { .struct_size = sizeof(faam_chapter), .start_ms = 30000, .title = "Main part" }
    };

    faam_muxer_config cfg;
    faam_muxer_config_init(&cfg, sizeof(cfg));
    faam_gapless_info gapless = { .struct_size = sizeof(gapless), .encoder_delay = 1024 };
    cfg.gapless = &gapless;
    cfg.metadata = entries; cfg.num_metadata = sizeof(entries) / sizeof(entries[0]);
    cfg.chapters = chapters;
    cfg.num_chapters = 2;

    faam_track_config track = {
        .struct_size = sizeof(track),
        .track_type = FAAM_TRACK_AUDIO,
        .codec_id = FAAM_CODEC_AAC,
        .timescale = rates[sr],
        .sample_rate = rates[sr],
        .channels = ch,
        .language = "und",
        .codec_data = asc,
        .codec_data_len = 2,
    };
    uint32_t id = 0;
    faam_muxer *m = NULL;
    cfg.tracks = &track;
    cfg.num_tracks = 1;
    faam_status st = faam_muxer_open(&cfg, &io, &m);
    if (st == FAAM_OK) st = faam_muxer_get_track_id(m, 0, &id);
    if (st != FAAM_OK) {
        faam_muxer_close(&m);
        fprintf(stderr, "mux setup: %s\n", faam_strerror(st));
        fclose(out);
        free(adts);
        return 1;
    }

    int64_t pos = 0;
    for (; st == FAAM_OK && pos + 7 <= len; ) {
        unsigned hdr = (adts[pos + 1] & 1) ? 7 : 9;
        unsigned flen = ((adts[pos + 3] & 3) << 11) | (adts[pos + 4] << 3) | (adts[pos + 5] >> 5);
        if (adts[pos] != 0xFF || (adts[pos + 1] & 0xF6) != 0xF0 ||
            ((adts[pos + 2] >> 6) + 1u) != aot ||
            ((adts[pos + 2] >> 2) & 15u) != sr ||
            (((adts[pos + 2] & 1u) << 2) | (adts[pos + 3] >> 6)) != ch ||
            flen <= hdr || pos + flen > len || (adts[pos + 6] & 3)) {
            st = FAAM_ERR_INVALID_ARGUMENT; break;
        }
        st = faam_muxer_write_frame(m, id, adts + pos + hdr, flen - hdr, 1024, 0, FAAM_FRAME_KEYFRAME);
        pos += flen;
    }
    if (st == FAAM_OK && pos != len) st = FAAM_ERR_INVALID_ARGUMENT;
    if (st == FAAM_OK) st = faam_muxer_finalize(m);
    if (st != FAAM_OK) fprintf(stderr, "mux: %s\n", faam_strerror(st));

    faam_status closed = faam_muxer_close(&m);
    if (st == FAAM_OK) st = closed;
    fclose(out);
    free(adts);
    return st == FAAM_OK ? 0 : 1;
}
```

**`record.c`** writes a fragmented, crash-safe file with a preallocated fragment
index. It needs the `muxer-fragmented` option. Its synthetic payload only exercises
container writing; use encoded AAC-LC stereo 44.1 kHz access units for playback.

```c
/* record.c */
#include "stdio_io.h"
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "Usage: %s out.m4a\n", argv[0]); return 1; }
    FILE *out = fopen(argv[1], "wb");
    if (!out) { perror(argv[1]); return 1; }
    faam_io io = stdio_io(out);

    static const uint8_t asc[2] = { 0x12, 0x10 };  /* AAC-LC, 44.1 kHz, stereo */
    faam_muxer_config cfg;
    faam_muxer_config_init(&cfg, sizeof(cfg));
    cfg.fragment_ms = 2000;
    faam_track_config track = {
        .struct_size = sizeof(track), .track_type = FAAM_TRACK_AUDIO, .codec_id = FAAM_CODEC_AAC,
        .timescale = 44100, .sample_rate = 44100, .channels = 2, .codec_data = asc, .codec_data_len = 2,
    };
    uint32_t id = 0;
    cfg.tracks = &track;
    cfg.num_tracks = 1;

    faam_muxer *m = NULL;
    faam_status st = faam_muxer_open(&cfg, &io, &m);
    if (st == FAAM_OK) st = faam_muxer_get_track_id(m, 0, &id);
    if (st != FAAM_OK || !m) {
        faam_muxer_close(&m);
        fprintf(stderr, "open: %s\n", faam_strerror(st));
        fclose(out);
        return 1;
    }

    uint8_t frame[128] = { 0x21 };  /* stands in for one encoded access unit */
    for (int i = 0; i < 500 && st == FAAM_OK; i++)
        st = faam_muxer_write_frame(m, id, frame, sizeof(frame), 1024, 0, FAAM_FRAME_KEYFRAME);
    if (st == FAAM_OK) st = faam_muxer_finalize(m);
    if (st != FAAM_OK) fprintf(stderr, "record: %s\n", faam_strerror(st));

    faam_status closed = faam_muxer_close(&m);
    if (st == FAAM_OK) st = closed;
    fclose(out);
    return st == FAAM_OK ? 0 : 1;
}
```

**`tag.c`** changes one tag of an existing file and keeps the others, as `faam tag`
does. Only the changed key is supplied.

```c
/* tag.c */
#include "stdio_io.h"
#include <string.h>

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "Usage: %s file.m4a title\n", argv[0]); return 1; }
    FILE *f = fopen(argv[1], "r+b");
    if (!f) { perror(argv[1]); return 1; }
    faam_io io = stdio_io(f);

    faam_metadata_entry title = { .struct_size = sizeof(title),
        .key = FAAM_FOURCC(0xa9, 'n', 'a', 'm'), .data_type = FAAM_METADATA_UTF8,
        .data = argv[2], .data_bytes = (uint32_t)strlen(argv[2]) };
    faam_status st = faam_update_metadata_stream(&io, &title, 1, 0);
    fclose(f);
    if (st != FAAM_OK) fprintf(stderr, "%s: %s\n", argv[1], faam_strerror(st));
    return st == FAAM_OK ? 0 : 1;
}
```

## ABI compatibility

The API and ABI are stable within a major version (SONAME) on a given platform ABI.
Layouts follow the target's pointer width and alignment, so 32- and 64-bit differ,
and the structs are not a serialization format.

- Every struct you fill or receive starts with `uint32_t struct_size`. Initialize
  `faam_muxer_config`, `faam_demuxer_config`, `faam_track_config` and `faam_io` with
  `faam_muxer_config_init()`, `faam_demuxer_config_init()`,
  `faam_track_config_init()` and `faam_io_init()` (pass `sizeof` the struct).
- `faam_chapter`, `faam_metadata_entry`, `faam_gapless_info` and
  `faam_muxer_update_params` have no init function: use `{ .struct_size = sizeof(x) }`.
  Set `.struct_size` on every output struct and on nothing else.
- Initialization stores `min(caller size, library size)` in `struct_size`. That
  write-back is the support probe: before using a field appended in a newer release,
  check that `struct_size` covers it. Structs without an init function
  (`faam_chapter`, `faam_metadata_entry`, `faam_gapless_info`) are also getter
  outputs, so a query reports the same probe. `faam_muxer_update_params` gates every
  field behind a `flags` bit and rejects unknown bits, so a library that predates a
  field refuses the update instead of ignoring it.
- Queries write at most the caller's `struct_size` bytes and set `struct_size` to the
  bytes populated. Reset it before each query.
- The library reads only the input bytes `struct_size` covers; absent fields take
  their zero value. A `struct_size` below the matching `FAAM_*_BASELINE` returns
  `FAAM_ERR_INVALID_ARGUMENT`. A garbage larger value is not detectable, so always
  initialize.
- Input lists (tracks, chapters, metadata entries) are a pointer and a count, strided
  by the first element's `struct_size`, so every element shares one size. Output lists
  use a count getter plus a getter by index. A getter reports the populated bytes in
  `struct_size`, which can be below your `sizeof`, so set `struct_size =
  sizeof(element)` again on every element before passing queried chapters or entries
  back as input; mixed sizes are rejected.
- `faam_demuxer_config.flags` is reserved and must be 0, and so is the `flags`
  argument of `faam_update_chapters_stream()`.
- Tolerate unknown enum values, flag bits and status codes in anything returned.
  `FAAM_OK` is 0; `FAAM_ERR_INVALID_ARGUMENT`, `FAAM_ERR_UNSUPPORTED` and
  `FAAM_ERR_NO_MEMORY` are -1, -2 and -3; higher codes are specific to libfaam.
- Compile-time options never change public layouts. Query `faam_get_library_info()`
  for what a build supports, including `max_tracks`; do not compile against the
  internal track cap.
- `FAAM_VERSION_*` is the project release version that every library reports; the
  SONAME is versioned separately, and `faam_get_library_info()` reports the linked
  version.
- Opaque handle contents, allocation and state sizes, and sample-index limits are
  outside the ABI.

FAAM is free software, licensed under the GNU Lesser General Public License
(LGPL), version 2.1 or later.

Copyright © 2026, Nils Schimmelmann
