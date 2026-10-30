# FAAR - Resampler Library

## Contents

- [Scope](#scope)
- [Interface description](#interface-description)
- [Building and linking](#building-and-linking)
  - [Build options](#build-options)
- [Resampling model](#resampling-model)
- [Quality profiles](#quality-profiles)
- [API (faar.h)](#api-faarh)
  - [Calling sequence](#calling-sequence)
  - [Error handling](#error-handling)
  - [Function reference](#function-reference)
  - [Complete streaming example](#complete-streaming-example)
  - [Bounded streams](#bounded-streams)
  - [Reusing a filter design](#reusing-a-filter-design)
  - [Custom allocators](#custom-allocators)
- [ABI compatibility](#abi-compatibility)

## Scope

FAAR converts interleaved PCM audio between sample rates. The application
handles file and container I/O, timestamps, and playback. The library accepts
and returns 32-bit float PCM at unity full scale: `-1.0` to `1.0`. It does not
read or write audio files.

## Interface description

Include `<faar.h>` and link against `libfaar` (`-lfaar`). Installed builds
provide the `faar` pkg-config module.

The shared library is `libfaar` (`libfaar.so.0` on Linux, `libfaar.0.dylib` on
macOS, or `libfaar.dll` on Windows). The default build also produces a static
archive. The `faar` command-line tool in `frontend/` is an example of using the
API and is documented in `faar(1)`.

## Building and linking

Build and install the library with Meson:

```sh
meson setup build-faar -Dencoder=false -Ddecoder=false -Dfrontend=false
meson compile -C build-faar
meson install -C build-faar
```

Omit `-Dfrontend=false` to build the `faar` command-line tool; it is built
when both `-Dfrontend` and `-Dresampler` are enabled (the default).

Compile an application against an installed library with:

```sh
cc -std=c11 -Wall -Wextra app.c -o app $(pkg-config --cflags --libs faar)
```

For a custom prefix, add its pkg-config directory to `PKG_CONFIG_PATH` and
configure the platform runtime library path for shared linking. Static linking
uses `pkg-config --cflags --libs --static faar` and the toolchain's archive
selection options.

### Build options

The default precision policy is mixed. `-Dresampler-float=true` selects float arithmetic and supports Fast, Good, and
High quality. Float High uses compensated float arithmetic and more CPU.
Mixed is the float engine with High's filter kernels in native double: its
output matches float at Fast/Good and double arithmetic at High. Use `resampler-float`
for microcontrollers without a double-precision FPU. Filter
coefficients and FFT twiddles still use double-precision setup calculations.
The channel limit is controlled by `-Dmax-channels` (default 8, range 1–8).
The FFT engine keeps no static tables: each handle allocates its twiddles at
setup. Float High additionally allocates expansion FFT workspaces per handle;
channels within the handle share immutable twiddles and filter spectra.

## Resampling model

The planner selects a filter based on the rates and quality preset. Polyphase
FIR filters use Kaiser windows and interpolation between stored phases. Some
conversions use overlap-save DFT FIR stages; large downsampling ratios can use
cascaded stages. `faar_resampler_get_info()` reports filter lookahead in input
frames, including cascaded stages. FFT twiddles, work buffers and scratch are
private to each handle, so
concurrent independent handles share no mutable state.

Within each multichannel prefilter, channel stages share one immutable filter
spectrum (or sparse halfband coefficient set). Their overlap histories and
streaming queues remain separate. The prefilter owns the shared
coefficients and releases them when it closes; this adds no global cache or
public API requirement.

Within a handle, serialized channel pairs also share FFT workspace storage.
Their overlap histories and streaming queues remain separate. This sharing
assumes the normal per-handle rule that calls are serialized; independent
handles keep separate workspaces.

Staged routes with a downstream polyphase bank drain prefilter output directly
into the bank's channel histories, avoiding a separate intermediate sample
buffer and copy. A bounded stream reserves those histories in open.

## Quality profiles

Fast reduces processing cost with a narrower passband and weaker alias
rejection. Good is the default profile, with a wide passband and strong alias
suppression. High keeps Good's passband while further reducing aliasing, at
greater CPU and setup-memory cost. Float builds support High using
compensated float arithmetic. Filter coefficients and FFT twiddles still use
the existing double-precision setup calculations. These profiles describe
different filter and resource tradeoffs, so the best choice depends on the
source material and the application's CPU and memory budget.

## API (faar.h)

Supply configuration once, when opening the resampler. Every fallible call
returns a `faar_status`; frame counts are returned through out-parameters. Create
one handle for each continuous stream. Independent handles may be used
concurrently; a single handle belongs to one thread at a time. Buffers contain
interleaved float samples; counts are frames (samples per channel). The caller
owns all buffers and the handle.

### Calling sequence

1. Initialize a `faar_config` with `faar_config_init(&config, sizeof(config))`,
   then set `in_rate`, `out_rate` and `channels`. Defaults are Good quality
   and no dither.
2. Call `faar_resampler_open()`. It validates the configuration and allocates
   the handle.
3. Optionally call `faar_resampler_get_info()` with `struct_size` set to
   `sizeof(info)` for the resolved settings and the filter delay.
4. Call `faar_resampler_process()` for each chunk of input. Drain queued output
   with `in_samples == NULL` and `in_count == 0` whenever the output buffer
   was smaller than the result.
5. At end of input, call `faar_resampler_finalize()` until it returns `FAAR_OK`
   with zero frames. It emits the filter tail; it does not discard history.
6. Call `faar_resampler_close(&resampler)`. It frees the handle and sets the
   pointer to `NULL`.

Two refinements are optional and need no extra calls in the sequence above:

| You want | Do this |
|---|---|
| One stream | Nothing extra: the steps above. |
| Many streams with identical settings | Create a `faar_design` once with `faar_design_open()` and open each stream with `faar_resampler_open_from_design()`; see [Reusing a filter design](#reusing-a-filter-design). |
| Real-time or allocation-free streaming | Set `config.max_input_frames` to your largest input chunk before opening; see [Bounded streams](#bounded-streams). |

### Error handling

`FAAR_OK` is 0; all error codes are negative. `faar_strerror()` maps a status to
a static, human-readable string and never returns NULL.

| Status | Meaning |
|---|---|
| `FAAR_ERR_INVALID_ARGUMENT` | NULL pointer, bad argument or bad configuration. |
| `FAAR_ERR_UNSUPPORTED` | Reserved for a configuration a build refuses on purpose; this release does not return it. |
| `FAAR_ERR_NO_MEMORY` | An allocation failed, the planner could not build the filter design, or a bounded stream exhausted its reserved output queue. |
| `FAAR_ERR_PROCESSING` | Resampling failed. Discard and close that handle. |

Open functions set their output pointer to `NULL` before anything else, so it is
always safe to pass to `faar_resampler_close()`. Process and finalize set the
output frame count to zero when they return an error.

### Function reference

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

`faar_config_init()` zeroes the structure and selects Good quality and no
dither. Dither is selected with `flags`: zero, or
exactly one of `FAAR_DITHER_TPDF`/`FAAR_DITHER_SHAPED` plus exactly one of
`FAAR_DITHER_16`/`FAAR_DITHER_24`; any other combination is
`FAAR_ERR_INVALID_ARGUMENT`. Dither adds TPDF or shaped noise (plus the chosen
target depth) for later integer quantization; FAAR does not convert samples to
integers. Set `decimate_factor` to zero for automatic planning or to a value of
two or greater to request an explicit cascade.
`faar_get_library_info()` reports the compiled channel ceiling and flag bits;
`faar_resampler_get_info()` reports the resolved cascade decimation and the filter delay. Initialize each
query structure's `struct_size` to `sizeof(structure)` first. Closing frees the
handle and sets the caller's pointer to NULL; closing an already-NULL pointer is
a no-op that returns `FAAR_OK`.

### Complete streaming example

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

The example reserves enough output space for the complete result. For
streaming with smaller output buffers, retain and drain queued output by calling
`faar_resampler_process()` with `in_samples == NULL` and `in_count == 0` before
supplying more input. At end of input, call `faar_resampler_finalize()` until it
returns `FAAR_OK` with zero frames. The total output is
`ceil(input_frames * out_rate / in_rate)`.

### Bounded streams

For streams with a known maximum input chunk, set `max_input_frames` in the
configuration before opening:

```c
config.max_input_frames = 1024;
faar_status status = faar_resampler_open(&config, &resampler);
if (status != FAAR_OK) {
    fprintf(stderr, "%s\n", faar_strerror(status));
    /* Handle the error. */
}
```

Zero, the default, grows storage on demand. A nonzero bound reserves all
streaming storage inside `faar_resampler_open()`. Every input chunk must then
be at most the bound. Drain with `faar_resampler_process()` using
`in_samples == NULL`, `in_count == 0`, and nonzero output capacity until it
produces zero frames before supplying the next chunk; finalize with
`faar_resampler_finalize()` until it produces zero frames at end of stream.
Process, drain, and finalize calls do not allocate or free heap storage.
Exceeding the bound returns `FAAR_ERR_INVALID_ARGUMENT` before consuming input.
If queued output is not drained and its reserved capacity is exhausted,
processing may return `FAAR_ERR_NO_MEMORY` or `FAAR_ERR_PROCESSING`;
discard and close that stream.

The reservation covers the cascade, including endpoint padding and FFT
workspace prefill; it can increase persistent memory, and `open` may return
`FAAR_ERR_NO_MEMORY` or `FAAR_ERR_INVALID_ARGUMENT`. Pages initialized
during open can still be paged out later by the operating system, so this is
not a hard real-time guarantee. FFT block costs are unchanged. A design opened
with a nonzero `max_input_frames` reserves each stream's storage in
`faar_resampler_open_from_design()`.

### Reusing a filter design

For multiple streams with identical settings, create a `faar_design` once and
open each stream with `faar_resampler_open_from_design()`. The design stores the
resolved stage geometry, filter spectra, polyphase banks and immutable FFT
twiddles. Each stream allocates separate histories, queues and FFT workspace;
streams can be processed concurrently. Ordinary `faar_resampler_open()` remains
available for standalone streams.

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

A design fixes the complete configuration, including channel count, dither
flags and `max_input_frames`.
Design creation uses the normal designer and temporarily allocates its setup
workspace, then releases that workspace. Subsequent stream opens allocate
mutable storage without regenerating filters. There is no global cache of
filter designs.

Streams retain the design automatically; the design is released after the last
stream and the caller's design reference are closed. A live design reference may
be used for concurrent stream opens. As with resampler handles, do not close
the same caller reference while another thread is using it. On toolchains
without atomic support, serialize design and associated stream open/close
operations; independent stream processing can still run concurrently.

### Custom allocators

libfaar allocates through the `AllocMemory` / `FreeMemory` macros in
`libfaar/libfaar_internal.h`. A source build can override both together, for
example with `-DAllocMemory=my_alloc -DFreeMemory=my_free` (provide the
function declarations when compiling), to place resampler state in external
RAM. The allocator need not clear the memory it returns and must return
storage aligned as `malloc` does; libfaar aligns its SIMD buffers to 64 bytes
internally. `FreeMemory` must accept NULL. Default builds grow the two
resizable buffers with libc `realloc`; custom-allocator builds allocate, copy
and free so growth stays in the custom allocator's memory.

Check the speed cost of external-RAM placement on your target before
committing to it.

#### Running without a heap

A fixed-pool override can avoid the system heap, with the same
`pool_alloc` / `pool_free` pattern as the
[FAAC example](libfaac.md#running-without-a-heap). Allocation occurs when a
resampler is opened. Process calls on a resampler opened with
`max_input_frames == 0` may still grow a buffer, so set a bound if the pool must
not allocate during streaming, and reserve space for freed blocks because a bump
allocator cannot reuse them. Bounded process and finalize calls allocate nothing.

## ABI compatibility

The API and ABI are stable within a major version / SONAME on a given platform
ABI. Layouts follow the target's pointer width and alignment; they differ between
32- and 64-bit targets and are not a serialization format.

**Caller rules**

- Initialize configuration with `faar_config_init(&config, sizeof config)`. Initialize each output struct
  by setting `.struct_size = sizeof(info)` and nothing else.
- Initialization stores `min(caller size, library size)` in `struct_size`. That
  write-back is the support probe: before using a field appended in a newer
  release, check that `struct_size` covers it
  (`struct_size >= offsetof(T, field) + sizeof(((T *)0)->field)`).
- Query functions write at most the caller's `struct_size` bytes and set
  `struct_size` to the bytes populated. Reset it before reusing a struct as a
  query destination.
- The library reads only the input bytes `struct_size` covers; absent fields take
  their zero or default value. A `struct_size` below the struct's `FAAR_*_BASELINE`
  macro, including zero, returns `FAAR_ERR_INVALID_ARGUMENT`.
  Unknown flag bits and out-of-range enum values are rejected the same way.
- Tolerate unknown enum values, flag bits and status codes in anything the
  library returns.
  `FAAR_OK` is 0 and `FAAR_ERR_INVALID_ARGUMENT`, `FAAR_ERR_UNSUPPORTED` and
  `FAAR_ERR_NO_MEMORY` are -1, -2 and -3; higher codes are specific to libfaar.
- Fixed-width integers and width-pinned enums keep layouts stable under
  `-fshort-enums`. `size_t` appears in signatures only and follows the platform ABI.

**Maintainer rules**

- New fields are appended at or after the previous `sizeof(struct)`, never in its
  tail padding. In a pointer-free struct with 4-byte alignment (`faar_config`, `faar_resampler_info`) append only
  members of 4 bytes or less, so alignment stays 4. Where a struct holds a pointer
  or 64-bit member, place each appended member at an offset that is a multiple of
  its size, add an explicit `uint32_t` first if needed, and keep each appended
  block a multiple of 8 bytes.
- Zero in a new input field keeps the previous behaviour.
- Baseline sizes (`FAAR_*_BASELINE` macros), released offsets, enum values, status codes
  and signatures stay fixed within a major version; incompatible changes need a
  new SONAME.
- `FAAR_VERSION_*` is the project release version, which every library reports;
  the SONAME is versioned separately. `faar_get_library_info()` reports the linked version.

## License

FAAR is free software, licensed under the GNU Lesser General Public License
(LGPL), version 2.1 or later.

Copyright © 2026, Nils Schimmelmann
