# FAAM - MP4 Muxer and Demuxer Library

## Contents

- [Scope](#scope)
- [Interface description](#interface-description)
- [Building and linking](#building-and-linking)
- [Supported content](#supported-content)
- [API (faam.h)](#api-faamh)
  - [Stream I/O](#stream-io)
  - [Error handling](#error-handling)
  - [Function reference](#function-reference)
  - [Demuxer lifecycle](#demuxer-lifecycle)
  - [Muxer lifecycle](#muxer-lifecycle)
  - [Allocation and ownership](#allocation-and-ownership)
  - [Gapless playback, metadata and chapters](#gapless-playback-metadata-and-chapters)
  - [Editing finished files](#editing-finished-files)
  - [Codec data and language](#codec-data-and-language)
  - [Video tracks](#video-tracks)
  - [Recording to removable storage](#recording-to-removable-storage)
  - [Reading and editing tags without remuxing](#reading-and-editing-tags-without-remuxing)
  - [Files with many tracks](#files-with-many-tracks)
  - [Reading MP4 for the FAAD decoder](#reading-mp4-for-the-faad-decoder)
  - [Thread safety](#thread-safety)
  - [Complete examples](#complete-examples)
- [ABI compatibility](#abi-compatibility)

## Scope

FAAM reads and writes ISO BMFF files: MP4, M4A and M4B. The application
supplies encoded access units and decoder configuration; the library does no
encoding or decoding. All stream access uses caller-supplied callbacks.
The `faam` command-line tool in `frontend/` is documented in `faam(1)`.

## Interface description

Include `<faam.h>` and link against `libfaam`. Installed builds provide the
`faam` pkg-config module. Meson builds shared and static libraries by default;
`-Ddefault_library` selects one. The library and header are built and installed
whether or not the `faam` command-line tool is enabled.

## Building and linking

From the source directory, build and install the library with Meson. The
frontends can be turned off:

```sh
meson setup build-faam -Dfrontend=false
meson compile -C build-faam
meson install -C build-faam
```

The `faam` tool is built by default; `-Dmuxer=false` leaves it out. Video and
fragmented files are enabled by default too (see the options below). For an
installed library, compile an example from this guide with:

```sh
cc -std=c11 -Wall -Wextra inspect.c -o inspect $(pkg-config --cflags --libs faam)
```

For a custom installation prefix, add its pkg-config directory to
`PKG_CONFIG_PATH`. Within the source tree, link the library from the build
directory and add `include/` to the include path.

For static linking, use `pkg-config --cflags --libs --static faam` and select an
installation containing only the static library, or use your toolchain's
archive-selection options. On Windows, a program that links the static library must
define `FAAM_STATIC` before including `faam.h`; otherwise the API is declared
`dllimport`. Meson's pkg-config file describes the shared library, so add the define
yourself for a static link.

Building without Meson: compile `libfaam/demux.c`, `mux.c`, `metadata.c`,
`faam_util.c`, `tag.c`, `chapter.c` and `atom_patch.c` with `include/` and
`common/` on the include path. Define `FAAM_STATIC` for an archive and its
consumers; define `FAAM_BUILDING` when building a Windows DLL. Define
`FAAM_MUXER_VIDEO=1` and `FAAM_MUXER_FRAGMENTED=1` to include those features;
leave the macros undefined to omit them. Install `include/faam.h` with the library.

## Supported content

Query `faam_get_library_info()` with `faam_library_info.struct_size` set.
`features` contains `FAAM_FEATURE_*` bits; `max_tracks` is the number one
muxer can write or one demuxer can hold. `version` and `copyright` are static strings.

| Feature | Option (default) | Runtime detection | When absent |
|---|---|---|---|
| AAC, any AOT carried as `codec_data` | Always built | `FAAM_CODEC_AAC` | No build switch. xHE-AAC/USAC passes through as opaque `codec_data`, untested end to end; channels are not derived from USAC configuration. |
| H.264/H.265, including Annex-B input and avc3/hev1 | `muxer-video` (true) | `FAAM_FEATURE_VIDEO` | Video muxing and non-zero frame CTS offsets return `FAAM_ERR_NOT_BUILT`; video sample entries demux as `FAAM_CODEC_GENERIC`, without video sync/CTS parsing. |
| Fragmented MP4 | `muxer-fragmented` (true) | `FAAM_FEATURE_FRAGMENTED` | Fragmented muxing/demuxing returns `FAAM_ERR_NOT_BUILT`. |
| Multi-track | Always built | `faam_library_info.max_tracks` | More tracks in a file are counted but not held; muxer configurations above the limit are invalid. |
| Other sample entries | Demuxer only | `FAAM_CODEC_GENERIC`, `faam_track_info.fourcc` | Payloads can be read; these entries are not muxable. |
| Chapters | Always built | `faam_demuxer_get_num_chapters()` | Nero `chpl` only, at most 255 chapters; no QuickTime chapter-track interpretation. |
| Tags | Always built | `faam_demuxer_get_metadata()` | iTunes `ilst` only; in-place tag editing rejects `mdta` metadata. |

## API (faam.h)

Every fallible call returns `faam_status`; results use out-parameters. Set
`struct_size` on every input and output struct, including nested pointer targets.
Reserved fields and reserved flags must be zero.

### Stream I/O

`faam_io` starts with `struct_size`, followed by `user_data` and the `read`,
`write`, `seek`, `tell` and optional `flush` callbacks. The library copies it;
keep `user_data` alive until close. Seek offsets are absolute, in bytes.

| Operation | Required callbacks |
|---|---|
| Demuxer open | `read`, `seek` |
| Muxer open, both modes | `write`, `seek`, `tell` |
| Tag/chapter updates | `read`, `write`, `seek`, `tell` |

Missing required callbacks return `FAAM_ERR_UNSUPPORTED` before any I/O.
`read` may return a short count: the library retries until satisfied. Zero
means end of data; a negative count or one above the request is
`FAAM_ERR_IO_READ`. During demuxer open, zero inside a structure is treated as
truncation: the file opens with whatever was parsed, possibly no tracks.
`write` must return exactly the requested count. Any other count, or a failed
muxer seek/flush, is `FAAM_ERR_IO_WRITE`. Muxer I/O failures are sticky.
The demuxer does not use write/tell; the muxer does not use read.
Optional flush runs at muxer finalize and at fragmented open and fragment completion.

Save this adapter as `stdio_io.h`. Include it before other headers so the
POSIX declarations and large-file offsets are enabled:

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

### Error handling

`FAAM_OK` is zero, errors are negative, and `FAAM_END_OF_STREAM` is +1.
Test for end of stream explicitly in frame loops; `status < 0` detects errors.
`faam_strerror()` returns a static, non-NULL string.

| Status | Value | Meaning |
|---|---|---|
| `FAAM_OK` | 0 | Success |
| `FAAM_END_OF_STREAM` | +1 | No more frames; not an error |
| `FAAM_ERR_INVALID_ARG` | -1 | NULL required pointer, invalid struct size, configuration, flags or call order |
| `FAAM_ERR_BAD_CONTAINER` | -2 | Invalid atom structure or missing track; no `moov` for an in-place edit |
| `FAAM_ERR_IO_READ` | -3 | Failed read or insufficient data for the requested operation |
| `FAAM_ERR_IO_WRITE` | -4 | Failed write, muxer seek or flush |
| `FAAM_ERR_INSUFFICIENT_MEM` | -5 | Arena too small or allocation failure |
| `FAAM_ERR_NO_TRACK` | -6 | No matching track |
| `FAAM_ERR_UNSUPPORTED` | -7 | Missing required callback, unsupported content/call or in-place 64-bit box header |
| `FAAM_ERR_OUTPUT_TOO_SMALL` | -8 | Output capacity too small; length out-parameter reports required capacity |
| `FAAM_ERR_NOT_BUILT` | -10 | Feature compiled out; query `faam_library_info.features` |

Init/open set the output handle to NULL before failing. Close takes its
address and clears it; check its status too. Muxer I/O/allocation failures
are sticky, including at finalize/close. Argument errors are not sticky.

### Function reference

The declarations below are from `faam.h`; output structs require `struct_size`.

```c
faam_status faam_get_library_info(faam_library_info *out);
faam_status faam_demuxer_open(const faam_demuxer_config *cfg, const faam_io *io,
                                      faam_demuxer **out_demuxer);
faam_status faam_demuxer_close(faam_demuxer **d);
faam_status faam_demuxer_get_num_tracks(faam_demuxer *d, uint32_t *out_held, uint32_t *out_total);
faam_status faam_demuxer_get_track_info(faam_demuxer *d, uint32_t track_index,
                                                faam_track_info *out_info);
faam_status faam_demuxer_get_codec_data(faam_demuxer *d, uint32_t track_id,
                                                uint8_t *out_buf, uint32_t buf_cap,
                                                uint32_t *out_len);
faam_status faam_demuxer_get_major_brand(faam_demuxer *d, char out_brand[5]);
faam_status faam_demuxer_get_track_gapless(faam_demuxer *d, uint32_t track_id, faam_gapless_info *out_gapless);
faam_status faam_demuxer_get_metadata(faam_demuxer *d, faam_metadata *out_meta);
faam_status faam_demuxer_get_custom_tag(faam_demuxer *d, uint32_t index, faam_custom_tag *out_tag);
faam_status faam_demuxer_get_num_chapters(faam_demuxer *d, uint32_t *out_count);
faam_status faam_demuxer_get_chapter(faam_demuxer *d, uint32_t index, faam_chapter *out_chapter);
faam_status faam_demuxer_next_frame_loc(faam_demuxer *d, faam_frame_loc *out_loc);
faam_status faam_demuxer_read_frame(faam_demuxer *d,
                                            uint8_t *out_frame, uint32_t frame_cap,
                                            uint32_t *frame_bytes);
faam_status faam_muxer_config_init(faam_muxer_config *cfg, uint32_t caller_size);
faam_status faam_muxer_get_track_id(const faam_muxer *m, uint32_t track_index, uint32_t *out_track_id);
faam_status faam_muxer_update(faam_muxer *m, const faam_muxer_update_params *params);
faam_status faam_muxer_write_frame(faam_muxer *m,
                                           uint32_t track_id,
                                           const uint8_t *frame_buf, uint32_t frame_bytes,
                                           uint32_t duration_ticks,
                                           int32_t cts_offset,
                                           uint32_t frame_flags);
faam_status faam_muxer_finalize(faam_muxer *m);
faam_status faam_muxer_open(const faam_muxer_config *cfg, const faam_io *io,
                                    faam_muxer **out_muxer);
faam_status faam_muxer_close(faam_muxer **m);
faam_status faam_muxer_get_info(const faam_muxer *m, uint32_t track_id, faam_muxer_info *out_info);
const char *faam_strerror(faam_status status);
faam_status faam_update_tags_stream(const faam_io *io, const faam_metadata *meta, uint32_t flags);
faam_status faam_update_chapters_stream(const faam_io *io, const faam_chapter *chapters, uint32_t count, uint32_t flags);
```

### Demuxer lifecycle

1. Pass a `faam_demuxer_config` with `struct_size` set and `flags` zero, or
   NULL for defaults, to open. Opening retains `moov` and
   indexes progressive samples; no `moov` means zero tracks.
2. Get held/total counts, then query track info by index. Other track calls
   use the returned `track_id`. Codec data is copied to your buffer; NULL
   with capacity zero queries its length. An undersized buffer reports
   `FAAM_ERR_OUTPUT_TOO_SMALL` and the required length. Major brand is a
   five-byte NUL-terminated string, padding spaces retained, empty without `ftyp`.
3. Initialize `faam_frame_loc.struct_size` and call next_frame_loc to inspect
   the next sample. It does not advance. read_frame reads and advances;
   a NULL payload buffer skips it. An undersized buffer reports its required
   size without advancing. Stop on `FAAM_END_OF_STREAM`, and report negative statuses.
4. Close the handle; it does not close your stream.

Track info includes timescale, sample rate, channels, total frames/duration,
maximum frame size, language, big-endian sample-entry `fourcc`, flags,
width/height and clockwise rotation (0, 90, 180, 270; other matrices read 0).
`FAAM_TRACK_OTHER` identifies other handlers, including text/timecode/metadata.
`FAAM_TRACK_INFO_INBAND_PARAMS` identifies avc3/hev1 entries.
Progressive duration is in track ticks. Fragmented total_frames is zero,
duration comes from `mehd` (zero before finalization), and maximum frame size
is the largest seen so far, zero before the first fragment is loaded.

### Muxer lifecycle

1. Call `faam_muxer_config_init(&cfg, sizeof(cfg))`. Set `tracks` to an array
   of `faam_track_config` and `num_tracks` to its count, from 1 to max_tracks.
   Set every element's struct_size to the same size. Audio uses
   `FAAM_TRACK_AUDIO`/`FAAM_CODEC_AAC`; video uses
   `FAAM_TRACK_VIDEO`/`FAAM_CODEC_H264` or `FAAM_CODEC_H265`.
2. Set optional creation_time, fragment_ms, metadata, chapters/count and
   gapless pointers. `flags` accepts `FAAM_MUXER_M4B` (progressive audio-only
   M4B branding) and `FAAM_MUXER_CONSTANT_RATE` (esds maxBitrate equals avgBitrate).
   Supply a populated config: the current muxer rejects NULL.
3. Open with `faam_muxer_open()`. Track ID zero auto-assigns;
   `faam_muxer_get_track_id()` retrieves IDs in config order. Explicit IDs
   must be unique. Progressive open writes ftyp/mdat; video and fragmented
   files use isom, otherwise M4A or M4B.
4. Write one encoded access unit per call using that ID, in decode order per
   track. duration_ticks is the DTS delta; cts_offset is PTS minus DTS.
   Frames from different tracks may interleave. frame_bytes must be non-zero.
   Use `FAAM_FRAME_KEYFRAME` in frame_flags for video sync samples; audio
   ignores that flag because every audio sample is a keyframe.
5. Finalize to write progressive moov/sample tables and patch mdat, or close
   the last fragment. Repeating a successful finalize succeeds; writing or
   updating afterwards is invalid. Query `faam_muxer_info` (struct_size set)
   for frame_count, duration_ticks, file_bytes, max/average bitrate and max_frame_size.
   Close on every path and check the returned sticky status.

For AAC, timescale/sample_rate are the audio rate and duration is normally
1024 samples. HE-AAC uses the core rate, still 1024 ticks per frame.
Audio track flags must be zero; GENERIC/OTHER tracks are not muxable.
Video needs dimensions and decoder configuration, except progressive Annex-B
H.264 may derive it. Codec data is at most `FAAM_CODEC_DATA_MAX` (1024 bytes).
Creation time is Unix seconds, zero unset; version-1 time boxes avoid wrapping
when the stored 1904-epoch time reaches 2040.

### Allocation and ownership

Open owns all instance allocations; close releases them. Muxer handles, tracks,
fragment indexes and oversized codec configurations are independent blocks.
Progressive sample tables and demuxer indexes/strings allocate as needed.
Fragmented muxers allocate their entire index at open and allocate nothing while muxing.
The demuxer uses views into its loaded `moov` buffer for temporary sample tables,
avoiding duplicate allocations while constructing the final sample index.

Source builds can override `AllocMemory` and `FreeMemory` together using `-D`
or a force-included header with function declarations. They default to
`malloc`/`free`. Return storage aligned as `malloc` does, and allow NULL
arguments to `FreeMemory`. Default builds use libc `realloc` for growing buffers;
custom-allocator builds allocate, copy initialized bytes and free the old block,
so growth preserves the custom allocator's memory placement. There are no
separate fast-memory or reallocation hooks.

Tracks and codec data are read/copied at open. Progressive metadata and
chapters are borrowed until finalize; metadata may be filled in before then.
Fragmented metadata/chapters are written at open. Strings, artwork and input
list contents must remain valid through finalize/update as specified by the
header. Demuxer output pointers live until close. Library information and
error strings are static. Custom-tag output is accessed by index, not an array pointer.

#### Running without a heap

A fixed-pool override can avoid the system heap. Force-include a header
declaring `pool_alloc(size_t)` and `pool_free(void *)` when compiling libfaam with
`-DAllocMemory=pool_alloc -DFreeMemory=pool_free`. Compile their definitions
into your application:

```c
#include <stddef.h>

/* Define POOL_BYTES for your build and container workload. */
static _Alignas(16) unsigned char pool[POOL_BYTES];
static size_t used;

void *pool_alloc(size_t n)
{
    if (n > sizeof(pool) - used)
        return NULL;
    n = (n + 15u) & ~(size_t)15u;
    if (n > sizeof(pool) - used)
        return NULL;
    void *p = pool + used;
    used += n;
    return p;
}

void pool_free(void *p) { (void)p; }
void pool_reset(void) { used = 0; }
```

Use one FAAM operation or instance at a time with this pool and serialize
access. Reset only after close or a failed open/standalone update, once no
pool-backed instance or borrowed output remains. Allocation failure returns
`FAAM_ERR_INSUFFICIENT_MEM`.

A bump allocator never reuses freed blocks. Budget for every allocation over
the whole operation, including temporary parsing buffers and both old and new
buffers during growth. Progressive tables grow with recording length;
fragmented demuxing allocates temporary buffers for each fragment. Use a
reclaiming pool for long or unbounded workloads. Fragmented muxing allocates
its state and complete index during open and allocates nothing while writing
or finalizing, making a bounded static pool practical. Input/output buffers,
stack and the stream callback's storage remain separate. See the matching
[FAAC example](libfaac.md#running-without-a-heap) and
[FAAD example](libfaad.md#running-without-a-heap).

If this pool is shared across libraries, reset it only after every pool-backed
handle is closed and no operation still uses its storage.

### Gapless playback, metadata and chapters

`faam_gapless_info` holds encoder_delay, end_padding and total_samples, in
samples of the audio track timescale (HE-AAC core rate). Supply the codec's
priming yourself. cfg.gapless supplies iTunSMPB and every audio edit list;
track.gapless overrides that track's edit list. The edit list requires a
non-zero delay. Progressive finalize derives an unknown total from coded
duration minus delay/padding, saturating at zero for an empty programme.
Fragmented gapless information is written at open; total_samples must be given
for its edit list. Query `faam_demuxer_get_track_gapless()` by ID, or zero for
the first audio track: iTunSMPB takes precedence on the first audio track,
otherwise each track uses its own edit list. Missing information gives zeros.

`faam_metadata` holds title/artist/album/album_artist/composer and their sort
forms, genre_str/year/comment/encoder, cover_art/cover_bytes, custom_tags/count,
genre_code, track/disc numbers and totals, compilation and cover_type.
Genre code is the ID3v1 number plus one, zero absent. Cover type is
`FAAM_COVER_JPEG`, `FAAM_COVER_PNG`, `FAAM_COVER_GIF` or `FAAM_COVER_BMP`,
written as iTunes data types 13, 14, 12 and 27 respectively.
`FAAM_COVER_AUTO` detects PNG, GIF (`GIF87a`/`GIF89a`) and JPEG (`FF D8 FF`)
by signature. BMP detection checks the file header, a supported DIB header
(12, 40, 52, 56, 108 or 124 bytes), dimensions, planes, bit depth and offset/size
bounds; the `BM` prefix alone is insufficient. This identifies the format,
without decoding or validating pixel data. Unrecognized AUTO artwork and unknown
cover type values return `FAAM_ERR_INVALID_ARG` when writing artwork. An explicit
supported cover type is honored even when the bytes do not match.
Demux returns the declared type for 12/13/14/27, even if the bytes differ;
other declared types use the same detection as AUTO. If detection fails, demux
returns `FAAM_COVER_AUTO` and preserves the artwork bytes without labeling them.
Custom tags have name/value/mean; NULL mean defaults to `com.apple.iTunes`.
Demux metadata returns custom_tags NULL and num_custom_tags; initialize a
`faam_custom_tag` and call `faam_demuxer_get_custom_tag()` for each index.

Chapters use start_ms and non-NULL UTF-8 title, stored up to 255 bytes.
Use `faam_demuxer_get_num_chapters()` and `faam_demuxer_get_chapter()` with an
initialized output struct. Nero chpl supports at most 255 chapters.

### Editing finished files

`faam_update_tags_stream(io, meta, flags)` and
`faam_update_chapters_stream(io, chapters, count, flags)` edit finished
progressive files in place. Chapter flags are reserved and must be zero;
count zero writes an empty chapter list. Missing udta/meta/ilst/chpl atoms
are created. Growing moov adjusts moov-first chunk offsets; shrinking leaves
free space. A 64-bit size header on moov or an edited box is unsupported and
leaves the file unchanged. No moov is `FAAM_ERR_BAD_CONTAINER`.
These edits are not transactional; interrupted writes can damage the file.

### Codec data and language

For late progressive configuration use `faam_muxer_update_params` with
struct_size and flags set, then call `faam_muxer_update()`:

| Flag | Fields applied |
|---|---|
| `FAAM_UPDATE_CREATION_TIME` | creation_time (Unix seconds) |
| `FAAM_UPDATE_GAPLESS` | gapless pointer |
| `FAAM_UPDATE_CODEC_DATA` | track_id, codec_data, codec_data_len (copied, at most FAAM_CODEC_DATA_MAX) |
| `FAAM_UPDATE_LANGUAGE` | track_id, language |
| `FAAM_UPDATE_AUDIO_SAMPLE_SIZE` | track_id, audio_sample_size (informational PCM size, AAC default 16) |

Unselected fields are ignored. Fragmented update returns
`FAAM_ERR_UNSUPPORTED`; update after finalize returns `FAAM_ERR_INVALID_ARG`.
Language uses the first three ISO 639-2/T letters, normalized lowercase;
short NUL-terminated input or any non-letter gives und.

### Video tracks

Set width, height, timescale (often 90000), rotation_degrees and codec_data
(avcC/hvcC payload). Non-zero cts_offset permits negative values for B-frames,
requires FAAM_FEATURE_VIDEO and is invalid for audio. Progressive files write
ctts only for non-zero offsets; the first offset is compensated by an edit list.
Set `FAAM_TRACK_INBAND_PARAMS` when samples repeat parameter sets, selecting
avc3/hev1; codec_data is still required for their configuration boxes.

`FAAM_TRACK_ANNEXB` accepts 00 00 01 or 00 00 00 01 start codes and writes
four-byte length-prefixed NAL units without modifying the caller buffer or
allocating. Progressive H.264 may omit codec_data: its first access unit must
carry SPS then PPS to derive avcC. Finalize fails if none was obtained.
H.265 always needs supplied hvcC. Dimensions are still required.

### Recording to removable storage

Set fragment_ms greater than zero and check FAAM_FEATURE_FRAGMENTED.
Open with `faam_muxer_open()`; it allocates the fragment index up front.
ftyp/moov are written at open, then moof/mdat fragments.
A fragment closes at the first video keyframe after the requested duration
(audio-only: accumulated audio duration), or earlier at sample-index capacity
(about 128 samples per second of fragment_ms, at least 64) or 1 GiB.
Supply flush to persist every completed fragment; seek/tell are still required.

A crash loses at most the open fragment. The demuxer ignores a fragment with
truncated data and exposes completed fragments. Finalize closes the last one
and writes mehd duration. Closing without finalize preserves completed
fragments; progressive files closed without finalize have no moov and cannot
be recovered. Fragmented video requires codec_data at open. Metadata,
chapters and gapless are fixed at open; update is unsupported. B-frame offsets
are stored in trun without shifting the first presentation time to zero.

### Reading and editing tags without remuxing

Open a demuxer, read metadata and retrieve custom tags by index into a
caller-owned array. Change the desired fields and pass the full metadata to
`faam_update_tags_stream()` while the demuxer remains open for borrowed data.
Close it after the update; reopen before reading the modified file.

With flags zero, modeled fields are replaced from the supplied metadata;
omitted modeled fields are cleared. Unmodeled atoms, extra cover images and
iTunSMPB are preserved. Custom tags must be carried through read-modify-write,
including their mean namespaces. `FAAM_TAG_UPDATE_CLEAR` drops all existing
ilst atoms, including unmodeled atoms and iTunSMPB, before writing the supplied
metadata. Chapters are kept; chapter updates keep tags.

### Files with many tracks

`faam_demuxer_get_num_tracks()` returns held and optionally total counts.
Only indices below held are valid for get_track_info; total can exceed
`faam_library_info.max_tracks`. Store each returned track_id and use that ID
for codec data and gapless queries. Do not confuse track IDs with indices.
Generic sample entries retain fourcc and readable payloads; other handlers
use FAAM_TRACK_OTHER. next_frame_loc interleaves all held tracks in file order;
filter its track_id and read or skip every frame to advance.

### Reading MP4 for the FAAD decoder

Applications using the legacy `faad.h`/mp4ff sequence can replace it with
`faam.h` and [libfaad's current API](libfaad.md). libfaam parses the container;
libfaad decodes its AAC access units.

| Legacy step | Current calls |
|---|---|
| `mp4ff_open_read` | `faam_demuxer_open(NULL, &io, &demuxer)`, with memory or stdio `faam_io` callbacks. |
| Find the audio track | `faam_demuxer_get_num_tracks()` and `faam_demuxer_get_track_info()`; choose an audio track with `codec_id == FAAM_CODEC_AAC`. Match `track_id` for an explicit selection. |
| `mp4ff_get_decoder_config` | `faam_demuxer_get_codec_data()` copies the track's AudioSpecificConfig (ASC) into your buffer. |
| `NeAACDecOpen` + `NeAACDecInit2` | Initialize `faad_config`, set `stream_format = FAAD_STREAM_RAW`, then call `faad_decoder_open(&cfg, asc, asc_len, &decoder)`. |
| `mp4ff_read_sample` + decode loop | `faam_demuxer_next_frame_loc()` then `faam_demuxer_read_frame()` and `faad_decode_frame()`. Filter `loc.track_id`; skip other tracks with `read_frame(d, NULL, 0, &bytes)`. |
| mp4ff gapless/metadata | `faam_demuxer_get_track_gapless()`, `faam_demuxer_get_metadata()` and `faam_demuxer_get_custom_tag()`. |

A file may hold several tracks; track IDs are not enumeration indices.
The CLI's `faad --track <id>` selects one (`faad -i` lists them). AAC codec
identification alone does not guarantee a profile supported by libfaad.
`frontend/mp4read.c` shows a memory-backed adapter: it skips each frame to
advance the demuxer, validates `file_offset`/`frame_bytes` against the input
size, then feeds the original bytes to libfaad without a payload copy.
With a file stream, you can likewise read at `file_offset` yourself, but must
still advance the demuxer with `read_frame`.

Gapless values are track-timescale samples, normally the AAC core rate for
HE-AAC, not output-rate samples. Query by the selected track ID; iTunSMPB
applies to the first audio track, otherwise that track's edit list is used.
As in `frontend/faad_main.c`, after output format discovery convert delay and
padding with `(uint64_t)value * output_rate / track_timescale`. If timescale
is zero, the frontend uses the core rate (half the output rate for SBR with
2048-sample frames, otherwise the output rate). Add `decoder_delay` to the
leading trim and subtract it from converted padding, bounded at zero.
Drop leading samples per channel and retain enough trailing PCM to discard
padding at EOF; re-query stream info on `FAAD_FRAME_FORMAT_CHANGED`. See
[Decoder delay](libfaad.md#decoder-delay) for edit-list boundaries.

For tags, initialize `faam_metadata.struct_size`; retrieve custom tags by
index up to `num_custom_tags`. For chapters, use
`faam_demuxer_get_num_chapters()` and `faam_demuxer_get_chapter()` (Nero `chpl`,
with start times in milliseconds). Initialize every output struct's
`struct_size`. Metadata strings, artwork and chapter titles are borrowed
until demuxer close; copy them if needed afterwards. ASC is copied into your
buffer and consumed by decoder open, so it need not outlive that call.
Keep the stream/callback context alive through demuxer close, which does not
close the FILE. Encoded and aligned PCM buffers belong to the caller;
close both handles and release the buffers on every exit path.

Save this as `m4a_decode.c`, alongside `stdio_io.h` from
[Stream I/O](#stream-io), which supplies 64-bit seeking. It decodes the first
AAC track to native-endian interleaved signed 16-bit PCM, with no header or
gapless trimming. It stops on demux/decode errors and accepts recovered PCM.

```c
/* m4a_decode.c */
#include "stdio_io.h"
#include <faad.h>
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

For an installed build, compile with `pkg-config --cflags --libs faam faad`.
For source-tree static archives built in `b/`:

```sh
cc -std=c11 -Wall -Wextra -DFAAM_STATIC -DFAAD_STATIC -Iinclude -Icommon \
  m4a_decode.c b/libfaam/libfaam.a b/libfaad/libfaad.a -lm -o m4a_decode
```

Run `./m4a_decode input.m4a output.pcm`. On a little-endian host,
compare with `faad --no-gapless -f raw -o reference.pcm input.m4a`.
The CLI writes little-endian PCM; byte-swap the example's output before
comparison on a big-endian host. Omitting `--no-gapless` trims container
priming/padding and decoder delay, so its output can differ.

### Thread safety

Serialize access to one handle. Separate handles are independent; callbacks
run on the calling thread. The library has no global mutable state.

### Complete examples

Each example uses `stdio_io.h` from [Stream I/O](#stream-io). They print their
errors to stderr and return non-zero on failure.

`inspect.c` opens a file with allocator-owned state, prints tracks, tags and
chapters, and counts frames per track:

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
        free(frames); free(ids); faam_demuxer_close(&d); free(mem); fclose(f);
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

    faam_metadata meta = { .struct_size = sizeof(meta) };
    if (st == FAAM_OK) st = faam_demuxer_get_metadata(d, &meta);
    if (st == FAAM_OK && meta.title)
        printf("title: %s\n", meta.title);

    for (uint32_t i = 0; st == FAAM_OK && i < meta.num_custom_tags; i++) {
        faam_custom_tag tag = { .struct_size = sizeof(tag) };
        st = faam_demuxer_get_custom_tag(d, i, &tag);
        if (st == FAAM_OK)
            printf("tag %s: %s (%s)\n", tag.name, tag.value,
                   tag.mean ? tag.mean : "com.apple.iTunes");
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

`mux_adts.c` writes an ADTS AAC file to an M4A with gapless information, tags
and chapters. The delay comes from your encoder (1024 for libfaac AAC-LC; for
HE-AAC the core-rate value, 1040 from libfaac, see
[Gapless playback](#gapless-playback-metadata-and-chapters)); the program
leaves `end_padding` at 0 and so does not trim the end. The loop assumes one
raw data block of 1024 samples per ADTS frame and a constant configuration.
It reconstructs only the ADTS-signaled ASC; extensions such as explicit SBR
need encoder-supplied codec data for full signaling:

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

    faam_metadata meta = { .struct_size = sizeof(meta), .title = "Title", .artist = "Artist", .track_num = 1, .track_total = 10 };
    faam_custom_tag custom_tag = { .struct_size = sizeof(custom_tag),
        .name = "Source", .value = "ADTS", .mean = "org.example" };
    meta.custom_tags = &custom_tag;
    meta.num_custom_tags = 1;
    faam_chapter chapters[] = {
        { .struct_size = sizeof(faam_chapter), .start_ms = 0, .title = "Intro" },
        { .struct_size = sizeof(faam_chapter), .start_ms = 30000, .title = "Main part" }
    };

    faam_muxer_config cfg;
    faam_muxer_config_init(&cfg, sizeof(cfg));
    faam_gapless_info gapless = { .struct_size = sizeof(gapless), .encoder_delay = 1024 };
    cfg.gapless = &gapless;
    cfg.metadata = &meta;
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
            st = FAAM_ERR_INVALID_ARG; break;
        }
        st = faam_muxer_write_frame(m, id, adts + pos + hdr, flen - hdr, 1024, 0, FAAM_FRAME_KEYFRAME);
        pos += flen;
    }
    if (st == FAAM_OK && pos != len) st = FAAM_ERR_INVALID_ARG;
    if (st == FAAM_OK) st = faam_muxer_finalize(m);
    if (st != FAAM_OK) fprintf(stderr, "mux: %s\n", faam_strerror(st));

    faam_status closed = faam_muxer_close(&m);
    if (st == FAAM_OK) st = closed;
    fclose(out);
    free(adts);
    return st == FAAM_OK ? 0 : 1;
}
```

`record.c` writes a fragmented, crash-safe file with a preallocated fragment index. It
needs the `muxer-fragmented` option. Its synthetic payload tests container
writing only; replace it with encoded AAC-LC stereo 44.1 kHz access units
for playback:

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

`tag.c` changes one tag of an existing file and keeps the others, as
`faam tag` does. The demuxer stays open until the update returns because the
existing strings are borrowed from it:

```c
/* tag.c */
#include "stdio_io.h"
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "Usage: %s file.m4a title\n", argv[0]); return 1; }
    FILE *f = fopen(argv[1], "r+b");
    if (!f) { perror(argv[1]); return 1; }
    faam_io io = stdio_io(f);

    faam_demuxer *d = NULL;
    faam_metadata meta = { .struct_size = sizeof(meta) };
    faam_status st = faam_demuxer_open(NULL, &io, &d);
    if (st == FAAM_OK) st = faam_demuxer_get_metadata(d, &meta);
    faam_custom_tag *tags = NULL;
    if (st == FAAM_OK && meta.num_custom_tags) {
        tags = calloc(meta.num_custom_tags, sizeof(*tags));
        if (!tags) st = FAAM_ERR_INSUFFICIENT_MEM;
        for (uint32_t i = 0; st == FAAM_OK && i < meta.num_custom_tags; i++) {
            tags[i].struct_size = sizeof(*tags);
            st = faam_demuxer_get_custom_tag(d, i, &tags[i]);
        }
        meta.custom_tags = tags;
    }
    if (st == FAAM_OK) {
        meta.title = argv[2];
        st = faam_update_tags_stream(&io, &meta, 0);
    }
    free(tags);
    faam_demuxer_close(&d);
    fclose(f);
    if (st != FAAM_OK) fprintf(stderr, "%s: %s\n", argv[1], faam_strerror(st));
    return st == FAAM_OK ? 0 : 1;
}
```

## ABI compatibility

Every caller-filled or received struct starts with uint32_t struct_size. Set
it to sizeof of your struct. The corresponding `FAAM_*_BASELINE` freezes its
minimum accepted size: larger structs are accepted, input reads only covered
fields, and output writes at most struct_size bytes. Unknown fields take
zero values. Zero your storage and reset struct_size before each query.

Fields are append-only after the baseline; explicit padding is never reused.
Released enum values, signatures and field offsets stay fixed within a major
version. The design excludes embedded structs and embedded list arrays:
tracks, chapters and custom tags use pointers/counts. The header still has
fixed language and reserved byte arrays; these are not extensible lists.
Input arrays have one uniform element size, strided by the first element's
struct_size. Output lists use index getters.

Open calls take config pointers so future knobs can be appended;
NULL means defaults for the demuxer. The design summary allows NULL defaults
generally, but the current muxer requires a config with at least one track.
Compile-time options never change public layouts; query library features.
Layouts follow platform pointer width/alignment and are not serialized formats.

`FAAM_VERSION_MAJOR`, `FAAM_VERSION_MINOR`, `FAAM_VERSION_PATCH`,
`FAAM_VERSION_STRING` and `FAAM_VERSION_HEX` describe header version 3.0.0.
Major 3 corresponds to SONAME 3; faam_get_library_info reports the linked version.
Deliberately outside the ABI are opaque handle contents, allocation/state
sizes, sample-index limits and the internal track cap (currently 8).
Query max_tracks instead of compiling against that cap. No internal track
array or compile-time track-cap constant is part of the public interface.

FAAM is free software, licensed under the GNU Lesser General Public License
(LGPL), version 2.1 or later.

Copyright © 2026, Nils Schimmelmann
