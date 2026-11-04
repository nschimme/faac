# libfaac - AAC encoder library

## Contents

- [Overview](#overview)
- [Building and linking](#building-and-linking)
- [Supported streams](#supported-streams)
- [Using the API](#using-the-api)
- [Example](#example)
- [Defaults and PCM input](#defaults-and-pcm-input)
- [Channel order](#channel-order)
- [Ownership and threading](#ownership-and-threading)
- [Memory](#memory)
- [Gapless output](#gapless-output)
- [Rate control](#rate-control)
- [Capping the peak frame size](#capping-the-peak-frame-size)
- [Porting](#porting)
- [ABI compatibility](#abi-compatibility)

## Overview

libfaac encodes interleaved PCM into MPEG-2/4 AAC access units, as RAW frames or
ADTS. The application handles file and container I/O, resampling, timestamps and
transport; the library does not write MP4/M4A. The API is one header, `faac.h`.

The library is `libfaac` (`libfaac.so.3`, `libfaac.3.dylib` or `libfaac.dll`; the
build also produces a static archive). The `faac` CLI in `frontend/` is the
reference user of the API.

All parameters go to `faac_encoder_open()` at once, so an encoder is never
half-configured and every derived value (frame size, buffer bound, resolved
object type) is queryable as soon as open returns. Every fallible call returns a
`faac_status`.

## Building and linking

```sh
meson setup build-faac -Ddecoder=false -Dfrontend=false
meson compile -C build-faac
meson install -C build-faac
cc -std=c11 -Wall -Wextra adts_encode.c -o adts_encode $(pkg-config --cflags --libs faac)
```

- Shared and static libraries are built by default; pick one with
  `-Ddefault_library=shared|static`. Drop `-Dfrontend=false` to build the CLI.
- Static linking: `pkg-config --cflags --libs --static faac`. `--static` adds
  private dependencies but does not choose the archive; install only the static
  library or use your toolchain's archive selection.
- Custom prefix: set `PKG_CONFIG_PATH` and the runtime library search path.

### Without Meson

Every source includes the `config.h` that Meson writes. A build that compiles the
sources directly must define:

| Macro | Meson option | Default |
|---|---|---|
| `PACKAGE`, `PACKAGE_VERSION` | | |
| `MAX_CHANNELS` | `max-channels` | 8 |
| `FAAC_SBR_DECIMATION` | `sbr-decimation` | 1 |
| `FAAC_ENCODER_SBR` | `encoder-sbr` | 1 |
| `FAAC_ENCODER_PS` | `encoder-ps` (needs SBR) | 0 |
| `FAAC_STATS` | `stats` | off |

An undefined `FAAC_ENCODER_SBR` or `FAAC_ENCODER_PS` is 0 (no HE-AAC v1 or v2);
leave the SBR sources out then, as Meson does. Add `common/` and `include/` to
the include path, and define `FAAC_BUILDING` for a Windows DLL.

## Supported streams

| Feature | Support |
|---|---|
| AAC-LC | Every build; 1024 samples/channel per frame |
| HE-AAC v1 | Dual-rate SBR; needs `encoder-sbr`; input at least 32000 Hz; 2048 samples/channel per frame |
| HE-AAC v2 | SBR + Parametric Stereo; needs `encoder-ps`, stereo input, input at least 32000 Hz; 2048 samples/channel per frame |
| MAIN, SSR, LTP, LD, ELD, USAC | Unsupported |
| ADTS | The library writes each frame's header |
| RAW | One access unit per frame; the MPEG-4 ASC is available separately |
| MP4/M4A | Mux outside the library from RAW frames and the ASC |
| MPEG-2 | `FAAC_STREAM_ADTS_MPEG2`: AAC-LC only, no ASC. Explicit HE-AAC resolves to MPEG-4 and reports `FAAC_STREAM_ADTS`. RAW is always MPEG-4 |
| Channels | 1-6 or 8, up to the compiled `max-channels`; 7 is unsupported |
| PCM | Interleaved native-endian signed integer or float |

**Sample rates.** Use a standard AAC rate: 8000, 11025, 12000, 16000, 22050,
24000, 32000, 44100, 48000, 64000, 88200 or 96000 Hz. Open accepts 7350-96000 Hz,
but the ASC signals a table index, not an arbitrary rate, and the library does not
resample.

**Build facts.** `faac_get_library_info()` reports `max_channels`, version and
copyright, `sbr_decimation` (1 is full SBR analysis density) and `flags`. Check
that the returned `struct_size` covers `flags` before reading it.

- `FAAC_LIB_SBR` is clear in an LC-only build (`-Dencoder-sbr=false`). There,
  `FAAC_OBJ_HE_AAC_V1` returns `FAAC_ERR_UNSUPPORTED` and `FAAC_OBJ_AUTO` resolves
  to LC.
- `FAAC_LIB_PS` is set only when PS and SBR are both built; otherwise
  `FAAC_OBJ_HE_AAC_V2` returns `FAAC_ERR_UNSUPPORTED`.

## Using the API

### Calling sequence

1. `faac_params_init(&p, sizeof p)`. This is mandatory: it sets defaults, stamps
   `struct_size` and clears padding, and the size argument keeps a newer library
   from writing past an older caller's struct.
2. Set the fields you need (at least `sample_rate` and `channels`) and call
   `faac_encoder_open()`.
3. Set `info.struct_size` and call `faac_encoder_get_info()`. Size buffers from it:
   input holds `info.frame_samples` x `channels` samples, output at least
   `info.max_output_bytes`. For a raw stream, take the AudioSpecificConfig from
   `info.asc` (`info.asc_size` bytes) or `faac_encoder_asc()`; both are
   library-owned, so do not free them.
4. Call `faac_encoder_encode()` per block. It emits at most one frame per call and
   returns the byte count through `bytes_written`. `in_samples` counts samples
   across all channels, in whole channel groups, up to `frame_samples * channels`.
   Smaller blocks accumulate internally, and zero output means priming or
   accumulation, not end of stream.
5. Flush with `in_samples == 0` until `bytes_written` is 0.
6. `faac_encoder_close(&enc)` sets the handle to NULL on success.

### Functions

```c
faac_status faac_get_library_info(faac_library_info *out_info);
faac_status faac_params_init(faac_params *params, uint32_t caller_size);  /* sizeof(*params) */
faac_status faac_encoder_open(const faac_params *config, faac_encoder **out);
faac_status faac_encoder_close(faac_encoder **encoder);   /* sets *encoder = NULL */

/* Valid after open; set out_info->struct_size first. */
faac_status faac_encoder_get_info(const faac_encoder *encoder, faac_encoder_info *out_info);

/* Library-owned, valid until close; do not free. */
faac_status faac_encoder_asc(const faac_encoder *encoder, const uint8_t **buf, uint32_t *len);

faac_status faac_encoder_encode(faac_encoder *encoder,
                                const void *in, uint32_t in_samples,
                                uint8_t *out, uint32_t out_cap,
                                uint32_t *bytes_written);

const char *faac_strerror(faac_status status);
```

`faac_encoder_info` holds only what the library decided: resolved
`object_type`, `stream_format`, `bit_rate`, `bandwidth` and `quality`, plus
`frame_samples`, `max_output_bytes`, `encoder_delay` and the ASC. Values you
passed unchanged (sample rate, rate control, peak rate) are not repeated.

### Parameters

`faac_params_init()` sets the defaults below.

| Field | Default | Meaning |
|---|---|---|
| `sample_rate` | required | Input rate in Hz |
| `channels` | required | 1-6 or 8, up to `max_channels` |
| `object_type` | `FAAC_OBJ_LC` | See [Object types](#object-types) |
| `stereo_mode` | `FAAC_STEREO_AUTO` | `LR`, `MS`, `IS` or per-band `AUTO` |
| `flags` | `TNS` and `PNS` | `FAAC_FLAG_*`; unknown bits are rejected |
| `bit_rate` | 0 | Whole-stream bit/s; 0 is 64000 per channel in ABR/CBR, and must be 0 in VBR |
| `bandwidth` | 0 | Cutoff in Hz; 0 derives it from `bit_rate` |
| `quality` | 0 | VBR: required, 1-5000. ABR/CBR: must be 0 |
| `rate_control` | `FAAC_RC_ABR` | See [Rate control](#rate-control) |
| `peak_bit_rate` | 0 | Per-frame ceiling; see [Capping the peak frame size](#capping-the-peak-frame-size) |
| `stream_format` | `FAAC_STREAM_ADTS` | `RAW`, `ADTS` or `ADTS_MPEG2` |
| `input_format` | `FAAC_INPUT_16BIT` | See [Defaults and PCM input](#defaults-and-pcm-input) |
| `short_control` | `FAAC_SHORTCTL_NORMAL` | `NORMAL`, `NOSHORT` (long only) or `NOLONG` (short only) |
| `channel_layout` | `FAAC_LAYOUT_AAC` | See [Channel order](#channel-order) |

### Resolved info

`faac_encoder_get_info()` returns what the library decided, valid as soon as open
returns.

| Field | Meaning |
|---|---|
| `frame_samples` | Samples/channel per frame: 1024 LC, 2048 HE-AAC |
| `max_output_bytes` | Upper bound for one `encode()` call's output; size the output buffer from it |
| `object_type` | The type in effect; `FAAC_OBJ_AUTO` is resolved |
| `stream_format` | The format in effect; HE-AAC forces MPEG-4 |
| `bit_rate` | Whole-stream bit/s after defaults, per-channel rounding and the per-channel ceiling; 0 in VBR |
| `bandwidth` | Cutoff in Hz |
| `quality` | Quantizer quality: your value in VBR; in ABR/CBR the starting point, derived from `bit_rate` (100 when you set `bandwidth` explicitly) |
| `encoder_delay` | Priming in samples/channel; see [Gapless output](#gapless-output) |
| `asc_size`, `asc[14]` | AudioSpecificConfig; `asc_size` is 0 for MPEG-2 ADTS |

### Object types

`object_type` uses MPEG-4 Audio Object Type numbers:

| Value | Meaning |
|---|---|
| `FAAC_OBJ_LC` (2) | AAC-LC |
| `FAAC_OBJ_HE_AAC_V1` (5) | AAC-LC core + SBR |
| `FAAC_OBJ_HE_AAC_V2` (29) | + Parametric Stereo; `FAAC_ERR_UNSUPPORTED` without PS |
| `FAAC_OBJ_AUTO` (0) | Library picks the type; see the rules below |

`FAAC_OBJ_AUTO` currently resolves like this (per-channel rate = `bit_rate` / `channels`),
and `get_info()` reports the result:

- **LC** when SBR is not built, the input is under 32 kHz, or the stream is MPEG-2.
- **HE-AAC v1** from 8000 bit/s per channel up to a ceiling of 1 bit/s per Hz of
  sample rate, capped at 48000 (32000 at 32 kHz, 44100 at 44.1 kHz, 48000 at 48 kHz
  and above). In VBR, when `quality` is 75 or lower.
- **HE-AAC v2** (PS builds only) for stereo with an explicit bitrate at or below
  8000 bit/s per channel at 48 kHz, or 6000 at 32 and 44.1 kHz. Just above those
  ceilings but under 8000 it stays HE-AAC v1 instead of falling to LC.
- **LC** otherwise.

An explicit `FAAC_OBJ_LC`, `FAAC_OBJ_HE_AAC_V1` or `FAAC_OBJ_HE_AAC_V2` is never
overridden. These thresholds are tuned values and may change between releases; set the
type explicitly if you need it fixed.

For HE-AAC the SBR core runs at half the input rate and codes 2048-sample
frames, so `frame_samples` is 2048. `faac_params.sample_rate` is still the full
input rate.

### Errors

`FAAC_OK` is 0 and every error is negative, so `status < 0` tests for failure.
`faac_strerror()` returns a static string, never NULL.

| Code | Meaning |
|---|---|
| `FAAC_ERR_INVALID_ARGUMENT` | NULL, bad `struct_size`, or out-of-range field |
| `FAAC_ERR_UNSUPPORTED` | Object type or channel count not built, or a request with no ASC |
| `FAAC_ERR_NO_MEMORY` | Allocation failed |
| `FAAC_ERR_OUTPUT_TOO_SMALL` | Output buffer below `max_output_bytes` |
| `FAAC_ERR_INPUT_OVERFLOW` | `in_samples` above the per-call capacity |
| `FAAC_ERR_INTERNAL` | Failure inside the core; state may have advanced, so do not assume a replay is safe |

- Always pass a non-NULL output buffer of at least `info.max_output_bytes`,
  including when flushing, and a non-NULL `bytes_written`. Input may be NULL only
  when `in_samples` is zero.
- Argument, capacity and sample-limit errors are reported before any input is
  consumed, so fix the call and retry the same block.
- Flush drains the stream; it is not a reset or seek. Submit the final partial
  block first, then flush until zero output. Close does not drain.
- To start a new stream, close and reopen.

## Example

`adts_encode.c` reads headerless native-endian interleaved signed 16-bit stereo
PCM at 48000 Hz from stdin and writes ADTS at 128 kbit/s to stdout. Streams must
be binary (set binary mode on Windows) and the application must strip WAV headers.

```c
#include <faac.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    faac_params p;
    faac_encoder *enc = NULL;
    faac_encoder_info info = { .struct_size = sizeof(info) };
    int16_t *pcm = NULL;
    uint8_t *out = NULL;
    faac_status st;
    int result = EXIT_FAILURE;

    st = faac_params_init(&p, sizeof(p));
    if (st < 0) goto api_error;
    p.sample_rate = 48000;
    p.channels = 2;
    p.bit_rate = 128000; /* whole stream, bits/sec */
    st = faac_encoder_open(&p, &enc);
    if (st < 0) goto api_error;
    st = faac_encoder_get_info(enc, &info);
    if (st < 0) goto api_error;

    uint32_t capacity = info.frame_samples * p.channels;
    size_t input_bytes = (size_t)capacity * sizeof(*pcm);
    pcm = malloc(input_bytes);
    out = malloc(info.max_output_bytes);
    if (!pcm || !out) { fputs("allocation failed\n", stderr); goto cleanup; }

    for (;;) {
        size_t used = 0;
        /* Accumulate short reads so only the final block can be partial. */
        while (used < input_bytes) {
            size_t n = fread((uint8_t *)pcm + used, 1, input_bytes - used, stdin);
            used += n;
            if (n == 0) {
                if (ferror(stdin)) { perror("read"); goto cleanup; }
                break;
            }
        }
        if (used == 0) break;
        if (used % (sizeof(*pcm) * p.channels)) {
            fputs("incomplete PCM channel group\n", stderr);
            goto cleanup;
        }
        uint32_t written = 0;
        st = faac_encoder_encode(enc, pcm, (uint32_t)(used / sizeof(*pcm)),
                                 out, info.max_output_bytes, &written);
        if (st < 0) goto api_error;
        if (fwrite(out, 1, written, stdout) != written) {
            perror("write"); goto cleanup;
        }
    }
    for (;;) {
        uint32_t written = 0;
        st = faac_encoder_encode(enc, NULL, 0, out,
                                 info.max_output_bytes, &written);
        if (st < 0) goto api_error;
        if (written == 0) break;
        if (fwrite(out, 1, written, stdout) != written) {
            perror("write"); goto cleanup;
        }
    }
    if (fflush(stdout) != 0) { perror("write"); goto cleanup; }
    result = EXIT_SUCCESS;
    goto cleanup;

api_error:
    fprintf(stderr, "FAAC: %s\n", faac_strerror(st));
cleanup:
    free(out);
    free(pcm);
    faac_encoder_close(&enc);
    return result;
}
```

## Defaults and PCM input

`faac_params_init()` selects MPEG-4 AAC-LC, automatic stereo mode (per-band M/S
and intensity), TNS and PNS on, AAC channel order, ADTS, signed 16-bit input,
normal block switching, ABR at 64000 bit/s per channel, no peak cap, and automatic
bandwidth. Only `sample_rate` and `channels` are required, plus `quality` when you
select VBR. `FAAC_OBJ_AUTO` is never the default.

VBR is selected explicitly, with a zero bitrate:

```c
p.rate_control = FAAC_RC_VBR;
p.quality = 100; /* 1-5000, required in VBR; higher is better */
```

Bandwidth is clamped to the available range; read the effective values from
`get_info()`.

| Input format | Representation | Alignment |
|---|---|---|
| `FAAC_INPUT_16BIT` | Native-endian `int16_t`, full scale | `int16_t` |
| `FAAC_INPUT_24BIT` | Packed signed 3-byte samples, full scale | any |
| `FAAC_INPUT_24IN32` | `int32_t` holding a sign-extended 24-bit sample (not full-scale 32-bit) | `int32_t` |
| `FAAC_INPUT_FLOAT` | 32-bit float, unity full scale (+-1.0) | `float` |

- All formats are interleaved.
- FAAD's float and 24-in-32 outputs can be fed back directly.
- Convert full-scale 32-bit PCM to 24-bit scale first.
- NaN, infinity and floats of magnitude 256 or more become silence. The encoder
  does not clip or normalize.

## Channel order

Mono and stereo use mono and L/R. For three or more channels,
`faac_params.channel_layout` names the input order:

| Channels | `FAAC_LAYOUT_AAC` (default) | `FAAC_LAYOUT_WAV` |
|---|---|---|
| 3 | C, L, R | L, R, C |
| 4 | C, L, R, BC | L, R, C, BC |
| 5 | C, L, R, Ls, Rs | L, R, C, Ls, Rs |
| 6 | C, L, R, Ls, Rs, LFE | L, R, C, LFE, Ls, Rs |
| 8 | C, L, R, SL, SR, BL, BR, LFE | L, R, C, LFE, BL, BR, SL, SR |

- `FAAC_LAYOUT_WAV` lets WAVE files and decoder output go straight in; the library
  reorders to AAC element order. It only reorders, never mixes or downmixes.
- Six and eight channels always carry an LFE as the trailing unpaired channel.
- For any other order, permute the samples yourself and pass `FAAC_LAYOUT_AAC`.
- Eight channels use channelConfiguration 0 and a program config element (PCE)
  listing front, side and back pairs, so Apple, FDK, FFmpeg and libfaad read the
  same 7.1. Configuration 7 is not used because ISO/IEC 14496-3 defines it as
  7.1 wide.
- ADTS repeats the PCE every 32 frames so a late-joining decoder learns the layout.
  RAW carries it in the ASC only.

## Ownership and threading

- Open copies the parameters; release yours afterwards.
- Input and output buffers stay caller-owned; PCM is copied during encode.
- The ASC and the version, copyright and error strings are library-owned; copy
  the ASC if you need it past close.
- `faac_encoder_close(&enc)` on a NULL handle succeeds.
- Independent handles may run concurrently; serialize access to one handle.
- The optional `stats` build prints diagnostics on close.

## Memory

All encoder state is allocated at open and nothing while encoding or flushing. The ASC lives in a 14-byte array in the handle: 2 bytes for LC,
5 for multichannel HE-AAC, 7 for mono HE-AAC, up to 14 for eight channels (the PCE
is part of it).

Indicative figures from an arm64 build at 48000 Hz with default options; they are
not guarantees:

| Configuration | Heap | Frame samples/channel | 16-bit input per call |
|---|---|---|---|
| Mono, AAC-LC | 154 KiB | 1024 | 2 KiB |
| Stereo, AAC-LC | 187 KiB | 1024 | 4 KiB |
| 5.1, AAC-LC | 319 KiB | 1024 | 12 KiB |
| 7.1, AAC-LC | 385 KiB | 1024 | 16 KiB |
| Mono, HE-AAC | 311 KiB | 2048 | 4 KiB |
| Stereo, HE-AAC | 353 KiB | 2048 | 8 KiB |

- Add the output buffer (`info.max_output_bytes`, 8 KiB in all rows) and your input
  buffer.
- Shared tables are separate from the heap. Writable ones are built at first open
  and stay in static storage; measure static storage and stack on your target.
- Sizes follow pointer width, alignment and options such as `-Dmax-channels`.
- HE-AAC roughly doubles the heap (half-rate core plus SBR state); use
  `FAAC_OBJ_LC` when RAM is tight.
- The core is single-precision float; double appears only in one-time table setup
  at open.

### Custom allocators

State is allocated through the `AllocMemory` / `FreeMemory` macros in
`libfaac/util.h`. A source build can override them, for example
`-DAllocMemory=my_alloc -DFreeMemory=my_free` (declare the functions when
compiling). This is the only control over where encoder memory lives, for example
to place it in external RAM; measure the speed cost first. Returned memory need
not be cleared; 16-byte alignment is sufficient.

### Running without a heap

A fixed pool can replace the system heap. Allocations happen during open only. A bump allocator must reserve space for blocks
freed during open, since it cannot reuse them.

Force-include a header declaring `pool_alloc(size_t)` and `pool_free(void *)`,
build libfaac with `-DAllocMemory=pool_alloc -DFreeMemory=pool_free`, and compile
these into your application:

```c
#include <stddef.h>

/* Define POOL_BYTES for your build and encoder configuration. */
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

- Use one encoder at a time with this pool and serialize access.
- Read `used` after open to size `POOL_BYTES`.
- Reset only after close or a failed open. If the pool is shared across libraries,
  reset only once every pool-backed handle is closed.
- Too small a pool returns `FAAC_ERR_NO_MEMORY` from open.
- Shared tables, stack and your own buffers are outside the pool.
- Matching examples: [FAAD](libfaad.md#running-without-a-heap),
  [FAAM](libfaam.md#running-without-a-heap).

## Gapless output

Encoding adds priming and trailing padding. `info.encoder_delay` is the priming
in samples/channel at the output rate; use it as is for gapless tags such as
iTunSMPB. An MP4 edit list counts in the track timescale, which for HE-AAC is the
core rate, so halve the value there (and the padding with it). For HE-AAC it is
not `frame_samples` and excludes the SBR decoder delay, which decoders add
themselves.

To write the metadata, count original PCM samples/channel (64-bit) and emitted
access units, including flush output:

- coded duration = `frame_count * info.frame_samples`
- trailing padding = excess over `info.encoder_delay + original_samples`, or zero

ADTS alone cannot carry gapless metadata. See `frontend/encode_engine.c` and
`frontend/mp4write.c` for container integration.

## Rate control

`faac_params.rate_control` selects the mode; zero is ABR. `bit_rate` is a
whole-stream rate in bit/s that the library divides by the channel count; zero
means 64000 per channel.

| Mode | Behaviour |
|---|---|
| `FAAC_RC_ABR` | `bit_rate` is an average. Frames are bounded only by the ISO/IEC 14496-3 ceiling of 6144 bits per channel; a transient may take several times the mean and silence stays small |
| `FAAC_RC_CBR` | A bit reservoir models the decoder's input buffer. Every frame fits `mean + fill`, frames that would overflow the buffer are stuffed, and the rate lands exactly |
| `FAAC_RC_VBR` | `quality` drives the size; `bit_rate` must be 0 |

- ABR and CBR reject a nonzero `quality`. VBR rejects a nonzero `bit_rate`, and a
  `quality` of 0 or above 5000. All return `FAAC_ERR_INVALID_ARGUMENT`.
- CBR writes the fill into every ADTS header's `buffer_fullness` (ABR and VBR write
  the 0x7FF sentinel). MP4 output declares `bufferSizeDB` = 768 bytes per channel
  and `maxBitrate` == `avgBitrate`.
- CBR suits constant-rate channels and matched-bitrate comparisons. On a file or
  packet network its stuffing wastes bytes, and a transient gets only what the
  buffer holds.

**Bandwidth.** In ABR and CBR the cutoff follows `bit_rate`. In VBR it is the top
of that curve, 18750 Hz, except that input above 48 kHz at quality 100 or higher
keeps the full band (half the sample rate). A nonzero `bandwidth` overrides both.
`info.bandwidth` is the cutoff in use.

## Capping the peak frame size

Packet transports often cannot fragment a frame: one over the link MTU is dropped.
`bit_rate` is only an average, so a transient can exceed it several times over.
`peak_bit_rate` is a hard ceiling on any single frame. Zero means no cap beyond
the encoder's ISO/IEC 14496-3 limit of 6144 bits per channel per frame, which
is always enforced.

It is a whole-stream rate derived from the payload one packet can carry:

```text
peak_bit_rate = payload_bytes * 8 * params.sample_rate / info.frame_samples
```

- Use 64-bit arithmetic and round down. `frame_samples` is 1024 for LC and 2048
  for HE-AAC.
- The cap budgets the AAC payload; reserve 7 more bytes for ADTS and any transport
  headers when deriving it from an MTU.
- The output buffer still needs `info.max_output_bytes`, whatever the cap.
- It must be at least `bit_rate` and at most `FAAC_MAX_PEAK_BIT_RATE`, otherwise
  open returns `FAAC_ERR_INVALID_ARGUMENT`. More headroom means fewer retries.

The cap is a ceiling, not a second average: frames that already fit are untouched
(the stream stays ABR, not CBR), and a frame re-quantized to fit does not pull
down later ones.

Enforcement is best effort, because a frame cannot shrink below its side info and
scalefactors. On transient-heavy stereo the cap held at and above about 64 kbit/s
and slipped on a few percent of frames near 48. If the packet must fit, check
`bytes_written` and drop the frame yourself.

## Porting

### From the legacy faacEnc* API

`faacEnc*` and `faaccfg.h` are removed, with no shim.

| Legacy | Now |
|---|---|
| `faacEncOpen()` then `GetCurrentConfiguration`/`SetConfiguration` | Fill a `faac_params`, call `faac_encoder_open()` once |
| `inputSamples`, `maxOutputBytes` from open | `faac_encoder_get_info()`: `frame_samples` x `channels`, `max_output_bytes` |
| `faacEncGetVersion()` | `faac_get_library_info()` |
| `faacEncGetDecoderSpecificInfo()` (caller frees) | `faac_encoder_asc()`: library-owned, valid until close; remove the `free()` |
| `faacEncEncode()` returning a byte count | `faac_encoder_encode()` returns a `faac_status`; the count is `bytes_written`. Flush is unchanged |
| `faacEncClose(h)` | `faac_encoder_close(&enc)`; sets the handle to NULL |
| NULL or -1 on error | `faac_status`; test `< 0`, message from `faac_strerror()` |

Configuration fields:

| `faacEncConfiguration` | `faac_params` |
|---|---|
| `aacObjectType` | `object_type` (`FAAC_OBJ_*`, MPEG-4 AOT numbers; MAIN, SSR and LTP are gone) |
| `mpegVersion` | `stream_format`: `FAAC_STREAM_ADTS` is MPEG-4, `FAAC_STREAM_ADTS_MPEG2` is MPEG-2; raw is always MPEG-4 |
| `jointmode` (alias `allowMidside`) | `stereo_mode` (`FAAC_STEREO_*`) |
| `useLfe` | none; six and eight channels carry an LFE |
| `useTns`, `pnslevel` | `FAAC_FLAG_TNS`, `FAAC_FLAG_PNS` in `flags`; the encoder picks the PNS level |
| `shortctl` | `short_control` (`FAAC_SHORTCTL_*`) |
| `bitRate` | `bit_rate`, now whole-stream rather than per channel |
| `bandWidth` | `bandwidth` |
| `quantqual` | `quality`, VBR only |
| `outputFormat`, `inputFormat` | `stream_format` (`FAAC_STREAM_*`), `input_format` (`FAAC_INPUT_*`) |
| `channel_map[64]` | `channel_layout` (`FAAC_LAYOUT_AAC` or `_WAV`); other permutations are the caller's |
| `version`, `name`, `copyright` | none; see `faac_params.struct_size` and `faac_library_info` |

`rate_control` has no automatic mode: zero is ABR. Always call
`faac_params_init()` first and set `struct_size` on query structs; the legacy
config used a version field instead of a caller-sized, append-only layout.

### From libfaac ABI 1 to 2

ABI 2 added `caller_size` to `faac_params_init()`: replace
`faac_params_init(&p)` with `faac_params_init(&p, sizeof(p))` and rebuild. To
compile against either header:

```c
#if FAAC_VERSION_MAJOR >= 2
    st = faac_params_init(&p, sizeof(p));
#else
    st = faac_params_init(&p);
#endif
```

`FAAC_VERSION_MAJOR` is the project release's major version, versioned
separately from the SONAME. Version 3.0 unified FAAC, FAAD and FAAM under one
project version without changing the `faac_*` API, so code written for ABI 2
builds and runs unchanged. An ABI 1 binary must not load ABI 2, and legacy
configuration or handle types must not be cast to the new ones; migrate and rebuild.

### From ABI 2 to ABI 3

**`bit_rate` keeps its name and type but is now whole-stream.** Code recompiled
with an old per-channel value builds cleanly and encodes at 1/N of the rate, N
being the channel count. Multiply every `bit_rate` by `channels`.

| ABI 2 | ABI 3 |
|---|---|
| `num_channels` | `channels` |
| `quant_quality` | `quality` |
| `max_bit_rate`, `FAAC_MAX_BIT_RATE` | `peak_bit_rate`, `FAAC_MAX_PEAK_BIT_RATE` |
| `joint_mode`, `FAAC_JOINT_*` | `stereo_mode`, `FAAC_STEREO_LR`, `_MS`, `_IS`, `_AUTO` (was `MIXED`) |
| `FAAC_OBJ_LOW` | `FAAC_OBJ_LC` |
| `output_format` | `stream_format` |
| `FAAC_INPUT_32BIT` | `FAAC_INPUT_24IN32` (same signed 24-bit scale in a native-endian 32-bit word) |
| `FAAC_INPUT_FLOAT` on the 16-bit scale | Pass unity full-scale floats (+-1.0) and drop the x32768. Old-scale input compiles but every sample of 256 or more is encoded as silence |
| `mpeg_version`, `FAAC_MPEG4`, `FAAC_MPEG2` | `FAAC_STREAM_ADTS` (MPEG-4) and `FAAC_STREAM_ADTS_MPEG2`; raw MPEG-2 can no longer be expressed. `info.stream_format` is the resolved format (HE-AAC forces MPEG-4) |
| `use_tns`, `use_pns` | `FAAC_FLAG_TNS`, `FAAC_FLAG_PNS` in `flags`; both default on, unknown bits rejected |
| ASC only via `faac_encoder_asc()` | Also `info.asc` / `info.asc_size`, valid when open returns. `get_info()` and `asc()` take a const handle |
| `FAAC_RC_AUTO` as zero | Gone. `FAAC_RC_ABR` is 0 and the default, VBR 1, CBR 2. VBR needs `bit_rate = 0`; ABR and CBR need `quality = 0` (a `quality` with a bitrate used to seed the rate loop and is now refused); VBR needs an explicit `quality` of 1-5000: 0 (the old default) and values above 5000 are refused, not defaulted or clamped |
| per-channel `bit_rate` | Whole-stream; multiply by `channels`. Zero means 64 kbit/s per channel. `info.bit_rate` is whole-stream and `peak_bit_rate` must be at least `bit_rate` |
| more channels than `max-channels` | Open returns `FAAC_ERR_UNSUPPORTED` (it used to succeed) |
| `faac_library_info` layout | Members are `struct_size`, `version`, `copyright`, `max_channels`, `sbr_decimation`, `flags`; recompile. `FAAC_LIB_SBR` and `FAAC_LIB_PS` say whether HE-AAC v1 and v2 are built |
| `use_lfe`, `channel_map`, `channel_map_count` | `channel_layout`: `FAAC_LAYOUT_AAC` (default) or `_WAV`. Other orders are permuted by the caller. `faac.h` no longer includes `<stdbool.h>` |
| `faac_encoder_info.sample_rate` | Removed; it always equalled `faac_params.sample_rate` (HE-AAC included). Use the value you passed |
| `faac_encoder_info.bit_rate`, `.quant_quality` | `bit_rate` is whole-stream; `quant_quality` is now `quality` |
| `faac_encoder_info.pns_level` | Removed; the encoder chooses the PNS level and you only switch `FAAC_FLAG_PNS` |

## ABI compatibility

- Initialize params with `faac_params_init(&p, sizeof(p))` and query structs with
  `.struct_size = sizeof(info)`.
- Queries set `struct_size` to the bytes populated. Reset it before reusing a
  struct as a query destination, and check it before reading an appended field
  from an older library.
- The library accepts its frozen baseline sizes, ignores unknown trailing
  configuration bytes, and never writes past the caller's capacity. Bytes beyond
  its known layout are untouched, so callers with newer structs should zero them.
- Structs grow additively; do not repurpose reserved padding.
- Fixed-width integers and width-pinned enums keep the layout stable under
  `-fshort-enums` on a given platform ABI. Pointer fields follow the target, so
  32- and 64-bit layouts differ. The structs are not serializable formats.
- An incompatible signature or layout change needs a new library ABI version.

FAAC is free software, licensed under the GNU Lesser General Public License (LGPL), version 2.1 or later.

Copyright © 1999-2001, Menno Bakker ·
Copyright © 2002-2017, Krzysztof Nikiel ·
Copyright © 2004, Dan Villiom P. Christiansen ·
Copyright © 2005-2026, Fabian Greffrath ·
Copyright © 2026, Nils Schimmelmann
