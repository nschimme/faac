# libfaad3 - AAC decoder library

## Contents

- [Overview](#overview)
- [Building and linking](#building-and-linking)
- [Supported streams](#supported-streams)
- [Using the API](#using-the-api)
- [Example](#example)
- [Allocation and memory](#allocation-and-memory)
- [PCM and format discovery](#pcm-and-format-discovery)
- [Dynamic range control](#dynamic-range-control)
- [Decoder delay](#decoder-delay)
- [Packet errors and seeking](#packet-errors-and-seeking)
- [Porting from the legacy NeAACDec* API](#porting-from-the-legacy-neaacdec-api)
- [ABI compatibility](#abi-compatibility)

## Overview

libfaad3 decodes one raw AAC access unit or ADTS frame into caller-owned,
interleaved PCM. The application handles containers, transport buffering,
resampling, timestamps and playback. Include `<faad3.h>` and link `-lfaad3`
(pkg-config module `faad3`).

The shared library is `libfaad3` (`libfaad3.so.0`, `libfaad3.0.dylib` or
`libfaad3.dll`; a static archive is also built). The `faad` CLI in `frontend/`
is an example of the API.

## Building and linking

```sh
meson setup build-faad -Dencoder=false -Dfrontend=false
meson compile -C build-faad
meson install -C build-faad
cc -std=c11 -Wall -Wextra adts_decode.c -o adts_decode $(pkg-config --cflags --libs faad3)
```

- Shared and static libraries are built by default; pick one with
  `-Ddefault_library=shared|static`. Drop `-Dfrontend=false` to build the CLI.
- Static linking: `pkg-config --cflags --libs --static faad3`. `--static` adds
  private dependencies but does not choose the archive; install only the static
  library or use your toolchain's archive selection.
- Custom prefix: set `PKG_CONFIG_PATH` and the runtime library search path.
  Remove legacy FAAD2 include and library paths.

### Without Meson

Every source includes the `config.h` that Meson writes. A build that compiles the
sources directly must define `PACKAGE`, `PACKAGE_VERSION`, `MAX_CHANNELS`
(`-Dmax-channels`, default 8) and, in a combined build with the encoder,
`FAAC_SBR_DECIMATION`. Add `common/` and `include/` to the include path, and
define `FAAD_BUILDING` for a Windows DLL.

## Supported streams

| Feature | Option (default) | Runtime check | When absent |
|---|---|---|---|
| AAC-LC | always built | none | |
| HE-AAC v1 (SBR) | `decoder-sbr` (true) | `flags & FAAD_LIB_SBR` | no SBR output |
| HE-AAC v2 (SBR + PS) | `decoder-sbr` and `decoder-ps` (true); needs 2+ compiled channels | `flags & FAAD_LIB_PS` and `max_channels` | no PS stereo output |
| DAB+ (960/120-sample frames) | `decoder-dab` (true) | `flags & FAAD_LIB_DAB` | 960-sample streams fail to open with `FAAD_ERR_UNSUPPORTED`; 1024-sample streams are unaffected |
| Dynamic range control | `decoder-drc` (true) | `flags & FAAD_LIB_DRC` | DRC settings are accepted and ignored |
| ADTS | always built | select `FAAD_STREAM_ADTS` | one raw data block per ADTS frame |
| RAW | always built | select `FAAD_STREAM_RAW` | needs a valid ASC at open and one complete access unit per call |
| MP4/M4A | external demuxer | none | use [libfaam](libfaam.md#reading-mp4-for-the-faad-decoder) for the ASC and RAW access units |
| Channels | `max-channels` (8), range 1-8 | `max_channels` | streams above the ceiling cannot be decoded; downmix does not raise it. Builds under 3 channels reject coupling channel elements (CCE) as unsupported |
| PCM output | always built | `faad_config.output_format` | signed 16-bit, packed 24-bit, 24-in-32, or 32-bit float |
| ADIF, LATM, DRM, legacy profiles | unsupported | | see [Configuration fields](#configuration-fields) |

`faad_get_library_info()` (set `struct_size` first; fields are `struct_size`,
`version`, `copyright`, `max_channels`, `flags`) is the only way to learn build
options; header constants do not identify them. HE-AAC can signal extensions
implicitly mid-stream, so rely on frame flags and stream info even if the first
snapshot says AAC-LC.

## Using the API

Configuration is supplied once, at open. The PCM capacity is queryable at once,
before the stream's rate and channel count are known. Every fallible call returns
a `faad_status`.

### Calling sequence

1. `faad_config_init(&cfg, sizeof cfg)`, then set what you need. Defaults are
   ADTS, signed 16-bit PCM and no downmix. A NULL config at open selects them.
2. `faad_decoder_open()`. For RAW, select `FAAD_STREAM_RAW` and pass a valid,
   nonempty AudioSpecificConfig (ASC) from your demuxer. For ADTS the ASC is
   optional. Config and ASC are consumed at open and need not outlive it.
3. Set `info.struct_size = sizeof info` and call `faad_decoder_get_info()`.
   Allocate aligned PCM storage of `info.max_output_bytes` and pass that capacity
   on every decode call.
4. `faad_decode_frame()` per packet. RAW input is one complete access unit. ADTS
   input may hold several frames but one call decodes at most one. Advance by
   `bytes_consumed`. On `FAAD_FRAME_FORMAT_CHANGED`, re-query the info before
   using the PCM.
5. After a seek or discontinuity, `faad_decoder_flush()` discards audio history
   (it does not drain PCM). Reopen for a different stream configuration.
6. `faad_decoder_close(&dec)` frees decoder state and sets the handle to NULL.
   Your PCM buffer stays yours.

### Functions

```c
/* Version, compiled channel ceiling, SBR/PS/DAB/DRC support. */
faad_status faad_get_library_info(faad_library_info *out_info);

faad_status faad_config_init(faad_config *config, uint32_t caller_size);
faad_status faad_decoder_open(const faad_config *config,
                              const uint8_t *asc_buf, uint32_t asc_len,
                              faad_decoder **out);
faad_status faad_decoder_close(faad_decoder **decoder);  /* sets *decoder = NULL */

/* Current format and lifetime PCM capacity; set out_info->struct_size. */
faad_status faad_decoder_get_info(const faad_decoder *decoder,
                                  faad_stream_info *out_info);
faad_status faad_decode_frame(faad_decoder *decoder,
                              const uint8_t *in_buf, uint32_t in_bytes,
                              uint32_t *bytes_consumed,
                              void *out_pcm, uint32_t out_cap_bytes,
                              uint32_t *bytes_written,
                              uint32_t *frame_flags);
faad_status faad_decoder_flush(faad_decoder *decoder);

const char *faad_strerror(faad_status status);
```

### Configuration

`faad_config_init()` sets the defaults below.

| Field | Default | Meaning |
|---|---|---|
| `stream_format` | `FAAD_STREAM_ADTS` | `ADTS`, or `RAW` (needs an ASC at open) |
| `output_format` | `FAAD_OUTPUT_16BIT` | `16BIT`, `24BIT` (packed), `24IN32` or `FLOAT` |
| `downmix_mode` | `FAAD_DOWNMIX_NONE` | `NONE` keeps the layout; `STEREO` or `MONO` downmix |
| `drc_cut`, `drc_boost` | 0 | DRC strength 0-127; 0 is off. See [Dynamic range control](#dynamic-range-control) |
| `drc_target_level` | 0 | 0 is off, else 40-127 in quarter-dB below full scale (96 is -24 dBFS) |

### Errors

`FAAD_OK` is 0 and every error is negative. `faad_strerror()` returns a static
string, never NULL. Check both the status and the input consumed:

- `FAAD_ERR_NEED_MORE_DATA`: append ADTS bytes and retry.
- `FAAD_ERR_SYNC_LOST`: reports the bytes skipped.
- A complete rejected packet also reports consumption; skip it.
- Recovered PCM returns `FAAD_OK` with `FAAD_FRAME_CONCEALED` or
  `FAAD_FRAME_DEGRADED` set.

Retry and consumption rules per result are in
[Packet errors and seeking](#packet-errors-and-seeking).

## Example

`adts_decode.c` reads an ADTS file and writes native-endian interleaved signed
16-bit PCM: `./adts_decode input.aac output.pcm`. The output has no header,
timestamps or gapless trimming, and rate or channel changes are printed to stderr.
For playback, reconfigure the audio sink from the stream info after each format
change before submitting PCM.

The loop keeps partial input, skips rejected packets, accepts concealed and
degraded audio, and treats an incomplete final frame as an error.

```c
#include <faad3.h>
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

For RAW input, replace the ADTS buffering with your demuxer's access-unit loop
and open with `cfg.stream_format = FAAD_STREAM_RAW` plus the track's ASC. Reopen
when the track configuration changes. Container priming and padding are the
application's job; see [Decoder delay](#decoder-delay).

## Allocation and memory

Heap use goes through the `AllocMemory` / `FreeMemory` macros in
`libfaad/faad_internal.h`. A source build can override them, for example
`-DAllocMemory=my_alloc -DFreeMemory=my_free` (declare the functions when
compiling). The allocator must return storage aligned for C types, as `malloc()`
does. Embedded builds can place the blocks in internal RAM, PSRAM or a pool.

`faad_decoder_open()` allocates a small handle plus separate blocks for spectra,
overlap, SBR channels, SBR working buffers and PCM staging. With explicit SBR or
PS configuration the optional state is allocated at open; for ADTS it is allocated
on first use when the bitstream signals it.

Indicative figures from an arm64 build with default options except as listed; they
are not guarantees:

| Build | Decoder state | Shared tables (.bss, once per process) | 16-bit PCM buffer |
|---|---|---|---|
| 2 channels, SBR+PS | 116 KB | 52.7 KB | 8 KB |
| 2 channels, LC only (`-Ddecoder-sbr=false`) | 41 KB | 34.3 KB | 4 KB |
| 8 channels, SBR+PS (default) | 336 KB | 52.7 KB | 32 KB |

- Totals exclude shared tables, input and your PCM buffer.
- Writable tables are initialized at first open and stay in static storage; their
  placement depends on the linker configuration.
- Per-frame scratch is in the instance; measure stack on your target.
- Independent handles may run concurrently; one handle belongs to one thread at a
  time. Stats builds use process-global diagnostics and need external
  serialization.

### Running without a heap

A fixed pool can replace the system heap. Use the `pool_alloc` / `pool_free` bump
allocator from the [FAAC example](libfaac.md#running-without-a-heap) and build libfaad
with `-DAllocMemory=pool_alloc -DFreeMemory=pool_free`.

- Use one decoder at a time with this pool and serialize access.
- Read `used` after open and after decoding streams that exercise SBR/PS, since
  ADTS can allocate optional state on first use. Reserve room for it before
  decoding.
- A bump allocator reclaims nothing, including blocks freed during a failed open.
  Reset only after close or a failed open, once no pool-backed instance remains.
  If the pool is shared across libraries, reset only after every pool-backed
  handle is closed.
- Exhaustion returns `FAAD_ERR_NO_MEMORY`.
- Shared tables, stack, input and your PCM buffer are outside the pool.

## PCM and format discovery

Query `faad_decoder_get_info()` right after open and allocate `max_output_bytes`.
The bound holds for the instance's whole life, including implicit SBR/PS discovery
and flushes. Every decode call must offer at least that capacity, even when the
current frame is smaller.

PCM is native-endian on all hosts; convert to the device or file byte order
separately (the frontend's WAVE output is always little-endian).

| Format | Representation | Alignment |
|---|---|---|
| 16-bit | signed `int16_t` | 2 bytes |
| 24-bit | signed packed 3-byte samples | any |
| `FAAD_OUTPUT_24IN32` | 24 significant bits in a native-endian 32-bit word (ALSA S24_LE style) | 4 bytes |
| Float | binary32, unity full scale | 4 bytes |

Channel order is documented in the header. `channel_mask` uses WAVE speaker bits
and is 0 for unknown surround layouts.

`faad_stream_info` describes the most recently emitted PCM, or the ASC/ADTS
snapshot before any PCM. Until ADTS is discovered, `FAAD_STREAM_FORMAT_KNOWN` is
clear in `flags` and the format fields are zero (`FAAD_OBJ_NULL`), but the capacity
is already valid. An ASC may give a usable snapshot, though implicit SBR/PS can
change it later. Set `struct_size` once; the library writes back the populated size.

`faad_stream_info` fields:

| Field | Meaning |
|---|---|
| `sample_rate`, `channels` | Rate and channel count of the emitted PCM |
| `object_type` | Detected type: LC, HE-AAC v1 or v2 |
| `decoder_delay` | See [Decoder delay](#decoder-delay) |
| `frame_samples` | Samples/channel per frame: 1024, or 960 for DAB+ streams, doubled (2048 or 1920) with upsampled SBR |
| `max_output_bytes` | Lifetime PCM capacity, including implicit SBR/PS |
| `channel_mask` | WAVE speaker mask; 0 for an unknown layout |
| `flags` | `FAAD_STREAM_FORMAT_KNOWN`, `FAAD_STREAM_PROG_REF_LEVEL` |
| `prog_ref_level` | Programme reference level, quarter-dB below full scale; valid only with `FAAD_STREAM_PROG_REF_LEVEL` |

The optional `frame_flags` output reports:

| Flag | Meaning |
|---|---|
| `FAAD_FRAME_FORMAT_CHANGED` | First PCM, or a change in rate, channels, frame size, channel mask or decoder delay. Re-query the info before using the PCM |
| `FAAD_FRAME_SBR` | SBR applied |
| `FAAD_FRAME_PS` | Parametric Stereo applied |
| `FAAD_FRAME_CONCEALED` | Replacement audio for corrupt core data |
| `FAAD_FRAME_DEGRADED` | Recovered audio, such as damaged SBR over an intact core |
| `FAAD_FRAME_DRC` | Compression or normalisation was applied to this frame |
| `FAAD_FRAME_DRC_INFO` | The frame carried DRC data, whether or not it was applied |

Ignore unknown flag bits.

## Dynamic range control

Some streams carry DRC metadata (`dynamic_range_info`) and a programme reference
level. DRC is off unless you set a field; it needs the `decoder-drc` build option
(`FAAD_LIB_DRC`), otherwise the settings are accepted and ignored.

- **`drc_cut`** and **`drc_boost`** scale how strongly the stream's gain control
  attenuates loud passages and boosts quiet ones. 0 disables each; 127 applies the
  full encoded gain.
- **`drc_target_level`** normalises loudness: the decoder applies the gain that moves
  the stream's programme reference level to the target, in quarter-dB steps. It does
  nothing for a stream without a reference level;
  `faad_stream_info.flags & FAAD_STREAM_PROG_REF_LEVEL` says whether one exists, and
  `prog_ref_level` holds it (quarter-dB below full scale).
- Per frame, `FAAD_FRAME_DRC_INFO` says DRC data was present and `FAAD_FRAME_DRC` says
  something was applied.
- Boost, and a target louder than the reference level (a smaller value), can raise
  peaks. Integer output gets a
  soft limiter above about -1 dBFS; float output is left unclipped.

Without DRC settings, streams with `dynamic_range_info` but no reference level are not
attenuated (legacy FAAD2 treated the missing level as 0 dBFS and played them 20 dB
quiet).

## Decoder delay

`decoder_delay` is extra decoder delay that container priming does not include,
in samples/channel at the reported output rate. LC output reports 0 and
upsampled SBR reports 962. Query it at run time and re-query on
`FAAD_FRAME_FORMAT_CHANGED`.

Trimming a file with gapless data (see `frontend/faad_main.c`), once the output
format is known:

1. Convert the container's delay and padding (track-timescale samples) to output
   samples: `(uint64_t)value * output_rate / track_timescale`. For HE-AAC the
   timescale is normally the core rate. If it is 0, the frontend assumes the core
   rate: half the output rate for SBR with 2048-sample frames, otherwise the output
   rate.
2. Add `decoder_delay` to the leading trim and subtract it from the converted padding,
   bounded at 0.
3. Drop the leading samples per channel and keep enough trailing PCM to discard the
   padding at EOF. Re-query the stream info on `FAAD_FRAME_FORMAT_CHANGED`.

With an edit list, playable audio ends `decoder_delay` samples past the end of
the edit, so a player that cuts at the edit end loses the last `decoder_delay`
samples of an SBR stream. Keep the access units starting up to that far beyond it.

## Packet errors and seeking

Byte counts and `frame_flags` are zeroed on entry. With no emitted PCM, flags stay
zero and the stream info is unchanged.

| Result | Consumption and action |
|---|---|
| Invalid arguments | No consumption, output or state change; fix and retry |
| Output too small | No consumption, output or state change; allocate the lifetime bound and retry |
| Need more data | No consumption, output or state change; keep the input and append ADTS bytes |
| Sync lost | Advance by the consumed bytes; keep any unconsumed trailing sync prefix |
| Complete unsupported or rejected packet | No PCM; advance by the consumed bytes, do not retry |
| OK | Consume the reported bytes and play the PCM; check the recovery flags |

- RAW framing is yours: pass one complete access unit.
- ADTS may carry later frames but one call decodes at most one. Incomplete headers
  and declared frames are safely retryable. ADTS packets with multiple raw data
  blocks are unsupported.
- Usable recovered PCM returns `FAAD_OK` with `FAAD_FRAME_CONCEALED` or
  `FAAD_FRAME_DEGRADED`, so playback keeps running. The CLI's `--strict` rejects
  either as a failure.

`faad_decoder_flush()` discards synthesis and concealment history on a seek or
discontinuity. It keeps the configuration, discovered format and PCM capacity
bound, resets the PNS sequence, and cannot replay earlier concealed audio. It does
not drain delayed PCM, generate audio for missing packets, or force
`FAAD_FRAME_FORMAT_CHANGED`. Reopen for a different stream configuration.

## Porting from the legacy NeAACDec* API

The header (`faad3.h`), library (`libfaad3`) and pkg-config module (`faad3`) all
differ in name from the legacy FAAD2 files, so both can be installed side by side.
Replace `<neaacdec.h>` / `<faad.h>` with `<faad3.h>`, rebuild against `libfaad3`
(ABI 3) and remove old include and library paths. There is no `NeAACDec*` or
`faacDec*` compatibility code in this library. The FAAD2 repository
(FreewareAdvancedAudio/faad2) ships a thin `libfaad.so.2` shim over libfaad3 for
programs that cannot be rebuilt; it reproduces the behaviours in
[Legacy behaviour](#legacy-behaviour-you-may-be-working-around). New code should use
libfaad3 directly.

| Legacy | Now |
|---|---|
| `NeAACDecHandle` | `faad_decoder *` |
| `NeAACDecOpen()`, `NeAACDecGetCurrentConfiguration()`, `NeAACDecSetConfiguration()`, `NeAACDecInit()` / `NeAACDecInit2()` | `faad_config_init()`, field assignments, one `faad_decoder_open()`. Config is supplied once; no live config pointer or setter |
| `NeAACDecInit()` (ADTS) | `FAAD_STREAM_ADTS` (default), NULL ASC, length 0. Open consumes no input: pass the first frame, header included, to `faad_decode_frame()`. Rate and channels may stay unknown until decoding; check `FAAD_STREAM_FORMAT_KNOWN` and re-query on `FAAD_FRAME_FORMAT_CHANGED` |
| `NeAACDecInit2()` (RAW) | `FAAD_STREAM_RAW` plus the demuxer's valid, nonempty ASC. One complete access unit per decode call. There is no fallback to a configured rate or object type when the ASC is missing |
| `NeAACDecDecode()` / `NeAACDecDecode2()` | `faad_decode_frame()` returns a `faad_status`; consumption and PCM byte count are separate `uint32_t` out-parameters. See [Packet errors](#packet-errors-and-seeking) |
| `NeAACDecGetErrorMessage()` | `faad_strerror(status)` |
| `NeAACDecPostSeekReset(handle, frame)` | `faad_decoder_flush(dec)`; no frame index |
| `NeAACDecClose(handle)` | `faad_decoder_close(&dec)`; sets the handle to NULL |
| `NeAACDecGetVersion()`, `NeAACDecGetCapabilities()` | `faad_get_library_info()`: `flags` carries `FAAD_LIB_SBR`, `_PS`, `_DAB`, `_DRC`. Use `FAAD_VERSION_STRING` / `FAAD_VERSION_HEX` instead of `FAAD2_VERSION` |
| `NeAACDecAudioSpecificConfig()`, `mp4AudioSpecificConfig` | No standalone parser; pass the ASC to open. Detailed ASC syntax is the container layer's job |

**PCM ownership.** `NeAACDecDecode()` returned library-owned PCM;
`faad_decode_frame()` always writes into your storage. Allocate
`info.max_output_bytes` with the alignment of the chosen format and offer it on
every call, even before the format is known. `NeAACDecDecode2()` callers keep their
own buffers but now pass `void *`, not `void **`. A buffer sized for the current
channel count or an LC frame may be too small once implicit SBR/PS appears.

**Struct and integer changes.** Always `faad_config_init(&cfg, sizeof cfg)` and set
the info `struct_size` once. The old structs cannot be cast to the new ones.
Lengths and counts are `uint32_t`, not `unsigned long`; check larger container
lengths before narrowing.

### Legacy behaviour you may be working around

Programs written for FAAD2 often carry workarounds for these; the shim keeps the
behaviours so such programs still work, but with libfaad3 the workarounds can go.

| Legacy behaviour | libfaad3 |
|---|---|
| The first decoded frame returned `samples == 0` | Every frame that produces PCM reports it; trim with `decoder_delay` and the container priming |
| Streams with `dynamic_range_info` but no programme reference level came out 20 dB quieter (missing level treated as 0 dBFS) | No attenuation. `FAAD_STREAM_PROG_REF_LEVEL` says whether a level exists; `FAAD_FRAME_DRC_INFO` says the frame carried DRC data |
| DRC and normalisation applied silently, unreadable and unchangeable | Off unless `drc_cut`, `drc_boost` or `drc_target_level` is set; `FAAD_FRAME_DRC` reports what was applied |
| Bitstream channel order (C L R, side, back, LFE) with a position array | WAVE speaker order with `channel_mask` |
| Output format and downmix settable any time; `Init` could report a wrong rate for implicit SBR at 24 kHz or below | Config is fixed at open. `faad_decoder_get_info()` gives the rate and channels actually emitted; `FAAD_FRAME_FORMAT_CHANGED` marks changes |
| LC-only streams at 24 kHz or below were upsampled to twice the rate | Emitted at their real rate |
| A decode call searched up to 768 bytes for an ADTS syncword and decoded the frame behind it | One resync step per call: `FAAD_ERR_SYNC_LOST` with `bytes_consumed` set to the bytes skipped; call again |
| Corrupt frames were errors with no samples and `bytesconsumed == 0` | Concealed audio with `FAAD_FRAME_CONCEALED`; every error reports how far to advance |
| 32-bit PCM wrapped to the opposite sign at exactly positive full scale | Clamped |
| `char` return values, unsigned on ARM Linux, so `< 0` tests failed | `faad_status`, negative on failure |
| Error numbers 1 to 34 | `faad_status` and `faad_strerror()` |

### Configuration fields

| Legacy `NeAACDecConfiguration` | New |
|---|---|
| `outputFormat` | `output_format`: `FAAD_FMT_16BIT`, `FAAD_FMT_24BIT`, `FAAD_FMT_32BIT`, `FAAD_FMT_FLOAT` become `FAAD_OUTPUT_16BIT`, `FAAD_OUTPUT_24BIT`, `FAAD_OUTPUT_24IN32`, `FAAD_OUTPUT_FLOAT` |
| `downMatrix` | `downmix_mode`: `FAAD_DOWNMIX_STEREO` for the old stereo downmix, `FAAD_DOWNMIX_NONE` to keep the layout, `FAAD_DOWNMIX_MONO` also available |
| `defObjectType`, `defSampleRate` | none; ADTS or the required RAW ASC supplies them |
| `useOldADTSFormat` | none |
| `dontUpSampleImplicitSBR` | none; read `sample_rate` after `FAAD_FRAME_FORMAT_CHANGED` and resample outside the decoder |

- Framing is explicit in `stream_format` (`FAAD_STREAM_RAW` or `_ADTS`). Do not copy
  the legacy `header_type` numbers: `ADTS` was 2, `FAAD_STREAM_ADTS` is 1.
- ADIF is unsupported, and LATM and DRM (`NeAACDecInitDRM()`) have no entry points.
- Object types are `FAAD_OBJ_LC`, `FAAD_OBJ_HE_AAC_V1` and `FAAD_OBJ_HE_AAC_V2`;
  MAIN, SSR, LTP, LD and error-resilient profiles have no replacement.
- Double and legacy fixed-point PCM are not exposed; choose a `FAAD_OUTPUT_*`
  format.

### Frame metadata

| Legacy `NeAACDecFrameInfo` | New |
|---|---|
| `bytesconsumed` | `bytes_consumed`, also on errors that skip input |
| `samples` | `bytes_written` is the PCM byte count; total interleaved samples are `frame_samples * channels` from the info |
| `samplerate`, `channels` | `faad_stream_info.sample_rate`, `channels`; re-query on `FAAD_FRAME_FORMAT_CHANGED` |
| `error` (`NeAACDecFrameInfo.error`) | the returned `faad_status`, plus the `CONCEALED` / `DEGRADED` flags for recovered PCM |
| `sbr`, `ps` | `FAAD_FRAME_SBR`, `FAAD_FRAME_PS`; the multi-valued SBR mode is gone |
| `object_type` | `faad_stream_info.object_type` |
| `header_type` | the configured `stream_format`; no per-frame field |
| `num_front_channels`, `num_side_channels`, `num_back_channels`, `num_lfe_channels`, `channel_position[]` | `channel_mask` and the documented order; no position array |

**Channel layouts.** Review any channel reordering inherited from FAAD2: surround
output starts FL, FR, FC, and `channel_mask` uses WAVE speaker bits rather than the
legacy position constants.

- FAAC signals eight channels with `channelConfiguration` 0 plus a PCE: front
  C/pair, side pair, back pair and LFE, matching fdkaac and unambiguous for Apple,
  FDK, FFmpeg and faad. ADTS repeats the in-band PCE every 32 frames from frame 0;
  MP4/RAW carry it only in the ASC.
- Configuration 7 streams are still accepted as common side/back 7.1. Both report
  WAVE order FL FR FC LFE BL BR SL SR and mask `0x63F`.
- A PCE with front SCE/CPE, one side CPE and one back CPE maps them to SL/SR and
  BL/BR. With two back CPEs and no side CPE, the first back pair is SL/SR and the
  second BL/BR (the FDK rear-surround convention). Element tags select the channels
  in both ASC and in-band PCEs.
- Other PCE surround layouts keep element order and report mask 0.
- A RAW ASC's PCE channel count sizes the output buffer before the first frame;
  ADTS discovery uses the build capacity.

## ABI compatibility

The API and ABI are stable within a major version (SONAME) on a given platform
ABI. Layouts follow the target's pointer width and alignment, so 32- and 64-bit
differ, and the structs are not a serialization format.

- Initialize config with `faad_config_init(&cfg, sizeof cfg)`. Initialize each
  output struct with `.struct_size = sizeof(info)` and nothing else.
- Initialization stores `min(caller size, library size)` in `struct_size`. That
  write-back is the support probe: before using a field appended in a newer
  release, check `struct_size >= offsetof(T, field) + sizeof(((T *)0)->field)`.
- Queries write at most the caller's `struct_size` bytes and set `struct_size` to
  the bytes populated. Reset it before reusing a struct as a query destination.
- The library reads only the input bytes `struct_size` covers; absent fields take
  their zero or default value. A `struct_size` below the struct's `FAAD_*_BASELINE`
  macro, including zero, returns `FAAD_ERR_INVALID_ARGUMENT`, as do unknown flag
  bits and out-of-range enum values.
- Tolerate unknown enum values, flag bits and status codes in anything returned.
  `FAAD_OK` is 0; `FAAD_ERR_INVALID_ARGUMENT`, `FAAD_ERR_UNSUPPORTED` and
  `FAAD_ERR_NO_MEMORY` are -1, -2 and -3; higher codes are specific to libfaad3.
- Fixed-width integers and width-pinned enums keep layouts stable under
  `-fshort-enums`. `size_t` appears only in signatures and follows the platform ABI.
- `FAAD_VERSION_*` is the project release version that every library reports; the
  SONAME is versioned separately, and `faad_get_library_info()` reports the linked
  version.

FAAD is free software, licensed under the GNU Lesser General Public License
(LGPL), version 2.1 or later.

Copyright © 2026, Nils Schimmelmann
