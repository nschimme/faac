# FAAC - ISO/MPEG 2/4 AAC Encoder Library

## Contents

- [Scope](#scope)
- [Interface description](#interface-description)
- [API (faac.h)](#api-faach)

  - [Calling sequence](#calling-sequence)
  - [Error handling](#error-handling)
  - [Function reference](#function-reference)
  - [Rate control modes](#rate-control-modes)
  - [Capping the peak frame size](#capping-the-peak-frame-size)

- [Porting from the legacy faacEnc* API](#porting-from-the-legacy-faacenc-api)

## Scope

This document describes the interface and usage of the
FAAC - ISO/MPEG 2/4 AAC Encoder Library
Developed for the Freeware Advanced Audio Coding project.

## Interface description

The ISO/MPEG 2/4 AAC Encoder Library provides a high-level
interface for encoding MPEG2 and MPEG4 ISO AAC files. A single header file is
provided for usage in C/C++ programs:

`faac.h`: the `faac_*` API — function prototypes and types.

The encoder is the shared library `libfaac` (`libfaac.so.N`,
libfaac.dylib or libfaac.dll; the build also produces the static archive). The
`faac` command-line encoder in `frontend/` is the reference
user of the API.

## API (faac.h)

The API supplies all encoder parameters once, up front, to
`faac_encoder_open()`. The encoder therefore never exists in a
half-configured state, and every derived quantity (frame size, output-buffer
bound, effective sample rate, resolved object type) is known and queryable the
instant open returns. Every fallible call returns a `faac_status` code;
fixed-width integer types and width-pinned enums keep the ABI identical across
platforms.

### Calling sequence

- Zero-initialize a `faac_params` with
  `faac_params_init(&p, sizeof p)`. This is mandatory — it
  stamps `struct_size` and clears padding, which is how the struct stays
  compatible as it grows in future releases; the size argument keeps a newer
  library from writing past an older caller's struct.
- Set the fields you care about (at minimum `sample_rate` and
  `num_channels`), then call `faac_encoder_open()`.
- Fill a `faac_encoder_info` with `faac_encoder_get_info()` (set its
  `struct_size` first) and size your buffers from it: input PCM holds
  `info.frame_samples` × `num_channels` samples; the output buffer
  must be at least `info.max_output_bytes`. The same struct also reports the
  resolved sample rate, object type, and rate-control settings. If you write a
  raw stream, fetch the AudioSpecificConfig with `faac_encoder_asc()`
  (library-owned — do **not** free it).
- Call `faac_encoder_encode()` for each block of input; it emits at most
  one frame per call and reports the byte count via an out-parameter.
- Pass `in_samples` == 0 to flush; keep calling until
  `bytes_written` is 0.
- Call `faac_encoder_close()`, passing the address of your handle; it is
  set to NULL on success.

### Error handling

Every fallible entry point returns a `faac_status`. `FAAC_OK` is 0 and
all error codes are negative, so `status < 0` tests for failure.
`faac_strerror()` maps any status to a static human-readable string (never
NULL). Notable codes: `FAAC_ERR_INVALID_ARGUMENT` (NULL, bad
`struct_size`, or an out-of-range field), `FAAC_ERR_UNSUPPORTED` (an
object type this build does not implement, or a request with no ASC),
`FAAC_ERR_OUTPUT_TOO_SMALL`, and `FAAC_ERR_INPUT_OVERFLOW`.

### Function reference

```c
/* library-global facts: compiled channel ceiling, version/copyright */
faac_status faac_get_library_info(faac_library_info *out);

faac_status faac_params_init(faac_params *p, uint32_t caller_size);  /* sizeof(*p) */
faac_status faac_encoder_open(const faac_params *p, faac_encoder **out);
faac_status faac_encoder_close(faac_encoder **enc);   /* sets *enc = NULL */

/* resolved properties, valid after open; set out.struct_size first */
faac_status faac_encoder_get_info(faac_encoder *enc, faac_encoder_info *out);

/* AudioSpecificConfig: library-owned, valid until close, do NOT free */
faac_status faac_encoder_asc(faac_encoder *enc, const uint8_t **buf, uint32_t *len);

faac_status faac_encoder_encode(faac_encoder *enc,
                                const void *in, uint32_t in_samples,
                                uint8_t *out, uint32_t out_cap,
                                uint32_t *bytes_written);

const char *faac_strerror(faac_status status);
```

The `object_type` field of `faac_params` uses MPEG-4 Audio Object Type
numbers: `FAAC_OBJ_LOW` (2) is AAC-LC and `FAAC_OBJ_HE_AAC_V1` (5) is
HE-AAC v1 (AAC-LC core + SBR). `FAAC_OBJ_AUTO` (0) lets the library choose
LC or HE-AAC from the bitrate, or from `quant_quality` when there is none. `FAAC_OBJ_HE_AAC_V2` (29) is defined but not
implemented; requesting it returns `FAAC_ERR_UNSUPPORTED`.

For an HE-AAC encoder the SBR core runs at half the input rate and codes a
2048-sample frame, so `faac_encoder_get_info()` reports
`frame_samples` == 2048 and `sample_rate` == the full (un-halved)
output rate. Buffer sizing and rate reporting therefore stay correct without
the caller knowing any SBR internals.

### Rate control modes

`faac_params.rate_control` selects how a rate is held. `FAAC_RC_AUTO`,
the default, keeps the legacy meaning of the two rate fields: a `bit_rate`
is ABR, none is VBR. `FAAC_RC_ABR` holds `bit_rate` as an average:
frames are bounded only by the ISO/IEC 14496-3 ceiling of 6144 bits per channel,
a transient may take several times the mean, and silence stays small.
`FAAC_RC_CBR` holds it as a constant-rate stream: a bit reservoir models the
decoder's input buffer, every frame fits `mean + fill`, a frame that would
leave the buffer to overflow is stuffed up to it, and the rate lands exactly.
ADTS output carries the fill in every header's `buffer_fullness` (ABR and
VBR write the 0x7FF sentinel); MP4 output declares `bufferSizeDB` = 768
bytes per channel and `maxBitrate` == `avgBitrate`. CBR is for
constant-rate channels and matched-bitrate comparisons; on a file or a packet
network its stuffing is bytes for nothing, and a transient may take only what
the buffer holds. `FAAC_RC_VBR` or `FAAC_RC_ABR`/`FAAC_RC_CBR`
with, respectively, a `bit_rate` set or unset is rejected by
`faac_encoder_open()` with `FAAC_ERR_INVALID_ARGUMENT`.
`faac_encoder_info.rate_control` reports the resolved mode.

In ABR and CBR the cutoff follows `bit_rate`; in VBR it is the top of
that curve, 18750 Hz. A non-zero `bandwidth` overrides either. A non-zero `quant_quality` given
together with a `bit_rate` is not rejected: it is the rate loop's starting
quality, and the loop moves on from it. `faac_encoder_info.bandwidth`
reports the cutoff in use.

### Capping the peak frame size

Packet-oriented transports often cannot fragment a frame: one that overruns the
link MTU is dropped, not split. `bit_rate` cannot prevent that, being an
average individual frames are free to exceed — a transient easily produces
a frame several times the mean. `faac_params.max_bit_rate` puts a hard
ceiling on any single frame; 0, the default, means none beyond the encoder's
own ISO/IEC 14496-3 peak ceiling of 6144 bits per channel per frame, enforced
unconditionally to guarantee decoder compatibility across portable devices.
Unlike `bit_rate` it is a whole-stream rate, so it stays independent of
`num_channels` and follows directly from the payload one packet can carry:

```text
    max_bit_rate = payload_bytes * 8 * sample_rate / 1024
```

It must be at least `bit_rate` × `num_channels` — a peak
below the average is unsatisfiable — and at most
`FAAC_MAX_BIT_RATE`; `faac_encoder_open()` returns
`FAAC_ERR_INVALID_ARGUMENT` otherwise. More headroom means fewer retries.

It is a ceiling, not a second average: frames that already fit are untouched, so
the stream stays ABR rather than becoming CBR, and a frame re-quantized to meet
the cap does not drag down the frames after it. Enforcement is best-effort
— a frame cannot shrink below the cost of its own side info and
scalefactors, so a small enough cap is still exceeded on a few percent of frames
(measured on transient-heavy stereo, the cap holds everywhere at and above
roughly 64 kbit/s and starts to slip around 48). Callers that must guarantee the
packet fits should check `bytes_written` and drop the frame themselves.

## Porting from the legacy faacEnc* API

The classic `faacEnc*` surface (`faacEncOpen`,
`faacEncGetCurrentConfiguration`/`faacEncSetConfiguration`,
`faacEncEncode`, `faacEncClose`, `faacEncGetVersion`,
`faacEncGetDecoderSpecificInfo`, and the `faacEncConfiguration`
struct from `faaccfg.h`) has been removed. There is no compatibility
shim — callers must move to `faac_*`. The changes are mechanical:

- **Open sequence.** The old two-step open (`faacEncOpen()` to get a
  handle plus `inputSamples`/`maxOutputBytes` out-params, then
  `faacEncGetCurrentConfiguration()` / `faacEncSetConfiguration()` to
  apply options after the fact) collapses into one step: build a
  `faac_params`, fill in the fields you care about, and call
  `faac_encoder_open()` once. There is no live-pointer configuration
  struct to mutate after opening.
- **Buffer sizing.** Where you previously read `inputSamples` and
  `maxOutputBytes` from `faacEncOpen()`, call
  `faac_encoder_get_info()` after `faac_encoder_open()` and use
  `info.frame_samples` × `num_channels` and
  `info.max_output_bytes` instead.
- **Version info.** `faacEncGetVersion()` is replaced by
  `faac_get_library_info()`, which also reports `max_channels`; the
  removed `name`/`copyright` write-only config fields are gone along
  with it.
- **Decoder-specific info.** `faacEncGetDecoderSpecificInfo()` is
  replaced by `faac_encoder_asc()`. The buffer is still library-owned and
  valid until close — do not free it.
- **Encoding loop.** `faacEncEncode()` becomes
  `faac_encoder_encode()` with the byte count returned via an
  out-parameter (`bytes_written`) instead of the return value; the return
  value is now a `faac_status`. Flush and end-of-stream detection are
  unchanged: pass `in_samples` == 0 to flush, and keep calling until
  `bytes_written` is 0.
- **Close.** `faacEncClose(hEncoder)` becomes
  `faac_encoder_close(&enc)` — pass the address of your handle,
  not the handle itself; the library NULLs it on success.
- **Return values.** Where the old API returned NULL handles or -1 on
  error, every fallible `faac_*` call returns a `faac_status`; test
  `status < 0` and use `faac_strerror()` for a message.
- **Configuration field renames.** `faacEncConfiguration` fields map
  onto `faac_params` fields as follows:

  - `aacObjectType` → `object_type` (now uses
    `FAAC_OBJ_*` constants, numbered per the MPEG-4 AOT registry, rather
    than the old MAIN/LOW/SSR/LTP enum — MAIN, SSR, and LTP are retired,
    unimplemented object types).
  - `mpegVersion` → `mpeg_version` (`FAAC_MPEG4` /
    `FAAC_MPEG2`).
  - `jointmode` → `joint_mode` (`FAAC_JOINT_*`).
    `allowMidside` was a compatibility alias sharing storage with
    `jointmode` in a union; it has no separate replacement, use
    `joint_mode` directly.
  - `useLfe` → `use_lfe`; `useTns` → `use_tns`
    (now `bool`).
  - `shortctl` → `short_control` (`FAAC_SHORTCTL_*`).
  - `bitRate` → `bit_rate`; `bandWidth` →
    `bandwidth`; `quantqual` → `quant_quality`.
  - `pnslevel` → `use_pns`: the level is now chosen by the
    encoder, leaving the caller an on/off switch.
  - `outputFormat` → `output_format` (`FAAC_STREAM_*`);
    `inputFormat` → `input_format` (`FAAC_INPUT_*`).
  - `channel_map[64]` → `channel_map` plus an explicit
    `channel_map_count` (the old fixed-size array was implicitly sized by
    `numChannels`).
  - `version`, `name`, and `copyright` were unused or
    library-populated informational fields in the legacy config struct; their
    closest analogs are `faac_params.struct_size` (for `version`-style
    compatibility checks) and `faac_library_info.version`/`copyright`
    (queried via `faac_get_library_info()`), not a `faac_params` field.

- **Struct growth.** Always call `faac_params_init()` before setting
  any fields, and set `struct_size` on any struct you pass to a query
  function (`faac_encoder_get_info()`, `faac_get_library_info()`).
  This is how the ABI stays stable as these structs grow in later releases;
  the legacy config struct had no equivalent versioning.

FAAC is free software, licensed under the GNU Lesser General Public License (LGPL), version 2.1 or later.

Copyright © 1999-2001, Menno Bakker ·
Copyright © 2002-2017, Krzysztof Nikiel ·
Copyright © 2004, Dan Villiom P. Christiansen ·
Copyright © 2005-2026, Fabian Greffrath ·
Copyright © 2026, Nils Schimmelmann
