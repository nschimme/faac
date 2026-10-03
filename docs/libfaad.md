# libfaad decoder API

FAAD decodes one raw AAC access unit or ADTS frame into caller-owned,
interleaved PCM. The application handles containers, transport buffering,
resampling, timestamps and audio-device playback. Both heap-backed and
caller-owned decoder state use the same decode interface.

## ABI compatibility

FAAD 3 freezes the revised pre-release API in `include/faad.h`. All configuration
and metadata structs begin with `uint32_t struct_size`. Initialize configuration
with `faad_config_init(&cfg, sizeof(cfg))`; initialize each metadata struct with
`.struct_size = sizeof(info)` before passing it to the library.

Structs grow by appending named fields. The library accepts the frozen original
layout, supplies defaults for absent configuration fields, ignores unknown
configuration tails, and writes only the metadata bytes that fit the caller's
struct. The returned size identifies how many bytes were populated. Unknown
trailing caller storage is untouched. When adding configuration fields, check
that the caller supplied the entire field before reading it. Existing field
offsets and meanings, enum values and function signatures remain unchanged
within this major ABI version; incompatible changes require a new SONAME.
Append after the explicit reserved tail bytes, never into them. Each extension
must leave explicit padding at its own tail so the next extension cannot occupy
an older layout's padding and falsely pass a populated-size check. Preserve
the struct's original alignment; wider or pointer-bearing fields must use an
alignment-compatible representation or a separate additive API.

Fixed-width integers and width-pinned enums support `-fshort-enums`. ABI
compatibility applies to binaries targeting the same platform ABI. Pointer
fields follow the target's pointer width and alignment; do not serialize these
structs or assume identical layouts between 32-bit and 64-bit machines. They are
also not a wire format between little-endian and big-endian machines.

## Allocation and initialization

Defaults are ADTS, signed 16-bit PCM, and no downmix. RAW requires a valid,
nonempty AudioSpecificConfig (ASC), normally supplied by a demuxer. ASC is
optional for ADTS. The library consumes configuration and ASC during
initialization; neither buffer needs to outlive the call. Passing NULL
configuration selects the defaults.

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
#define BOARD_AAC_STATE_BYTES (1024u * 1024u)
_Alignas(FAAD_STATE_ALIGNMENT) static uint8_t state[BOARD_AAC_STATE_BYTES];
faad_config cfg;
faad_decoder *dec = NULL;
uint32_t required = 0;
faad_status st = faad_config_init(&cfg, sizeof(cfg));
if (st == FAAD_OK)
    st = faad_get_state_size(&cfg, &required);
if (st == FAAD_OK && required > sizeof(state))
    st = FAAD_ERR_INSUFFICIENT_MEM;
if (st == FAAD_OK)
    st = faad_decoder_init(state, sizeof(state), &cfg, NULL, 0, &dec);
/* No internal heap allocation; close does not free caller-owned storage. */
faad_decoder_close(&dec);
```

The state size excludes shared tables, caller-owned input and PCM buffers.
First initialization builds process-wide tables once. Independent handles can
run concurrently; one handle is owned by one thread at a time. Instrumented
stats builds use process-global diagnostics and require external serialization.

## PCM and format discovery

Query `faad_decoder_get_info` immediately after initialization and allocate at
least `max_output_bytes`. This bound is valid for the entire instance lifetime,
including implicit SBR/PS discovery and flushes. Every decode call must supply
at least that capacity, even when its current frame would produce fewer bytes.

PCM is native-endian on both little-endian and big-endian machines:

| Format | Representation | Buffer alignment |
|---|---|---|
| 16-bit | signed `int16_t` | 2 bytes |
| 24-bit | signed packed three-byte samples | Any |
| 32-bit | signed full-scale `int32_t` | 4 bytes |
| Float | binary32, unity full scale | 4 bytes |

Convert native PCM to the device or file's required byte order separately.
WAVE output from the frontend is little-endian regardless of host byte order.
Channel order is documented in the header; `channel_mask` uses WAVE speaker
bits and is zero for unknown surround layouts.

Stream info is a current snapshot, not a promise that the format is final.
Before ADTS discovery, `format_known` is false and format fields are zero,
including `FAAD_OBJ_NULL`; capacity is already valid. ASC can establish a
snapshot immediately, except for unresolved PCE layouts. Later implicit SBR/PS
can change rate, channel count and frame size. Use frame metadata to interpret
the PCM actually emitted, and reconfigure the audio pipeline when necessary.

```c
faad_stream_info stream = { .struct_size = sizeof(stream) };
faad_status st = faad_decoder_get_info(dec, &stream);
/* Allocate aligned PCM storage of at least stream.max_output_bytes. */
faad_frame_info frame = { .struct_size = sizeof(frame) };
uint32_t consumed = 0, written = 0;
if (st == FAAD_OK)
    st = faad_decode_frame(dec, packet, packet_bytes, &consumed,
                           pcm, stream.max_output_bytes, &written, &frame);
