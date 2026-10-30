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
  - [Moving the index to the front](#moving-the-index-to-the-front)
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
archive-selection options.

Building without Meson: compile `libfaam/demux.c`, `mux.c`, `metadata.c`,
`faam_util.c`, `tag.c`, `chapter.c` and `atom_patch.c` with `include/` and
`common/` on the include path. Define `FAAM_BUILDING` when building a Windows DLL; nothing
else needs a define. Define
`FAAM_MUXER_VIDEO=1` and `FAAM_MUXER_FRAGMENTED=1` to include those features;
leave the macros undefined to omit them. Install `include/faam.h` with the library.

## Supported content

Query `faam_get_library_info()` with `faam_library_info.struct_size` set.
`flags` contains `FAAM_LIB_*` bits; `max_tracks` is the number one
muxer can write or one demuxer can hold. `version` and `copyright` are static strings.

| Optional feature | Option (default) | Runtime detection | When absent |
|---|---|---|---|
| H.264/H.265, including Annex-B input and avc3/hev1 | `muxer-video` (true) | `FAAM_LIB_VIDEO` | Video muxing and non-zero frame CTS offsets return `FAAM_ERR_NOT_BUILT`; video sample entries demux as `FAAM_CODEC_GENERIC`, without video sync/CTS parsing. |
| Fragmented MP4 | `muxer-fragmented` (true) | `FAAM_LIB_FRAGMENTED` | Fragmented muxing/demuxing returns `FAAM_ERR_NOT_BUILT`. |

## API (faam.h)

Every fallible call returns `faam_status`; results use out-parameters. Set
`struct_size` on every input and output struct, including nested pointer targets.
The `flags` argument of `faam_update_chapters_stream()` is reserved and must be 0.

### Stream I/O

`faam_io` starts with `struct_size`, followed by `user_data` and the `read`,
`write`, `seek`, `tell` and optional `flush` callbacks. The library copies it;
keep `user_data` alive until close. Seek offsets are absolute, in bytes.

| Operation | Required callbacks |
|---|---|
| Demuxer open | `read`, `seek` |
| Muxer open, both modes | `write`, `seek`, `tell` |
| Muxer with `FAAM_MUXER_FASTSTART` | `read`, `write`, `seek`, `tell` |
| Tag/chapter updates | `read`, `write`, `seek`, `tell` |

Missing required callbacks return `FAAM_ERR_UNSUPPORTED` before any I/O.
`read` may return a short count: the library retries until satisfied. Zero
means end of data; a negative count or one above the request is
`FAAM_ERR_IO_READ`. During demuxer open, zero inside a structure is treated as
truncation: the file opens with whatever was parsed, possibly no tracks.
`write` must return exactly the requested count. Any other count, or a failed
muxer seek/flush, is `FAAM_ERR_IO_WRITE`. Muxer I/O failures are sticky.
The demuxer does not use write/tell; the muxer uses read only for `FAAM_MUXER_FASTSTART`.
A single read or write request never exceeds `INT32_MAX` bytes, the largest count the
callbacks can return; `faam_muxer_write_frame()` rejects a larger `frame_bytes` with
`FAAM_ERR_INVALID_ARGUMENT`.
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
| `FAAM_ERR_INVALID_ARGUMENT` | -1 | NULL required pointer, invalid struct size, configuration, flags or call order |
| `FAAM_ERR_UNSUPPORTED` | -2 | Missing required callback, unsupported content/call or in-place 64-bit box header |
| `FAAM_ERR_NO_MEMORY` | -3 | An allocation failed |
| `FAAM_ERR_OUTPUT_TOO_SMALL` | -4 | Output capacity too small; length out-parameter reports required capacity |
| `FAAM_ERR_IO_READ` | -5 | Failed read or insufficient data for the requested operation |
| `FAAM_ERR_IO_WRITE` | -6 | Failed write, muxer seek or flush |
| `FAAM_ERR_BAD_CONTAINER` | -7 | Invalid atom structure or missing track; no `moov` for an in-place edit |
| `FAAM_ERR_NO_TRACK` | -8 | No matching track |
| `FAAM_ERR_NOT_BUILT` | -9 | Feature compiled out; query `faam_library_info.flags` |
| `FAAM_END_OF_STREAM` | 1 | No more frames; not an error |

Init/open set the output handle to NULL before failing. Close takes its
address and clears it; check its status too. Muxer I/O/allocation failures
are sticky, including at finalize/close. Argument errors are not sticky.

### Function reference

The declarations below are from `faam.h`; output structs require `struct_size`.

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

1. Pass a `faam_demuxer_config` from `faam_demuxer_config_init()` (`flags` stays zero), or
   NULL for defaults, to open. Opening retains `moov` and
   indexes progressive samples; no `moov` means zero tracks.
2. Get held/total counts, then query track info by index. Other track calls
   use the returned `track_id`. Codec data is copied to your buffer; NULL
   with capacity zero queries its length. An undersized buffer reports
   `FAAM_ERR_OUTPUT_TOO_SMALL` and the required length. Major brand is a
   five-byte NUL-terminated string, padding spaces retained, empty without `ftyp`.
3. Initialize `faam_frame_loc.struct_size` and call next_frame_loc to inspect
   the next sample. It does not advance. `flags` carries `FAAM_FRAME_KEYFRAME`
   for a sync sample; audio samples are all keyframes. The `faam_demuxer_get_*`
   queries take a const handle and do not change the cursor. read_frame reads and advances;
   a NULL payload buffer skips it. An undersized buffer reports its required
   size without advancing. Stop on `FAAM_END_OF_STREAM`, and report negative statuses.
4. Query tags with `faam_demuxer_get_num_metadata()` and the indexed entry getter.
5. Close the handle; it does not close your stream.

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
2. Set optional creation_time, fragment_ms, metadata/num_metadata, chapters/count and
   gapless pointers. `flags` accepts `FAAM_MUXER_M4B` (progressive audio-only
   M4B branding), `FAAM_MUXER_CONSTANT_RATE` (esds maxBitrate equals avgBitrate)
   and `FAAM_MUXER_FASTSTART` (see [Moving the index to the front](#moving-the-index-to-the-front)).
   Any other bit is `FAAM_ERR_INVALID_ARGUMENT`.
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
chapters are borrowed until finalize. Supply metadata entries and num_metadata at
open, or bind them later with `FAAM_UPDATE_METADATA` for progressive files.
Fragmented metadata/chapters must be supplied in the config and are written at open. Strings, artwork and input
list contents must remain valid through finalize/update as specified by the
header. Demuxer output pointers live until close. Library information and
error strings are static. Metadata output is accessed by index, not an array pointer.

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
`FAAM_ERR_NO_MEMORY`.

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

Metadata is a list of entries supplied as a pointer and count.
Every value uses `faam_metadata_entry`, with a four-byte `key`, 24-bit
`data_type`, 32-bit `locale`, byte pointer/length (`data`/`data_bytes`),
`flags`, and optional freeform `mean`/`name`. The library does not need a
field-specific implementation for new metadata. Set all struct sizes and
zero other fields before filling them.

Use `FAAM_FOURCC(0xa9, 'l', 'y', 'r')` for lyrics: the copyright byte in
an atom key is one byte, **not** a UTF-8 character. A UTF-8 payload uses
`FAAM_METADATA_UTF8`; its length excludes the terminating NUL. Line breaks
are preserved. Integer payloads use `FAAM_METADATA_INTEGER` with big-endian
bytes of the appropriate width. Unknown 24-bit types, arbitrary binary data,
and locale values pass through without interpretation or content validation.

Library representations include:

- `©nam`, `©ART`, `©alb`, sort names, lyrics and descriptions -> UTF-8.
- `tmpo`, `©mvi`, `©mvc` -> big-endian integer, 2 bytes.
- `shwm`, `cpil` -> integer, 1 byte.
- `trkn`, `disk` -> implicit bytes: reserved u16, number u16, total u16, reserved u16, all big-endian.
- `covr` -> raw image bytes, JPEG 13, PNG 14, GIF 12 or BMP 27.

Podcast/TV tags such as `stik`, `pcst`, and `tvsh`, and future native keys,
use the same generic API even when the CLI has no named option for them.

Sort names, year, comment, and encoder are ordinary UTF-8 entries too.
Multiple cover images and multiple values of any key are
represented by repeated entries. Both repeated atoms and multiple data children
inside one atom are enumerated, in file order.

A freeform entry has key `FAAM_FOURCC('-', '-', '-', '-')` and a nonempty
`name`; NULL or empty `mean` defaults to `com.apple.iTunes`. Native entries
must have NULL `mean` and `name`. MusicBrainz IDs, ISRC, ReplayGain, and other
freeform tags need no additional library API. For example, use the conventional
`com.apple.iTunes` namespace and the appropriate tag name for each tool.

The demuxer returns the count through `faam_demuxer_get_num_metadata()`; retrieve values by
index with `faam_demuxer_get_metadata_entry()`. Payloads, names and namespaces
remain valid until close. Payload lengths are authoritative; binary data and
text containing NUL bytes must not be treated as C strings. Opaque atoms
without a usable data child are not enumerated, but survive unrelated edits.
Gapless playback information remains available through the separate gapless
API because it also controls track timing and edit lists.

Chapters use start_ms and non-NULL UTF-8 title, stored up to 255 bytes.
Use `faam_demuxer_get_num_chapters()` and `faam_demuxer_get_chapter()` with an
initialized output struct. Nero chpl supports at most 255 chapters; QuickTime
chapter tracks are not interpreted.

### Editing finished files

`faam_update_metadata_stream(io, entries, count, flags)` and
`faam_update_chapters_stream(io, chapters, count, flags)` edit finished
progressive files in place. Chapter flags are reserved and must be zero;
count zero writes an empty chapter list. Missing udta/meta/ilst/chpl atoms
are created. Growing moov adjusts moov-first chunk offsets; shrinking leaves
free space. A 64-bit size header on moov or an edited box is unsupported and
leaves the file unchanged. No moov is `FAAM_ERR_BAD_CONTAINER`.
A fragmented recording, finalized or cut short by a crash, can be tagged too: its
completed fragments stay playable.
These edits are not transactional; interrupted writes can damage the file.

### Codec data and language

Set informational PCM `audio_sample_size` in `faam_track_config` before open;
zero selects the default 16 bits. It does not change encoded audio.


For late progressive configuration use `faam_muxer_update_params` with
struct_size and flags set, then call `faam_muxer_update()`:

| Flag | Fields applied |
|---|---|
| `FAAM_UPDATE_CREATION_TIME` | creation_time (Unix seconds) |
| `FAAM_UPDATE_GAPLESS` | gapless pointer |
| `FAAM_UPDATE_CODEC_DATA` | track_id, codec_data, codec_data_len (copied, at most FAAM_CODEC_DATA_MAX) |
| `FAAM_UPDATE_LANGUAGE` | track_id, language |
| `FAAM_UPDATE_METADATA` | metadata, num_metadata (borrowed until finalize) |

Unselected fields are ignored. Fragmented update returns
`FAAM_ERR_UNSUPPORTED`; update after finalize returns `FAAM_ERR_INVALID_ARGUMENT`.
Language uses the first three ISO 639-2/T letters, normalized lowercase;
short NUL-terminated input or any non-letter gives und.

Any AAC object type is carried as opaque `codec_data`. xHE-AAC/USAC passes
through the same way, untested end to end, and the channel count is not derived
from the USAC configuration.

### Video tracks

Set width, height, timescale (often 90000), rotation_degrees and codec_data
(avcC/hvcC payload). Non-zero cts_offset permits negative values for B-frames,
requires FAAM_LIB_VIDEO and is invalid for audio. Progressive files write
ctts only for non-zero offsets; the first offset is compensated by an edit list.
Set `FAAM_TRACK_INBAND_PARAMS` when samples repeat parameter sets, selecting
avc3/hev1; codec_data is still required for their configuration boxes.

`FAAM_TRACK_ANNEXB` accepts 00 00 01 or 00 00 00 01 start codes and writes
four-byte length-prefixed NAL units without modifying the caller buffer or
allocating. Progressive H.264 may omit codec_data: its first access unit must
carry SPS then PPS to derive avcC. Finalize fails if none was obtained.
H.265 always needs supplied hvcC. Dimensions are still required.

### Recording to removable storage

Set fragment_ms greater than zero and check FAAM_LIB_FRAGMENTED.
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

### Moving the index to the front

A progressive file ends with its moov, so a player fetching it over HTTP needs the
whole file or a range request before it can start. Set `FAAM_MUXER_FASTSTART` for
a file served that way: finalize writes moov at the end as usual, rebuilds it with
the shifted chunk offsets (stco entries that no longer fit 32 bits become co64),
and copies the sample data forward to make room in front of it. The io must
provide `read` (`FAAM_ERR_UNSUPPORTED` otherwise), and the flag is invalid with
`fragment_ms`.

The copy rewrites the whole file, which costs a second pass of I/O and flash
wear, and the file is not a valid MP4 until it completes: a power cut during
finalize loses the recording. A read that fails during the copy is
`FAAM_ERR_IO_READ`, and like a write failure it is sticky. Recorders writing to SD should leave it off and use
fragmented files instead. The default writes moov last. Tag and chapter updates
work on either layout.

### Reading and editing tags without remuxing

Read the count with `faam_demuxer_get_num_metadata()` and retrieve values with the indexed getter.
To edit a file, call `faam_update_metadata_stream()` with just the entries to
change; opening a demuxer and copying existing tags is unnecessary. Close any
existing demuxer and reopen before reading the modified file.
Only iTunes `ilst` metadata is handled; in-place editing rejects `mdta`.

With flags zero, each supplied key replaces **all** existing values of that
key. Identity is the native four-byte key, or key plus namespace/name for
freeform tags; locale and data type do not change identity. Repeated input
keys supply multiple replacement values. Set `FAAM_METADATA_REMOVE` with
NULL data and zero length to remove a key. Do not mix removals and values for
the same key. An empty payload without REMOVE writes an explicit empty value.
Untouched atoms, including unknown atoms, artwork and iTunSMPB, are preserved
byte-for-byte in their original relative order.

`FAAM_METADATA_UPDATE_CLEAR` drops all existing ilst atoms before applying
the entries. Chapters remain intact; chapter updates keep metadata. Invalid
arguments and malformed ilst child lengths fail before metadata is rewritten.

### Files with many tracks

`faam_demuxer_get_num_tracks()` returns held and optionally total counts.
Only indices below held are valid for get_track_info; total can exceed
`faam_library_info.max_tracks`. Store each returned track_id and use that ID
for codec data and gapless queries. Do not confuse track IDs with indices.
Generic sample entries retain fourcc and readable payloads; other handlers
use FAAM_TRACK_OTHER. An `mp4v` (MPEG-4 Part 2) video entry is a generic one
that also reports width and height, and `faam_demuxer_get_codec_data()` returns
its esds DecoderSpecificInfo (the VOL header) for a decoder; the muxer does not
accept it. next_frame_loc interleaves all held tracks in file order;
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
| mp4ff gapless/metadata | `faam_demuxer_get_track_gapless()`, `faam_demuxer_get_num_metadata()` and `faam_demuxer_get_metadata_entry()`. |

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

For tags, query `faam_demuxer_get_num_metadata()`; initialize each entry
with its struct_size and retrieve entries by index up to the count. For chapters, use
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

For an installed build, compile with `pkg-config --cflags --libs faam faad3`.
For source-tree static archives built in `b/`:

```sh
cc -std=c11 -Wall -Wextra -Iinclude -Icommon \
  m4a_decode.c b/libfaam/libfaam.a b/libfaad/libfaad3.a -lm -o m4a_decode
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
`faam tag` does. Only the changed key is supplied:

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

The API and ABI are stable within a major version / SONAME on a given platform
ABI. Layouts follow the target's pointer width and alignment; they differ between
32- and 64-bit targets and are not a serialization format.

**Caller rules**

- Every struct the caller fills or receives starts with `uint32_t struct_size`.
  Initialize `faam_muxer_config`, `faam_demuxer_config`, `faam_track_config` and
  `faam_io` with `faam_muxer_config_init()`, `faam_demuxer_config_init()`,
  `faam_track_config_init()` and `faam_io_init()` (pass `sizeof` of the struct).
  `faam_chapter`, `faam_metadata_entry`, `faam_gapless_info` and
  `faam_muxer_update_params` have no init function: set `.struct_size = sizeof(x)`
  and zero the rest (`{ .struct_size = sizeof(x) }`). Set `.struct_size` on every
  output struct and on nothing else.
- Initialization stores `min(caller size, library size)` in `struct_size`. That
  write-back is the support probe: before using a field appended in a newer
  release, check that `struct_size` covers it. Structs without an init function
  (`faam_chapter`, `faam_metadata_entry`, `faam_gapless_info`) are also getter
  outputs, so a query reports the same probe. `faam_muxer_update_params` gates
  every field behind a `flags` bit and rejects unknown bits, so a library that
  predates a field refuses the update instead of ignoring it.
- Query functions write at most the caller's `struct_size` bytes and set
  `struct_size` to the bytes populated. Reset it before each query.
- The library reads only the input bytes `struct_size` covers; absent fields take
  their zero value. A `struct_size` below the matching `FAAM_*_BASELINE` returns
  `FAAM_ERR_INVALID_ARGUMENT`.
- Input lists (tracks, chapters, metadata entries) are a pointer and a count,
  strided by the first element's `struct_size`, so every element shares one size.
  Output lists use a count getter plus a getter by index. A getter reports the
  bytes populated in `struct_size`, which can be smaller than your `sizeof`, so
  set `struct_size = sizeof(element)` again on every element before passing
  queried chapters or metadata entries back as input; mixed sizes are rejected.
- `faam_demuxer_config.flags` is reserved and must be 0; the demuxer rejects
  anything else. A `struct_size` of 0 or below the baseline is detectable; a
  garbage larger value is not, so always initialize.
- Tolerate unknown enum values, flag bits and status codes in anything the
  library returns.
  `FAAM_OK` is 0 and `FAAM_ERR_INVALID_ARGUMENT`, `FAAM_ERR_UNSUPPORTED` and
  `FAAM_ERR_NO_MEMORY` are -1, -2 and -3; higher codes are specific to libfaam.
  `faam_update_chapters_stream()` takes a `flags` argument that
  must be 0.
- Compile-time options never change public layouts; query
  `faam_get_library_info()` for what a build supports, including `max_tracks`
  (do not compile against the internal track cap).

**Maintainer rules**

- New fields are appended at or after the previous `sizeof(struct)`, never in its
  tail padding. Place each appended member at an offset that is a multiple of its
  size, add an explicit `uint32_t` first if needed, and keep each appended block a
  multiple of 8 bytes. `faam_demuxer_config` has 4-byte alignment: append only
  members of 4 bytes or less.
- Zero in a new input field keeps the previous behaviour.
- Baseline sizes (`FAAM_*_BASELINE` macros), released offsets, enum values, status
  codes and signatures stay fixed within a major version.
- No struct embeds another or holds an array of structs; the fixed `language`
  byte arrays are not extensible lists. Open calls take config pointers so knobs
  can be appended; only the demuxer accepts a NULL config.
- `FAAM_VERSION_*` is the project release version, which every library reports; the
  SONAME is versioned separately. Opaque handle contents, allocation/state sizes and sample-index limits
  are outside the ABI.

FAAM is free software, licensed under the GNU Lesser General Public License
(LGPL), version 2.1 or later.

Copyright © 2026, Nils Schimmelmann
