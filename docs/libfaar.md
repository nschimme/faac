# FAAR resampler library

## Contents

- [Scope](#scope)
- [Building and linking](#building-and-linking)
- [Resampling model](#resampling-model)
- [Quality profiles](#quality-profiles)
- [API](#api)
  - [Complete streaming example](#complete-streaming-example)
  - [Preparing bounded streams](#preparing-bounded-streams)
  - [Function reference](#function-reference)
- [ABI compatibility](#abi-compatibility)
- [Command-line tool](#command-line-tool)

## Scope

FAAR converts interleaved PCM audio between sample rates. The application
handles file and container I/O, timestamps, and playback. The library accepts
and returns 32-bit float PCM on the FAAC integer scale: `-32768.0` to
`32768.0`. It does not read or write audio files.

## Building and linking

Include `<faar.h>` and link against `libfaar`. Installed builds provide the
`faar` pkg-config module. Build and install with Meson:

```sh
meson setup build-faar -Dencoder=false -Ddecoder=false -Dfrontend=false
meson compile -C build-faar
meson install -C build-faar
```

Omit `-Dfrontend=false` to build the `faar` command-line tool; it is built
when both `-Dfrontend` and `-Dresampler` are enabled (the default). The default
precision policy is mixed.
`-Dfaar-precision=float` selects float arithmetic and supports Fast, Good, and
High quality. Float High uses compensated float arithmetic and more CPU.
Mixed is the float engine with High's filter kernels in native double: its
output matches float at Fast/Good and double arithmetic at High. Use `float`
for microcontrollers without a double-precision FPU. Filter
coefficients and FFT twiddles still use double-precision setup calculations.
The channel limit is controlled by `-Dmax-channels` (default 8, range 1–8).
The FFT engine lazily initializes shared twiddle tables for common complex
transform sizes from 16 through 65,536. Their bounded static BSS reservation
is about 1 MiB in float builds or 3 MiB in mixed builds; only tables for used
sizes are initialized. On hosted systems this is demand-zero virtual storage,
and only touched pages become resident; on bare-metal targets the linker
reserves the full BSS in RAM. Set `-Dfaar-twiddle-cache-bits=N` to cache only
complex sizes through `2^N` (default 16), or use `0` to disable static tables.
Larger sizes allocate twiddles per plan during setup; this trades more setup
work and heap allocation for a smaller static RAM reservation. The transform
arithmetic is unchanged. The distinction between reserved and resident memory
is important for the mixed build, which reserves both table sets. Float High
additionally allocates expansion FFT workspaces and twiddles per handle;
channels within the handle share immutable twiddles and filter spectra. See the
[float High measurements](../libfaar/tests/float-high-results.md) for measured
quality, CPU, and heap tradeoffs.

The current float/mixed tradeoffs and precision-policy measurements are
recorded in [precision-policy-results.md](../libfaar/tests/precision-policy-results.md).

Compile an application against an installed library with:

```sh
cc -std=c11 -Wall -Wextra app.c -o app $(pkg-config --cflags --libs faar)
```

For a custom prefix, add its pkg-config directory to `PKG_CONFIG_PATH` and
configure the platform runtime library path for shared linking. Static linking
uses `pkg-config --cflags --libs --static faar` and the toolchain's archive
selection options.

## Resampling model

The planner selects a filter based on the rates and quality preset. Polyphase
FIR filters use Kaiser windows and interpolation between stored phases. Some
conversions use overlap-save DFT FIR stages; large downsampling ratios can use
cascaded stages. `faar_resampler_get_info()` reports filter lookahead in input
frames, including cascaded stages. Common complex FFT sizes share immutable
twiddle tables initialized once per process on first use; unused size tables
remain uninitialized. This one-time initialization is protected when the
toolchain provides atomics, so concurrent independent handles remain safe under
the normal API threading rules. On toolchains without atomic support, serialize
concurrent opens as required by the codec libraries. FFT work buffers and
scratch remain private to each handle. Sizes above 65,536 continue to allocate
twiddles per plan, and static tables remain available until library unload or
process exit.

Within each multichannel prefilter, channel stages share one immutable filter
spectrum (or sparse halfband coefficient set). Their overlap histories and
streaming queues remain separate. The prefilter owns the shared
coefficients and releases them when it closes; this adds no global cache or
public API requirement.

Within a handle, serialized channel pairs also share FFT workspace storage.
Their overlap histories and streaming queues remain separate. This sharing
assumes the normal per-handle rule that calls are serialized; independent
handles keep separate workspaces.

## Quality profiles

Fast reduces processing cost with a narrower passband and weaker alias
rejection. Good is the default profile, with a wide passband and strong alias
suppression. High keeps Good's passband while further reducing aliasing, at
greater CPU and setup-memory cost. Float builds support High using
compensated float arithmetic. Filter coefficients and FFT twiddles still use
the existing double-precision setup calculations. These profiles describe
different filter and resource tradeoffs, so the best choice depends on the
source material and the application's CPU and memory budget.

## API

The status-returning `faar_resampler_*` API is the public interface. Create
one handle for each continuous stream. Independent handles may be used
concurrently; a single handle belongs to one thread at a time. Buffers contain
interleaved float samples; counts are frames (samples per channel). The caller
owns all buffers and the handle.

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
        status = faar_resampler_flush(resampler, output + written * 2,
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
supplying more input. At end of input, call `faar_resampler_flush()` until it
returns `FAAR_OK` with zero frames. The total output is
`ceil(input_frames * out_rate / in_rate)`.

### Preparing bounded streams

For streams with a known maximum input chunk, prepare storage after opening and
before processing or flushing:

```c
faar_status status = faar_resampler_prepare(resampler, 1024);
if (status != FAAR_OK) {
    fprintf(stderr, "%s\n", faar_strerror(status));
    /* Handle the error. */
}
```

The bound must be nonzero. It may be changed by calling prepare again before
any input; a failed call leaves the prior prepared mode and bound usable,
though it may retain larger allocations. After successful preparation, every
input chunk must be at most the bound. Drain with `faar_resampler_process()`
using `in_samples == NULL`, `in_count == 0`, and nonzero output capacity until
it produces zero frames before supplying the next chunk; flush until it
produces zero frames at end of stream. Prepared process, drain, and flush calls
do not allocate or free heap storage. Exceeding the bound returns
`FAAR_ERR_INVALID_ARGUMENT` before consuming input. If queued output is not
drained and its reserved capacity is exhausted, processing may return
`FAAR_ERR_NO_MEMORY` or `FAAR_ERR_PROCESSING`; discard and close that stream.

Preparation reserves storage across the cascade, including endpoint padding
and FFT workspace prefill; it can increase persistent memory and may return
`FAAR_ERR_NO_MEMORY` or `FAAR_ERR_INVALID_ARGUMENT`. Pages initialized during
preparation can still be paged out later by the operating system, so this is
not a hard real-time guarantee. FFT block costs are unchanged. Streams that do
not need a fixed input bound can continue using the regular open/process/flush
flow without preparing.

### Function reference

| Function | Purpose |
|---|---|
| `faar_config_init()` | Initializes a versioned configuration with default quality, boundary, and dither settings. |
| `faar_get_library_info()` | Reports the build's channel ceiling, supported quality mask, version, and copyright. |
| `faar_strerror()` | Returns a static message for a `faar_status` value. |
| `faar_resampler_open()` | Validates the configuration and creates a resampler handle. |
| `faar_resampler_prepare()` | Reserves streaming storage for a maximum input chunk before processing begins. |
| `faar_resampler_get_info()` | Reports the settings resolved for an open handle, including filter delay. |
| `faar_resampler_process()` | Consumes input frames or drains queued output and reports produced frames. |
| `faar_resampler_flush()` | Produces the remaining filter tail at end of stream. |
| `faar_resampler_close()` | Releases a handle and sets the caller's pointer to `NULL`. |

Every fallible function returns `FAAR_OK` on success or a negative
`faar_status` on error. Process and flush set the output frame count to zero
when they return an error; after a processing error, discard and close that
stream.

`faar_config_init()` zeroes the structure and selects Good quality, HOLD
boundary extension, and no dither. Set the rates and channel count before
opening. When dither is enabled, `dither_bits` must be 16 or 24; it is ignored
otherwise. `reserved` and `reserved1` must be zero or open fails with
`FAAR_ERR_INVALID_ARGUMENT`. `faar_get_library_info()` reports the compiled
channel ceiling and quality mask; `faar_resampler_get_info()` reports resolved
settings and delay. Initialize each query structure's `struct_size` to
`sizeof(structure)` first.

Dither adds TPDF or shaped noise for later 16-bit or 24-bit integer
quantization; FAAR does not convert samples to integers. Boundary and dither
settings are selected in `faar_config` before opening. Set `decimate_factor`
to zero for automatic planning or to a value of two or greater to request an
explicit cascade. Closing frees the handle and sets the caller's pointer to
NULL; closing an already-NULL pointer is a no-op that returns `FAAR_OK`.

## ABI compatibility

The shared library uses SONAME 3. The API and ABI are stable within that major
SONAME on a given platform ABI. Fixed-width types are used for configuration;
`size_t` and pointers follow the target platform ABI. Enum sentinels pin enum
storage to at least 32 bits, including with `-fshort-enums`. Public structures
grow by appending fields; initialize configurations with
`faar_config_init(&config, sizeof(config))` and set `struct_size` on query
structures. Reserved fields remain zero and are never reused. The structure
size passed to `faar_config_init()` must cover the current baseline fields.
`faar_resampler_prepare()` is an additive function; it does not change public
structure layouts or the SONAME.

Query functions write back the number of bytes populated in `struct_size`.
Reset `struct_size` and zero your storage before reusing a structure as a query
destination, and check the returned size before reading an appended field when
supporting older libraries. The library accepts its frozen baseline sizes,
reads only the configuration bytes the caller's `struct_size` covers, and
writes at most `struct_size` bytes to query structures.

New fields are appended at or after the previous `sizeof(struct)`; compiler
padding is never reused. Explicit `reserved` fields must be zero on input and
are zero on output until they gain a meaning. Layouts follow the platform's
pointer width and alignment and are not serialization formats.

Incompatible signatures or type changes require a new SONAME. `FAAR_VERSION_MAJOR`
matches the library SONAME major; minor and patch macros track the project
release. `faar_get_library_info()` reports the linked version.

## Command-line tool

`faar` reads WAV input automatically or raw PCM from a file. Use `--raw` when
the input is raw PCM on stdin, so format detection does not consume stream
data. Named output is WAV when the input is WAV or the output path ends in
`.wav`; stdout always carries raw PCM. See [`faar(1)`](faar.1) or run
`faar --help` for options and examples. `-h` shows the short option summary;
`-H` adds detailed descriptions for every option.

## License

FAAR is free software, licensed under the GNU Lesser General Public License
(LGPL), version 2.1 or later.

Copyright © 2026, Nils Schimmelmann
