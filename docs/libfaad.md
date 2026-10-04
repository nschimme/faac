# FAAD - AAC Decoder Library

## Contents

- [Scope](#scope)
- [Interface description](#interface-description)
- [Building and linking](#building-and-linking)
- [Supported streams](#supported-streams)
- [API (faad.h)](#api-faadh)
  - [Calling sequence](#calling-sequence)
  - [Error handling](#error-handling)
  - [Function reference](#function-reference)
  - [Complete ADTS decode example](#complete-adts-decode-example)
  - [Allocation and initialization](#allocation-and-initialization)
  - [PCM and format discovery](#pcm-and-format-discovery)
  - [Decoder delay](#decoder-delay)
  - [Packet errors and seeking](#packet-errors-and-seeking)
- [Porting from the legacy NeAACDec* API](#porting-from-the-legacy-neaacdec-api)
  - [Configuration field mapping](#configuration-field-mapping)
  - [Frame metadata mapping](#frame-metadata-mapping)
- [ABI compatibility](#abi-compatibility)

## Scope

FAAD decodes one raw AAC access unit or ADTS frame into caller-owned,
interleaved PCM. The application handles containers, transport buffering,
resampling, timestamps and audio-device playback. Both heap-backed and
caller-owned decoder state use the same decode interface.

## Interface description

Include `<faad.h>` and link against `libfaad`. Installed builds provide the
`faad` pkg-config module.

The shared library is `libfaad` (`libfaad.so.3` on Linux,
`libfaad.3.dylib` on macOS, or `libfaad.dll` on Windows). The default build
also produces a static archive. The `faad` command-line decoder in
`frontend/` is an example of using the API.

## Building and linking

From the source directory, build and install the decoder library with Meson:

```sh
meson setup build-faad -Dencoder=false -Dfrontend=false
meson compile -C build-faad
meson install -C build-faad
```

The default produces shared and static libraries. Select just one with
`-Ddefault_library=shared` or `-Ddefault_library=static` at setup time.
To build the `faad` command-line decoder too, omit `-Dfrontend=false`.

For an installed library, compile the example below with:

```sh
cc -std=c11 -Wall -Wextra adts_decode.c -o adts_decode $(pkg-config --cflags --libs faad)
```

For static linking, use `pkg-config --cflags --libs --static faad` and select
an installation containing only the static library, or use your toolchain's
static-library selection options. `--static` adds private dependencies; it
does not itself force the linker to select an archive.

For a custom installation prefix, add its pkg-config directory to
`PKG_CONFIG_PATH` and configure the platform's runtime library search path
for shared linking. Remove legacy FAAD2 include and library paths from the
application's build configuration.

## Supported streams

| Feature | Support and requirements |
|---|---|
| AAC-LC | Supported by every decoder build |
| HE-AAC v1 (SBR) | Enable `decoder-sbr` (default: true) |
| HE-AAC v2 (SBR + PS) | Enable `decoder-sbr` and `decoder-ps` (both default: true); requires at least two compiled channels |
| ADTS | Pass headers and payload; one raw data block per ADTS frame |
| RAW AAC | Supply valid ASC at open; pass one complete access unit per call |
| MP4/M4A | Demux outside the library; pass track ASC and RAW access units |
| Channel capacity | `max-channels` selects 1–8 compiled channels (default: 8); downmix does not enable decoding beyond that ceiling |
| PCM output | Interleaved signed 16-, packed 24-, 24-bit integer in 32 bits, or 32-bit float |
| ADIF, LATM, DRM and legacy AAC profiles | Unsupported; see [Configuration field mapping](#configuration-field-mapping) |
| 960-sample AAC frames | Unsupported; the AAC core uses 1024-sample frames |

Query `faad_get_library_info()` for the loaded library's channel ceiling and
SBR/PS capabilities. Header constants alone do not identify build options.
HE-AAC may signal extensions implicitly during decoding, so use frame flags
and stream info even when the initial snapshot describes AAC-LC.

## API (faad.h)

Supply configuration once, when opening the decoder. The PCM buffer capacity
is queryable immediately, even when the stream's sample rate and channel count
are not yet known. Every fallible call returns a `faad_status`; byte counts
and metadata are returned through out-parameters.

### Calling sequence

1. Initialize a `faad_config` with `faad_config_init(&cfg, sizeof(cfg))`,
   then set the fields you need. Defaults are ADTS, signed 16-bit PCM and no
   downmix.
2. Call `faad_decoder_open()`. For RAW input, select `FAAD_STREAM_RAW` and
   supply a valid, nonempty AudioSpecificConfig (ASC) from your demuxer.
   For caller-owned decoder state, use `faad_decoder_init()` instead; see
   [Allocation and initialization](#allocation-and-initialization).
3. Set a `faad_stream_info`'s `struct_size` to `sizeof(info)` and call
   `faad_decoder_get_info()`. Allocate aligned PCM storage of at least
   `info.max_output_bytes` and supply that capacity on every decode call.
4. Call `faad_decode_frame()` for each packet. RAW input must contain one
   complete access unit; ADTS input may contain several frames, but each call
   decodes at most one. Advance input by `bytes_consumed` and interpret the
   emitted PCM using `faad_stream_info`. On `FAAD_FRAME_FORMAT_CHANGED`,
   re-query `faad_decoder_get_info()` before using the PCM.
5. On a seek or discontinuity, call `faad_decoder_flush()` to discard audio
   history. Flush does not drain PCM. Reopen for a different stream configuration.
6. Call `faad_decoder_close(&dec)`. It sets the handle to NULL on success;
   caller-owned state and PCM remain yours to release.

### Error handling

`FAAD_OK` is 0; all error codes are negative. `faad_strerror()` maps a status
to a static, human-readable string and never returns NULL.

Check both status and input consumption. `FAAD_ERR_NEED_MORE_DATA` allows a
retry after appending ADTS bytes; `FAAD_ERR_SYNC_LOST` reports skipped bytes.
Complete rejected packets also report consumption and must be skipped.
Recovered PCM returns `FAAD_OK` with `FAAD_FRAME_CONCEALED` or
`FAAD_FRAME_DEGRADED` set. See [Packet errors and seeking](#packet-errors-and-seeking)
for consumption and retry rules.

### Function reference

```c
/* Library version, compiled channel ceiling and SBR/PS support.
 * Set out->struct_size before calling. */
faad_status faad_get_library_info(faad_library_info *out);

faad_status faad_config_init(faad_config *cfg, uint32_t caller_size);
faad_status faad_decoder_open(const faad_config *cfg,
                              const uint8_t *asc_buf, uint32_t asc_len,
                              faad_decoder **out_dec);
faad_status faad_decoder_close(faad_decoder **dec);  /* sets *dec = NULL */

/* Caller-owned state: query size, then initialize aligned storage. */
faad_status faad_get_state_size(uint32_t *state_bytes_out);
faad_status faad_decoder_init(void *mem_buf, uint32_t mem_size,
                              const faad_config *cfg,
                              const uint8_t *asc_buf, uint32_t asc_len,
                              faad_decoder **out_dec);

/* Current format and lifetime PCM capacity; set out_info->struct_size. */
faad_status faad_decoder_get_info(const faad_decoder *dec,
                                  faad_stream_info *out_info);
faad_status faad_decode_frame(faad_decoder *dec,
                              const uint8_t *in_buf, uint32_t in_bytes,
                              uint32_t *bytes_consumed,
                              void *out_pcm, uint32_t out_cap_bytes,
                              uint32_t *bytes_written,
                              uint32_t *frame_flags);
faad_status faad_decoder_flush(faad_decoder *dec);

const char *faad_strerror(faad_status status);
```

### Complete ADTS decode example

Save this as `adts_decode.c`. It reads an ADTS file and writes native-endian,
interleaved signed 16-bit PCM. Run `./adts_decode input.aac output.pcm`.
The PCM file has no header, timestamps or gapless trimming; the program prints
rate and channel changes to stderr. For playback, configure the audio sink
from stream info after each format-change flag before submitting PCM.

The loop retains partial input, skips consumed rejected packets, and accepts
concealed/degraded audio. It treats an incomplete final frame as an error.

```c
#include <faad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    FILE *input = NULL, *output = NULL;
    faad_decoder *dec = NULL;
    void *pcm = NULL;
    int result = 1;
    uint8_t pending[16384]; /* Larger than the 13-bit ADTS frame limit. */
    size_t used = 0;
    int need_input = 1, eof = 0;
    faad_config cfg;
    faad_stream_info info = { .struct_size = sizeof(info) };
    faad_status st;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s input.aac output.pcm\n", argv[0]);
        return 1;
    }
    input = fopen(argv[1], "rb");
    if (!input) { perror(argv[1]); goto done; }
    output = fopen(argv[2], "wb");
    if (!output) { perror(argv[2]); goto done; }
    st = faad_config_init(&cfg, sizeof(cfg));
    if (st == FAAD_OK)
        st = faad_decoder_open(&cfg, NULL, 0, &dec);
    if (st == FAAD_OK)
        st = faad_decoder_get_info(dec, &info);
    if (st != FAAD_OK) {
        fprintf(stderr, "%s\n", faad_strerror(st));
        goto done;
    }
    pcm = malloc(info.max_output_bytes); /* Aligned for 16-bit PCM. */
    if (!pcm) { fprintf(stderr, "PCM allocation failed\n"); goto done; }

    for (;;) {
        if (need_input && !eof) {
            if (used == sizeof(pending)) {
                fprintf(stderr, "Input buffer full without a complete frame\n");
                goto done;
            }
            used += fread(pending + used, 1, sizeof(pending) - used, input);
            if (ferror(input)) { perror("read"); goto done; }
            eof = feof(input);
            need_input = 0;
        }
        if (used == 0) {
            if (eof) break;
            need_input = 1;
            continue;
        }

        uint32_t consumed = 0, written = 0, flags = 0;
        st = faad_decode_frame(dec, pending, (uint32_t)used, &consumed,
                               pcm, info.max_output_bytes, &written, &flags);
        if (st == FAAD_ERR_NEED_MORE_DATA) {
            if (eof) {
                fprintf(stderr, "Incomplete ADTS frame at end of input\n");
                goto done;
            }
            need_input = 1;
            continue; /* Keep all pending bytes for the next read. */
        }
        if (st != FAAD_OK)
            fprintf(stderr, "Skipping %u bytes: %s\n",
                    (unsigned)consumed, faad_strerror(st));
        if (consumed == 0) {
            fprintf(stderr, "Decoder made no input progress\n");
            goto done;
        }
        if (st == FAAD_OK && written) {
            if (flags & FAAD_FRAME_FORMAT_CHANGED) {
                st = faad_decoder_get_info(dec, &info);
                if (st != FAAD_OK) {
                    fprintf(stderr, "%s\n", faad_strerror(st));
                    goto done;
                }
                fprintf(stderr, "%u Hz, %u channels\n",
                        (unsigned)info.sample_rate, (unsigned)info.channels);
            }
            if (flags & (FAAD_FRAME_CONCEALED | FAAD_FRAME_DEGRADED))
                fprintf(stderr, "Recovered audio: concealed=%d degraded=%d\n",
                        !!(flags & FAAD_FRAME_CONCEALED),
                        !!(flags & FAAD_FRAME_DEGRADED));
            if (fwrite(pcm, 1, written, output) != written) {
                perror("write");
                goto done;
            }
        }
        used -= consumed;
        memmove(pending, pending + consumed, used);
    }
    result = 0; /* No decode drain call is required at EOF. */

done:
    faad_decoder_close(&dec);
    free(pcm);
    if (input) fclose(input);
    if (output && fclose(output) != 0) { perror("close output"); result = 1; }
    return result;
}
```

For RAW input, replace ADTS buffering with your demuxer's access-unit loop and
open with `cfg.stream_format = FAAD_STREAM_RAW` plus the track's ASC. Reopen
when the track configuration changes. Container priming and padding remain
the application's responsibility; see [Decoder delay](#decoder-delay).

### Allocation and initialization

Defaults are ADTS, signed 16-bit PCM, and no downmix. RAW requires a valid,
nonempty AudioSpecificConfig (ASC), normally supplied by a demuxer. ASC is
optional for ADTS. The library consumes configuration and ASC during
initialization; neither buffer needs to outlive the call. Passing NULL
configuration selects the defaults.

Heap allocation uses the `AllocMemory` / `FreeMemory` macros in
`libfaad/faad_internal.h`. A source build can override them, for example with
`-DAllocMemory=my_alloc -DFreeMemory=my_free` (provide the function declarations
when compiling). Caller-owned state via `faad_decoder_init()` needs no allocator.

The heap path is:

```c
faad_config cfg;
faad_decoder *dec = NULL;
faad_status st = faad_config_init(&cfg, sizeof(cfg));
if (st == FAAD_OK)
    st = faad_decoder_open(&cfg, NULL, 0, &dec);
/* Check st, decode packets, then release the instance. */
faad_decoder_close(&dec);
```

For static allocation, query the build's exact instance size and provide a
16-byte-aligned block. This example's board-specific reserve must accommodate
the returned requirement; downmixing reduces PCM capacity, not decoder state.

```c
/* Define BOARD_AAC_STATE_BYTES for your build and memory budget. */
_Alignas(FAAD_STATE_ALIGNMENT) static uint8_t state[BOARD_AAC_STATE_BYTES];
faad_config cfg;
faad_decoder *dec = NULL;
uint32_t required = 0;
faad_status st = faad_config_init(&cfg, sizeof(cfg));
if (st == FAAD_OK)
    st = faad_get_state_size(&required);
if (st == FAAD_OK && required > sizeof(state))
    st = FAAD_ERR_INSUFFICIENT_MEM;
if (st == FAAD_OK)
    st = faad_decoder_init(state, sizeof(state), &cfg, NULL, 0, &dec);
/* No internal heap allocation; close does not free caller-owned storage. */
faad_decoder_close(&dec);
```

The following are indicative measurements from an arm64 build with default
options except as listed, not storage requirements guaranteed across builds:

| Build | Decoder state | Shared tables (.bss, once per process) | 16-bit PCM buffer |
|---|---|---|---|
| 2 channels, SBR+PS | 147 KB | 52.7 KB | 8 KB |
| 2 channels, LC only (`-Ddecoder-sbr=false`) | 41 KB | 34.3 KB | 4 KB |
| 8 channels, SBR+PS (default) | 374 KB | 52.7 KB | 32 KB |

Query state size at runtime with `faad_get_state_size()`; it is independent of
configuration and excludes shared tables, input and PCM. Table arrays use
float/int elements, so their sizes carry across targets up to alignment.
Writable tables are initialized on first initialization and remain in static
storage, separate from caller-owned state. Their placement depends on the
linker configuration. Per-frame scratch is stored in the instance, but measure
stack requirements on your target separately.

Independent handles can run concurrently; one handle is owned by one thread at a time. Instrumented
stats builds use process-global diagnostics and require external serialization.

### PCM and format discovery

Query `faad_decoder_get_info` immediately after initialization and allocate at
least `max_output_bytes`. This bound is valid for the entire instance lifetime,
including implicit SBR/PS discovery and flushes. Every decode call must supply
at least that capacity, even when its current frame would produce fewer bytes.

PCM is native-endian on both little-endian and big-endian machines:

| Format | Representation | Buffer alignment |
|---|---|---|
| 16-bit | signed `int16_t` | 2 bytes |
| 24-bit | signed packed three-byte samples | Any |
| 32-bit | native-endian signed int24 in `int32_t`, same meaning as `FAAC_INPUT_32BIT` | 4 bytes |
| Float | binary32, unity full scale | 4 bytes |

Convert native PCM to the device or file's required byte order separately.
WAVE output from the frontend is little-endian regardless of host byte order.
Channel order is documented in the header; `channel_mask` uses WAVE speaker
bits and is zero for unknown surround layouts.

`faad_stream_info` describes the most recently emitted PCM; before any PCM,
it describes the ASC/ADTS snapshot. Before ADTS discovery, `format_known` is
false and format fields are zero/`FAAD_OBJ_NULL`; capacity is already valid.
ASC may provide a usable snapshot, but implicit SBR/PS can change it later.
Set `struct_size` once to `sizeof(info)`. The library writes back the populated
size, which remains valid for later queries.

The optional `uint32_t *frame_flags` decode argument reports:

| Flag | Meaning |
|---|---|
| `FAAD_FRAME_FORMAT_CHANGED` | First emitted PCM or changed rate, channels, frame size, channel mask or decoder delay; re-query `faad_decoder_get_info()` before using PCM |
| `FAAD_FRAME_SBR` | SBR applied |
| `FAAD_FRAME_PS` | Parametric Stereo applied |
| `FAAD_FRAME_CONCEALED` | Replacement audio for corrupt core data |
| `FAAD_FRAME_DEGRADED` | Recovered audio, such as damaged SBR with intact core |

Ignore unknown flag bits. Reconfigure the audio pipeline from stream info when
`FAAD_FRAME_FORMAT_CHANGED` is set; the ADTS example shows this sequence.

### Decoder delay

`decoder_delay` is the additional decoder delay excluded from container priming,
in samples per channel at the reported output rate. Current LC output reports
zero; upsampled SBR reports 962. Convert container priming/padding from track
ticks to output samples before applying this additional delay. The frontend
adds it to leading trim and deducts it from trailing padding, bounded at zero.
Query this value at runtime; filter delay can change without changing its units.
Re-query stream info on `FAAD_FRAME_FORMAT_CHANGED` to obtain the emitted delay.

### Packet errors and seeking

Byte counts and optional `frame_flags` are zeroed on entry. Without emitted
PCM, flags remain zero and the stream-info format is unchanged.

| Result | Consumption and caller action |
|---|---|
| Invalid arguments | No consumption/output; fix arguments and retry |
| Output too small | No consumption/output or state changes; allocate the lifetime bound and retry |
| Need more data | No consumption/output or state changes; retain input and append ADTS bytes |
| Sync lost | Advance by consumed bytes; retain any unconsumed trailing sync prefix |
| Complete unsupported/rejected packet | No PCM; advance by consumed bytes instead of retrying |
| OK | Consume reported bytes and play emitted PCM; inspect recovery flags |

Invalid-argument failures also preserve decoder state. RAW framing is the
application's responsibility: pass one complete access unit. ADTS may include
subsequent frames, but one call decodes at most one frame; incomplete headers
and declared frames are safely retryable. ADTS packets containing multiple raw
data blocks are unsupported.

`FAAD_FRAME_CONCEALED` marks replacement audio after core corruption;
`FAAD_FRAME_DEGRADED` marks recovered audio such as an intact core with damaged SBR. Usable PCM returns
`FAAD_OK` with those flags, allowing playback applications to keep running.
The CLI's `--strict` mode rejects either flag as a decode failure.

Call `faad_decoder_flush` to discard synthesis and concealment history on a
seek/discontinuity. It preserves configuration, discovered format and the PCM
capacity bound, resets the PNS sequence, and cannot replay previous concealed
audio. It does not drain delayed PCM or generate audio for missing packets. Flush
preserves the last emitted format and does not force `FAAD_FRAME_FORMAT_CHANGED`.
Reopen the decoder for a different stream configuration.

## Porting from the legacy NeAACDec* API

Replace legacy `<neaacdec.h>` / `<faad.h>` with this project's `<faad.h>` and
rebuild against `libfaad` (ABI 3). Remove old include/library paths. There is
no `NeAACDec*` or `faacDec*` compatibility shim; both require migration.

- **Open and initialization.** Replace `NeAACDecHandle` with
  `faad_decoder *`. The old sequence of `NeAACDecOpen()`,
  `NeAACDecGetCurrentConfiguration()` / `NeAACDecSetConfiguration()`, and
  `NeAACDecInit()` or `NeAACDecInit2()` becomes `faad_config_init()`, field
  assignments, and one `faad_decoder_open()` call. Configuration is supplied
  once; there is no live configuration pointer or reconfiguration setter.

  `faad_decoder_init()` is the alternative for caller-owned, aligned state;
  see [Allocation and initialization](#allocation-and-initialization).
- **ADTS initialization.** For the ADTS use of `NeAACDecInit()`, select
  `FAAD_STREAM_ADTS` (the default) and open with NULL ASC and length 0.
  Opening consumes no input bytes: pass the first ADTS frame, including its
  header, to `faad_decode_frame()`. There is no initialization return value to
  skip in the input. Rate and channel count may remain unknown until decoding;
  check `faad_stream_info.format_known`; re-query on `FAAD_FRAME_FORMAT_CHANGED`.
- **RAW initialization.** For `NeAACDecInit2()`, select `FAAD_STREAM_RAW`
  and pass the demuxer's valid, nonempty AudioSpecificConfig to
  `faad_decoder_open()`. Configuration and ASC are consumed during open and
  need not outlive it. Pass exactly one complete raw AAC access unit per decode
  call. There is no fallback to a configured sample rate or object type when
  ASC is missing.
- **PCM ownership and capacity.** `NeAACDecDecode()` returned library-owned
  PCM; `faad_decode_frame()` always writes to caller-owned storage. Call
  `faad_decoder_get_info()` after open and allocate at least
  `info.max_output_bytes`, with the alignment required by the selected PCM
  format. Supply that lifetime capacity on every call, even before the stream
  format is known.

  Callers of `NeAACDecDecode2()` can retain their buffer
  management but now pass the buffer directly as `void *`, rather than as
  `void **`. A buffer sized for only the current channel count or LC frame may
  be too small for later implicit SBR/PS.
- **Decode and errors.** Replace both decode functions with
  `faad_decode_frame()`. It returns a `faad_status`, with input consumption and
  PCM byte count in separate `uint32_t` out-parameters. Replace checks of a
  returned PCM pointer or `NeAACDecFrameInfo.error` with status handling, and
  replace `NeAACDecGetErrorMessage()` with `faad_strerror(status)`.

  Follow [Packet errors and seeking](#packet-errors-and-seeking) for retries,
  skipped input and recovered PCM flags.
- **Seeking.** Replace `NeAACDecPostSeekReset(handle, frame)` with
  `faad_decoder_flush(dec)`. There is no frame-index argument. Flush discards
  audio history without draining PCM and preserves configuration and discovered
  format. Close and reopen for a different stream configuration.
- **Close.** Replace `NeAACDecClose(handle)` with
  `faad_decoder_close(&dec)`. Pass the address of the handle; it is set to NULL
  on success. Caller-owned state and PCM remain the caller's responsibility.
- **Version and capabilities.** Replace `NeAACDecGetVersion()` and
  `NeAACDecGetCapabilities()` with `faad_get_library_info()`. It reports
  library-owned version/copyright strings, the compiled channel ceiling, and
  SBR/PS support as fields rather than a capability bitmask. Use
  `FAAD_VERSION_STRING` / `FAAD_VERSION_HEX` for compile-time header version
  checks instead of `FAAD2_VERSION`.
- **Struct growth and integer widths.** Always initialize configuration with
  `faad_config_init(&cfg, sizeof(cfg))` and set info `struct_size` once to
  `sizeof(info)`. The old structs have different layouts and cannot
  be cast to the new ones. Lengths and counts are now `uint32_t`, rather than
  platform-dependent `unsigned long`; check larger container lengths before
  narrowing them.

### Configuration field mapping

| Legacy `NeAACDecConfiguration` field | New configuration or action |
|---|---|
| `outputFormat` | `faad_config.output_format`: `FAAD_FMT_16BIT`, `FAAD_FMT_24BIT`, `FAAD_FMT_32BIT`, and `FAAD_FMT_FLOAT` become `FAAD_OUTPUT_16BIT`, `FAAD_OUTPUT_24BIT`, `FAAD_OUTPUT_32BIT`, and `FAAD_OUTPUT_FLOAT` |
| `downMatrix` | `faad_config.downmix_mode`: use `FAAD_DOWNMIX_STEREO` for the old stereo downmix request, or `FAAD_DOWNMIX_NONE` to preserve the layout; `FAAD_DOWNMIX_MONO` is also available |
| `defObjectType`, `defSampleRate` | No replacement; ADTS or required RAW ASC supplies stream parameters |
| `useOldADTSFormat` | No replacement for the legacy ADTS variant |
| `dontUpSampleImplicitSBR` | No replacement; use `faad_stream_info.sample_rate` after `FAAD_FRAME_FORMAT_CHANGED` and resample outside the decoder if needed |

Stream framing is now explicit in `faad_config.stream_format`. Use
`FAAD_STREAM_RAW` or `FAAD_STREAM_ADTS`; do not copy the legacy `header_type`
numbers (`ADTS` was 2, whereas `FAAD_STREAM_ADTS` is 1). ADIF, LATM and DRM
initialization (`NeAACDecInitDRM()`) have no corresponding entry points.

The public object types are AAC-LC, HE-AAC v1 and HE-AAC v2
(`FAAD_OBJ_LC`, `FAAD_OBJ_HE_AAC_V1`, `FAAD_OBJ_HE_AAC_V2`); legacy MAIN,
SSR, LTP, LD and error-resilient profiles have no replacements.

Double and legacy fixed-point PCM modes are not exposed; select a documented
`FAAD_OUTPUT_*` representation explicitly.

### Frame metadata mapping

| Legacy `NeAACDecFrameInfo` field | New value |
|---|---|
| `bytesconsumed` | `bytes_consumed` decode out-parameter, including on errors where input was skipped |
| `samples` | `info.frame_samples * info.channels` from stream info after `FAAD_FRAME_FORMAT_CHANGED` for the total interleaved sample count; `bytes_written` is the PCM byte count |
| `samplerate`, `channels` | `faad_stream_info.sample_rate`, `channels`; re-query on `FAAD_FRAME_FORMAT_CHANGED` |
| `error` | Returned `faad_status`; also inspect `FAAD_FRAME_CONCEALED` / `FAAD_FRAME_DEGRADED` flags for recovered PCM |
| `sbr`, `ps` | `FAAD_FRAME_SBR`, `FAAD_FRAME_PS` flags; the legacy multi-valued SBR mode is not retained |
| `object_type` | `faad_stream_info.object_type`, queried with `faad_decoder_get_info()` |
| `header_type` | The configured `stream_format`; there is no per-frame header-type field |
| `num_front_channels`, `num_side_channels`, `num_back_channels`, `num_lfe_channels`, `channel_position[]` | `channel_mask` and the documented output order; there is no per-channel position array |

Review any channel reordering inherited from FAAD2: the new surround output
starts with FL, FR, FC, and `channel_mask` uses WAVE speaker bits rather than
the legacy channel-position constants. Unknown PCE surround layouts report
mask 0.

Stream info describes emitted PCM; re-query on `FAAD_FRAME_FORMAT_CHANGED`.

`NeAACDecAudioSpecificConfig()` and `mp4AudioSpecificConfig` have no standalone
public parser equivalent. Pass ASC to decoder initialization and query the
available stream metadata; applications needing detailed ASC syntax must
handle that in their container/parser layer.

## ABI compatibility

Initialize configuration with `faad_config_init(&cfg, sizeof cfg)` and each
info struct with `.struct_size = sizeof(info)`. The library writes back the
number of bytes populated; that size remains valid for later info queries.
The API/ABI is stable within the major version / SONAME. Struct layouts follow
the target platform ABI; they are not a wire format.

FAAD is free software, licensed under the GNU Lesser General Public License
(LGPL), version 2.1 or later.

Copyright © 2026, Nils Schimmelmann