/* On FAAD_OK, written bytes use frame.sample_rate and frame.channels. */
```

`decoder_delay` is the additional decoder delay excluded from container priming,
in samples per channel at the reported output rate. Current LC output reports
zero; upsampled SBR reports 962. Convert container priming/padding from track
ticks to output samples before applying this additional delay. The frontend
adds it to leading trim and deducts it from trailing padding, bounded at zero.
Query this value at runtime: `FAAD_SBR_DELAY` is no longer public, and the
implementation can change its filter delay without changing the API's units.
Frame delay is authoritative when SBR is discovered during decoding.

## Packet errors and seeking

Byte counts are zeroed on entry. On calls emitting no PCM, valid frame metadata
storage is cleared and its populated size is reported. `frame_info` may be NULL.

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

`concealed` marks replacement audio after core corruption; `degraded` marks
recovered audio such as an intact AAC core with damaged SBR. Usable PCM returns
`FAAD_OK` with those flags, allowing playback applications to keep running.
The CLI's `--strict` mode rejects either flag as a decode failure.

Call `faad_decoder_flush` to discard synthesis and concealment history on a
seek/discontinuity. It preserves configuration, discovered format and the PCM
capacity bound, resets the PNS sequence, and cannot replay previous concealed
audio. It does not drain delayed PCM or generate audio for missing packets.
Reopen the decoder for a different stream configuration.

## Porting from the legacy NeAACDec* API

The FAAD2 interface in `../faad2/include/neaacdec.h` and its compatibility
`faad.h` (generated from `faad.h.in`) is replaced by `include/faad.h` in this
tree. Include `<faad.h>` with this project's include directory on your search
path, and link against this project's `libfaad` (shared-library ABI 3).
Installed builds provide the `faad` pkg-config module. Remove the old FAAD2
include/library paths so an old header or library cannot be selected by mistake.
Rebuild callers: this is a new source and binary interface, with no `NeAACDec*`
or `faacDec*` compatibility shim. The latter names were aliases in the legacy
header and require the same migration.

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
  check `faad_stream_info.format_known` and use emitted frame metadata.
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
  format is known. Callers of `NeAACDecDecode2()` can retain their buffer
  management but now pass the buffer directly as `void *`, rather than as
  `void **`. A buffer sized for only the current channel count or LC frame may
  be too small for later implicit SBR/PS.
- **Decode and errors.** Replace both decode functions with
  `faad_decode_frame()`. It returns a `faad_status`, with input consumption and
  PCM byte count in separate `uint32_t` out-parameters. Replace checks of a
  returned PCM pointer or `NeAACDecFrameInfo.error` with status handling, and
  replace `NeAACDecGetErrorMessage()` with `faad_strerror(status)`.
  `status < 0` identifies a non-success result, but streaming callers must
  distinguish retryable `FAAD_ERR_NEED_MORE_DATA`, skipped bytes on
  `FAAD_ERR_SYNC_LOST`, and consumed rejected packets. Follow the consumption
  rules in [Packet errors and seeking](#packet-errors-and-seeking); do not
  blindly retry every negative status. Recovered PCM returns `FAAD_OK` with
  `concealed` or `degraded` set.
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
  `faad_config_init(&cfg, sizeof(cfg))` and set metadata `struct_size` before
  each query or decode call. The old structs have different layouts and cannot
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
| `dontUpSampleImplicitSBR` | No replacement; use the actual output rate in frame metadata and resample outside the decoder if needed |

Stream framing is now explicit in `faad_config.stream_format`. Use
`FAAD_STREAM_RAW` or `FAAD_STREAM_ADTS`; do not copy the legacy `header_type`
numbers (`ADTS` was 2, whereas `FAAD_STREAM_ADTS` is 1). ADIF, LATM and DRM
initialization (`NeAACDecInitDRM()`) have no corresponding entry points.
The public object types are AAC-LC, HE-AAC v1 and HE-AAC v2
(`FAAD_OBJ_LC`, `FAAD_OBJ_HE_AAC_V1`, `FAAD_OBJ_HE_AAC_V2`); legacy MAIN,
SSR, LTP, LD and error-resilient profiles have no replacements. Double and
legacy fixed-point PCM modes are not exposed; select a documented
`FAAD_OUTPUT_*` representation explicitly.

### Frame metadata mapping

| Legacy `NeAACDecFrameInfo` field | New value |
|---|---|
| `bytesconsumed` | `bytes_consumed` decode out-parameter, including on errors where input was skipped |
| `samples` | `frame.samples_per_ch * frame.channels` for the total interleaved sample count; `bytes_written` is the PCM byte count |
| `samplerate`, `channels` | `faad_frame_info.sample_rate`, `channels` |
| `error` | Returned `faad_status`; also inspect `concealed` / `degraded` for recovered PCM |
| `sbr`, `ps` | Boolean `sbr_active`, `ps_active`; the legacy multi-valued SBR mode is not retained |
| `object_type` | `faad_stream_info.object_type`, queried with `faad_decoder_get_info()` |
| `header_type` | The configured `stream_format`; there is no per-frame header-type field |
| `num_front_channels`, `num_side_channels`, `num_back_channels`, `num_lfe_channels`, `channel_position[]` | `channel_mask` and the documented output order; there is no per-channel position array |

Review any channel reordering inherited from FAAD2: the new surround output
starts with FL, FR, FC, and `channel_mask` uses WAVE speaker bits rather than
the legacy channel-position constants. Unknown PCE surround layouts report
mask 0. Frame metadata describes the PCM actually emitted; implicit SBR/PS can
change rate, channel count and frame length after open.

`NeAACDecAudioSpecificConfig()` and `mp4AudioSpecificConfig` have no standalone
public parser equivalent. Pass ASC to decoder initialization and query the
available stream metadata; applications needing detailed ASC syntax must
handle that in their container/parser layer.

## Validation

`meson test` includes baseline/future-size ABI canaries, a short-enum caller,
packet-boundary retries, real-audio retry/flush comparisons and numeric PCM
format checks. Run the decoder-only, one-/two-/eight-channel, feature-disabled,
stats and address/undefined-sanitizer configurations when changing the API.

`tests/faad_abi_compile.c` checks header layouts for freestanding 32-/64-bit
targets, including PowerPC big-endian and Windows LLP64. On Linux,
`python3 tests/faad_portability_test.py` cross-builds and runs the tests under
QEMU for i686, x86_64, PowerPC and s390x. Required toolchains are listed in the
script; build directories are temporary and the source can be read-only.
