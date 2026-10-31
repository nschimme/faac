# libfaar - Resampler library

## Contents

- [Overview](#overview)
- [Building and linking](#building-and-linking)
- [Resampling model](#resampling-model)
- [Quality profiles](#quality-profiles)
- [Using the API](#using-the-api)
- [Example](#example)
- [Bounded streams](#bounded-streams)
- [Reusing a filter design](#reusing-a-filter-design)
- [Custom allocators](#custom-allocators)
- [ABI compatibility](#abi-compatibility)

## Overview

libfaar converts interleaved PCM between sample rates. Audio in and out is 32-bit
float at unity full scale (-1.0 to 1.0). The application handles file and container
I/O, timestamps and playback; the library reads and writes no files. Include
`<faar.h>` and link `-lfaar` (pkg-config module `faar`).

The shared library is `libfaar` (`libfaar.so.0`, `libfaar.0.dylib` or
`libfaar.dll`; a static archive is also built).

## Building and linking

```sh
meson setup build-faar -Dencoder=false -Ddecoder=false -Dfrontend=false
meson compile -C build-faar
meson install -C build-faar
cc -std=c11 -Wall -Wextra app.c -o app $(pkg-config --cflags --libs faar)
```

- Custom prefix: set `PKG_CONFIG_PATH` and the runtime library path. Static linking
  uses `pkg-config --cflags --libs --static faar` plus your toolchain's archive
  selection.

### Build options

| Option | Effect |
|---|---|
| `-Dresampler-float=true` | Float arithmetic for Fast, Good and High. Float High uses compensated float arithmetic and more CPU. Use it on MCUs without a double-precision FPU |
| default (mixed) | The float engine with High's filter kernels in native double. Output matches float at Fast/Good and double arithmetic at High |
| `-Dmax-channels` | Channel limit, default 8, range 1-8 |

Filter coefficients and FFT twiddles are always computed in double at setup. The
FFT engine keeps no static tables: each handle allocates its own twiddles, and
float High adds per-handle expansion FFT workspaces.

## Resampling model

The planner picks a filter from the rates and quality preset. Polyphase FIR filters
use Kaiser windows with interpolation between stored phases; some conversions use
overlap-save DFT FIR stages, and large downsampling ratios can cascade stages.
`faar_resampler_get_info()` reports the filter lookahead in input frames across
all stages.

FFT twiddles, work buffers and scratch are private to each handle, so independent
handles share no mutable state. Inside a handle, channels share immutable filter
data and (for serialized channel pairs) FFT workspace, while keeping separate
overlap histories and queues.

## Quality profiles

Pick the preset with `faar_config.quality`. Good is the default.

| | Fast | Good | High |
|---|---|---|---|
| Use when | memory and lookahead matter more than passband | general use | stopband rejection matters more than cost |
| Passband | about 3.4 dB down near Nyquist | flat (under 0.01 dB) | flat (under 0.01 dB) |
| Min stopband rejection | about 106 dB | about 139 dB | about 154 dB |
| Worst residual | about -116 dB | about -135 dB | about -149 dB |
| Min image rejection | about 116 dB | about 147 dB | about 162 dB |
| Max lookahead | about 1 ms | about 3 ms | about 31 ms |
| CPU time vs Good | slightly less (about 15%) | 1x | roughly 1.5-2x |
| Setup heap | about 0.15 MiB | about 0.4 MiB | about 4 MiB |

- **Scale.** 16-bit audio has a noise floor about 96 dB below full scale and 24-bit about
  144 dB. Fast's stopband is already under the 16-bit floor, Good's is near the 24-bit
  floor and High's is below it, so High's extra rejection matters mainly for 24-bit or
  float output.
- **Good to High** buys about 15 dB more stopband rejection and lower residual and image
  floors for roughly 1.5-2x the CPU time and 10x the setup heap. High also adds about
  28 ms of lookahead (reported via `delay_frames` in `faar_resampler_info`). `libfaar`
  centers the filter at stream start and flushes the tail in `faar_resampler_finalize()`,
  so the total output frame count matches `ceil(input_frames * out_rate / in_rate)`
  without requiring callers to trim filter delay. Choose Good unless you need that margin;
  applications can use Good by default and High for sources above 48 kHz.
- **Fast to Good** buys about 33 dB more stopband rejection and a flat passband. Fast's
  passband is about 3.4 dB down at 0.96 of the lower Nyquist rate (about 21 kHz for
  44.1 kHz output and 23 kHz for 48 kHz). Fast uses a little less CPU than Good, and
  slightly more on some conversions, so choose it mainly for its small heap and
  lookahead.
- **Setup heap** varies by conversion, up to two to three times the typical figure in
  the worst case, so budget for that on constrained targets.
- **Speed.** All three run at hundreds of times real time on a desktop CPU. Measure the
  conversions you use on your target.

Approximate figures from synthetic probes on seven conversions: 96 to 44.1, 48 to 44.1,
44.1 to 48, 96 to 48, 192 to 44.1, 192 to 48 and 48 to 24 kHz. Quality is the worst case
over passband (to 0.96 of the lower Nyquist rate), stopband, residual and image probes;
they are not listening scores. CPU time is audio duration over processing time, excluding
I/O, and heap is the allocation at stream creation. All of it varies with hardware, build
and release, so treat the ratios as the useful part.

Precision builds change cost, not the preset choice: `-Dresampler-float=true` runs
the whole engine in float (for MCUs without a double FPU), and float High uses
compensated arithmetic and more CPU. See [Build options](#build-options).

## Using the API

Configuration is supplied once, at open. Every fallible call returns a
`faar_status`; frame counts come back through out-parameters. Buffers hold
interleaved floats and counts are frames (samples per channel). Use one handle per
continuous stream. Independent handles may run concurrently; one handle belongs to
one thread at a time. You own all buffers and the handle.

### Calling sequence

1. `faar_config_init(&config, sizeof config)`, then set `in_rate`, `out_rate` and
   `channels`. Defaults are Good quality and no dither.
2. `faar_resampler_open()` validates the config and allocates the handle.
3. Optionally `faar_resampler_get_info()` (set `struct_size`) for the resolved
   settings and filter delay.
4. `faar_resampler_process()` per input chunk. If the output buffer was smaller than
   the result, drain with `in_samples == NULL` and `in_count == 0`.
5. At end of input, call `faar_resampler_finalize()` until it returns `FAAR_OK` with
   zero frames. It emits the filter tail and does not discard history.
6. `faar_resampler_close(&resampler)` frees the handle and sets it to NULL.

| You want | Do this |
|---|---|
| Many streams with identical settings | `faar_design_open()` once, then `faar_resampler_open_from_design()` per stream; see [Reusing a filter design](#reusing-a-filter-design) |
| Real-time or allocation-free streaming | Set `config.max_input_frames` to your largest chunk; see [Bounded streams](#bounded-streams) |

### Configuration

`faar_config_init()` sets the defaults below.

| Field | Default | Meaning |
|---|---|---|
| `in_rate`, `out_rate` | required | Rates in Hz |
| `channels` | required | Interleaved channels, 1 to `faar_library_info.max_channels` |
| `quality` | `FAAR_QUALITY_GOOD` | See [Quality profiles](#quality-profiles) |
| `flags` | 0 | `FAAR_DITHER_*`, see below |
| `decimate_factor` | 0 | 0 plans automatically; 2 or more requests an explicit cascade |
| `max_input_frames` | 0 | 0 grows on demand; nonzero reserves everything at open, see [Bounded streams](#bounded-streams) |

### Resolved info

`faar_resampler_get_info()` reports only decisions the library made:

| Field | Meaning |
|---|---|
| `decimate_factor` | Total decimation of the cascade stages ahead of the final stage; 1 when the plan has none |
| `delay_frames` | Filter lookahead in input frames, all stages; centering and tail flushing are handled internally |

### Functions

```c
/* Fill defaults; pass sizeof(config). Set the rates and channels afterwards. */
faar_status faar_config_init(faar_config *config, uint32_t caller_size);

/* Channel ceiling, flag bits, version, copyright.
 * Set out_info->struct_size before calling. */
faar_status faar_get_library_info(faar_library_info *out_info);
const char *faar_strerror(faar_status status);

faar_status faar_resampler_open(const faar_config *config, faar_resampler **out);
faar_status faar_resampler_close(faar_resampler **resampler); /* sets *resampler = NULL */

/* Resolved settings and filter delay; set out_info->struct_size. */
faar_status faar_resampler_get_info(const faar_resampler *resampler,
                                    faar_resampler_info *out_info);
faar_status faar_resampler_process(faar_resampler *resampler,
        const float *in_samples, size_t in_count, float *out_samples,
        size_t max_out_count, size_t *out_count);
faar_status faar_resampler_finalize(faar_resampler *resampler,
        float *out_samples, size_t max_out_count, size_t *out_count);

/* Optional: share a filter design among identical streams. */
faar_status faar_design_open(const faar_config *config, faar_design **out);
faar_status faar_design_close(faar_design **design);   /* sets *design = NULL */
faar_status faar_resampler_open_from_design(faar_design *design,
                                            faar_resampler **out);
```

**Config details**

- `flags` selects dither: zero, or exactly one of `FAAR_DITHER_TPDF` /
  `FAAR_DITHER_SHAPED` plus exactly one of `FAAR_DITHER_16` / `FAAR_DITHER_24`. Any
  other combination is `FAAR_ERR_INVALID_ARGUMENT`. Dither adds TPDF or shaped noise
  at the chosen target depth for later integer quantization; FAAR itself does not
  convert to integers.
- `decimate_factor` 0 plans automatically; 2 or more requests an explicit cascade.
- `faar_get_library_info()` reports the channel ceiling and flag bits.
  `faar_resampler_get_info()` reports the resolved cascade decimation and filter
  delay. Set each query struct's `struct_size` to its `sizeof` first.
- Closing a NULL pointer is a no-op returning `FAAR_OK`.

### Errors

`FAAR_OK` is 0 and every error is negative. `faar_strerror()` returns a static
string, never NULL.

| Status | Meaning |
|---|---|
| `FAAR_ERR_INVALID_ARGUMENT` | NULL pointer, bad argument or bad configuration |
| `FAAR_ERR_UNSUPPORTED` | Reserved for a configuration a build refuses on purpose; not returned in this release |
| `FAAR_ERR_NO_MEMORY` | Allocation failed, the planner could not build the design, or a bounded stream exhausted its reserved output queue |
| `FAAR_ERR_PROCESSING` | Resampling failed; discard and close that handle |

Open functions set their output pointer to NULL first, so it is always safe to pass
to `faar_resampler_close()`. Process and finalize zero the output frame count on
error.

## Example

Resamples one 96 kHz stereo chunk to 48 kHz.

```c
#include <stdio.h>
#include <faar.h>

int main(void) {
    float input[9600 * 2] = {0};
    float output[4800 * 2] = {0};
    faar_config config;
    if (faar_config_init(&config, sizeof(config)) != FAAR_OK)
        return 1;
    config.in_rate = 96000;
    config.out_rate = 48000;
    config.channels = 2;

    faar_resampler *resampler = NULL;
    faar_status status = faar_resampler_open(&config, &resampler);
    if (status != FAAR_OK) {
        fprintf(stderr, "%s\n", faar_strerror(status));
        return 1;
    }

    size_t written = 0;
    status = faar_resampler_process(resampler, input, 9600, output, 4800,
                                   &written);
    while (status == FAAR_OK && written < 4800) {
        size_t made = 0;
        status = faar_resampler_process(resampler, NULL, 0,
                output + written * 2, 4800 - written, &made);
        written += made;
        if (made == 0) break;
    }
    while (status == FAAR_OK && written < 4800) {
        size_t made = 0;
        status = faar_resampler_finalize(resampler, output + written * 2,
                4800 - written, &made);
        written += made;
        if (made == 0) break;
    }
    if (status != FAAR_OK)
        fprintf(stderr, "%s\n", faar_strerror(status));
    faar_resampler_close(&resampler);
    return status == FAAR_OK ? 0 : 1;
}
```

- The example reserves room for the whole result. With smaller output buffers, drain
  queued output with `faar_resampler_process(NULL, 0)` before supplying more input.
- Total output is `ceil(input_frames * out_rate / in_rate)`.

## Bounded streams

If you know the largest input chunk, set `max_input_frames` before opening:

```c
config.max_input_frames = 1024;
faar_status status = faar_resampler_open(&config, &resampler);
if (status != FAAR_OK) {
    fprintf(stderr, "%s\n", faar_strerror(status));
    /* Handle the error. */
}
```

Zero (the default) grows storage on demand. A nonzero bound reserves all streaming
storage in `faar_resampler_open()`.

- Every input chunk must be at most the bound; a larger one returns
  `FAAR_ERR_INVALID_ARGUMENT` before consuming input.
- Before the next chunk, drain with `faar_resampler_process(NULL, 0)` and nonzero
  output capacity until it produces zero frames. At end of stream, finalize until it
  produces zero frames.
- Process, drain and finalize never allocate or free heap storage.
- If queued output is left undrained until the reserved capacity runs out,
  processing may return `FAAR_ERR_NO_MEMORY` or `FAAR_ERR_PROCESSING`; discard and
  close that stream.
- The reservation covers the cascade, endpoint padding and FFT workspace prefill. It
  raises persistent memory, and open may return `FAAR_ERR_NO_MEMORY` or
  `FAAR_ERR_INVALID_ARGUMENT`. FFT block costs are unchanged.
- This is not a hard real-time guarantee: the OS can still page out memory touched
  during open.
- A design opened with a nonzero `max_input_frames` reserves each stream's storage in
  `faar_resampler_open_from_design()`.

## Reusing a filter design

For several streams with identical settings, create a `faar_design` once and open
each stream from it. The design holds the resolved stage geometry, filter spectra,
polyphase banks and immutable FFT twiddles. Each stream allocates its own histories,
queues and FFT workspace, and streams can run concurrently. `faar_resampler_open()`
remains available for standalone streams.

```c
faar_design *design = NULL;
faar_resampler *left = NULL, *right = NULL;
faar_status status = faar_design_open(&config, &design);
if (status == FAAR_OK)
    status = faar_resampler_open_from_design(design, &left);
if (status == FAAR_OK)
    status = faar_resampler_open_from_design(design, &right);
faar_design_close(&design); /* Open streams retain the immutable design. */
if (status != FAAR_OK) {
    faar_resampler_close(&left);
    faar_resampler_close(&right);
    return status;
}
/* Process/finalize each stream, then close both handles. */
faar_resampler_close(&left);
faar_resampler_close(&right);
```

- A design fixes the whole configuration, including channel count, dither flags and
  `max_input_frames`.
- Creating a design runs the normal designer and temporarily allocates its setup
  workspace. Later stream opens allocate only mutable storage and do not regenerate
  filters. There is no global cache of designs.
- Streams retain the design; it is released after the last stream and your design
  reference are closed. A live reference may be used for concurrent stream opens, but
  do not close it while another thread is using it.
- Without atomic support on the toolchain, serialize design and related stream
  open/close calls. Processing of independent streams can still run concurrently.

## Custom allocators

libfaar allocates through the `AllocMemory` / `FreeMemory` macros in
`libfaar/libfaar_internal.h`. A source build can override both together, for example
`-DAllocMemory=my_alloc -DFreeMemory=my_free` (declare the functions when
compiling), to place state in external RAM; check the speed cost on your target
first.

- The allocator need not clear memory and must return `malloc`-aligned storage;
  libfaar aligns its SIMD buffers to 64 bytes itself.
- `FreeMemory` must accept NULL.
- Default builds grow the two resizable buffers with libc `realloc`. Custom-allocator
  builds allocate, copy and free so growth stays in the custom memory.

### Running without a heap

Use the same `pool_alloc` / `pool_free` pattern as the
[FAAC example](libfaac.md#running-without-a-heap).

- Allocation happens when a resampler is opened.
- With `max_input_frames == 0`, process calls may still grow a buffer. Set a bound if
  the pool must not allocate while streaming; bounded process and finalize calls
  allocate nothing.
- A bump allocator cannot reuse freed blocks, so reserve space for them.

## ABI compatibility

The API and ABI are stable within a major version (SONAME) on a given platform ABI.
Layouts follow the target's pointer width and alignment, so 32- and 64-bit differ,
and the structs are not a serialization format.

- Initialize config with `faar_config_init(&config, sizeof config)`. Initialize each
  output struct with `.struct_size = sizeof(info)` and nothing else.
- Initialization stores `min(caller size, library size)` in `struct_size`. That
  write-back is the support probe: before using a field appended in a newer
  release, check `struct_size >= offsetof(T, field) + sizeof(((T *)0)->field)`.
- Queries write at most the caller's `struct_size` bytes and set `struct_size` to
  the bytes populated. Reset it before reusing a struct as a query destination.
- The library reads only the input bytes `struct_size` covers; absent fields take
  their zero or default value. A `struct_size` below the struct's `FAAR_*_BASELINE`
  macro, including zero, returns `FAAR_ERR_INVALID_ARGUMENT`, as do unknown flag bits
  and out-of-range enum values.
- Tolerate unknown enum values, flag bits and status codes in anything returned.
  `FAAR_OK` is 0; `FAAR_ERR_INVALID_ARGUMENT`, `FAAR_ERR_UNSUPPORTED` and
  `FAAR_ERR_NO_MEMORY` are -1, -2 and -3; higher codes are specific to libfaar.
- Fixed-width integers and width-pinned enums keep layouts stable under
  `-fshort-enums`. `size_t` appears only in signatures and follows the platform ABI.
- `FAAR_VERSION_*` is the project release version that every library reports; the
  SONAME is versioned separately, and `faar_get_library_info()` reports the linked
  version.

FAAR is free software, licensed under the GNU Lesser General Public License
(LGPL), version 2.1 or later.

Copyright © 2026, Nils Schimmelmann
