# FAAR verification

The library tests live in this directory and are registered by its local
`meson.build`. They cover the public streaming API, signal quality controls,
rate-matrix invariants, DFT filtering and shared spectra, prefilter setup, and
the real FFT.
`frontend/tests/test_faar_cli.py` covers command-line and file-format behavior.
The public API is also exercised by `tests/fuzz/fuzz_faar.c` through the
repository fuzz runner.

Run the production checks from a configured build directory:

```sh
meson test -C build
```

The fuzz target can be built and run with:

```sh
tests/fuzz/run.sh /tmp/faac-fuzz 10 fuzz_faar
```

## Optional benchmark

`bench_faar` is an opt-in, POSIX-only timing tool; it is not part of the
correctness test suite. Build and run it with:

```sh
meson compile -C build bench_faar
build/libfaar/tests/bench_faar 48000 44100 2 2 1024 17
```

The six arguments are input rate, output rate, channel count, quality,
input block size, and output capacity. It prints one CSV row, in this column
order:

```text
in_rate,out_rate,channels,quality,input_block,output_capacity,setup_ms,input_Mframes_per_sec,peak_process_RSS_KiB,filter_delay_input_frames,first_output_input_frames
```

Each run averages three separate four-second streams. `setup_ms` is the mean
open time; setup is excluded from throughput. Peak RSS is the whole process's
high-water resident memory, not an estimate of library allocation. The first
output measurement depends on chunk and block scheduling; filter delay is a
separate filter property and is not callback latency.

For comparisons, run each configuration repeatedly in isolation, without
running tests at the same time, and compare the median. Check correctness
before comparing speed.

`bench_faar_open` is a separate POSIX-only setup and memory benchmark. It
keeps all opened handles alive, processes no PCM, and prints one CSV row:

```sh
meson compile -C build bench_faar_open
build/libfaar/tests/bench_faar_open 48000 44100 2 2 8
```

The arguments are input rate, output rate, channel count, quality, and handle
count. The columns are:

```text
in_rate,out_rate,channels,quality,handles,first_open_ms,mean_subsequent_open_ms,peak_process_RSS_KiB
```

The first open is the cold setup in that process; subsequent opens reuse
process-initialized tables while keeping all earlier handles live. With one
handle, `mean_subsequent_open_ms` is zero. Peak RSS is the whole process's
high-water mark, not live library allocation. Open only the handles required
for the measurement; the arrays and filter work buffers contribute to process
memory too.

`bench_faar_heap` measures requested libfaar heap with allocator hooks, so it
excludes process RSS noise and allocator metadata. Run it with:

```sh
meson compile -C build bench_faar_heap
build/libfaar/tests/bench_faar_heap 48000 44100 2 1 8192
```

The arguments are input rate, output rate, channels, quality, and frames to
reserve with `faar_resampler_prepare()` (`0` skips preparation). The CSV fields
are input rate, output rate, channels, quality, prepared frames, open live
bytes, open peak bytes, post-prepare live bytes, peak bytes including prepare,
and live bytes after close. Caller buffers, allocator bookkeeping, and static
BSS are excluded; the close field should be zero.

`bench_precision.py` compares float and mixed builds on the same host.
Configure separate release builds, then compile both optional benchmark targets
in each build:

```sh
meson setup build-faar-float -Dencoder=false -Ddecoder=false -Dfrontend=false \
  -Dbuildtype=release -Db_lto=true -Dfaar-precision=float
meson setup build-faar-mixed -Dencoder=false -Ddecoder=false -Dfrontend=false \
  -Dbuildtype=release -Db_lto=true -Dfaar-precision=mixed
for build in build-faar-float build-faar-mixed; do
  meson compile -C "$build" bench_faar bench_faar_heap
done
python3 libfaar/tests/bench_precision.py
```

Run this on a native target machine for architecture-specific throughput. The
script rotates build order and reports median throughput for each quality,
channel count, and 48/96/192 kHz input route; it also reports prepared peak
requested heap. Before measuring, it checks that each Meson build has the
expected precision and LTO enabled. It fails if a heap benchmark leaves
tracked bytes live. Use `--repeats` to change the number of timing samples.
Timing output is
input-Mframes/s, so larger is faster; setup and heap are separate measurements.
The `FAAR precision benchmark (GCC 13 amd64)` CI job runs this same comparison
on a native x86_64 runner and uploads the CSV together with compiler and host
information.

`bench_faar_latency` is an optional POSIX-only native API-call timing probe.
Build and run it with:

```sh
meson compile -C build bench_faar_latency
build/libfaar/tests/bench_faar_latency 48000 44100 8 2 64 17 1
```

The arguments are input rate, output rate, channels, quality, input block,
output capacity, and prepared mode (`0` or `1`). The CSV columns are:

```text
in_rate,out_rate,channels,quality,input_block,output_capacity,prepared,prepare_ms,process_call_count,callback_p50_us,callback_p99_us,callback_max_us,flush_max_us,peak_process_RSS_KiB
```

It times each public `process()` input and drain call; flush calls are timed
separately. It allocates timing storage before streaming and uses no allocator
hooks, so these are native API-call durations rather than instrumented
allocation timings. `prepare_ms` excludes open time. The observed maximum is
not an end-to-end host callback measurement or a real-time deadline guarantee;
FFT block costs and their bursts remain. See
[`preparation-results.md`](preparation-results.md) for the final comparison.

`test_faar_prepare` checks the no-allocation contract by failing all library
allocations after preparation and verifying that prepared processing and
flushing make zero allocation and free calls. It contrasts this with unprepared
allocation snapshots (0–19 allocation calls in the tested cases), injects
failures at each preparation allocation point and checks recovery, verifies a
failed reprepare retains the old usable bound, and exercises undrained queue
budget exhaustion. In `compare_quality.py`, `--prepare` prepares the candidate
for 8192 input frames and drains both reference and candidate between chunks;
the baseline library does not need to export the new prepare symbol:

```sh
python3 libfaar/tests/compare_quality.py /path/to/baseline/libfaar.dylib \
  /path/to/candidate/libfaar.dylib --report /tmp/prepared-quality.json --prepare
```

The `test_faar_fft_threads` test races first initialization for several table
sizes, checks that FFT work buffers remain private, then clears and reopens
plans and exercises the dynamic larger-size fallback. To run it under
ThreadSanitizer in a separate build:

```sh
meson setup build-tsan -Dencoder=false -Ddecoder=false -Dfrontend=false \
  -Db_sanitize=thread -Db_lto=false -Db_lundef=false
meson test -C build-tsan test_faar_fft_threads
```

`test_faar_dft_shared` checks per-channel sharing of immutable DFT spectra and
halfband coefficients within one prefilter, while history, queues, and FFT
work remain private to each channel. It also checks cleanup ordering for the
shared stages.

## Quality regression comparison

`compare_quality.py` requires Python and NumPy. It loads two separately built
shared libraries through the public API, with the baseline first and candidate
second, and writes a JSON report:

```sh
python3 libfaar/tests/compare_quality.py /path/to/baseline/libfaar.dylib \
  /path/to/candidate/libfaar.dylib --report /tmp/quality-report.json
```

The default quality IDs are `0 1 2`; both float and mixed builds support all
three. Use `--qualities 0 1` when comparing float Fast/Good against a baseline
that predates compensated High. The strict bit comparison is intended for
unchanged arithmetic, rather than comparisons across float and mixed builds.
The `high_stopband` Meson group independently checks High's 150 dB rejection
with one, two, three, and eight channels, including loud neighbouring channels.
`test_faar_float2` compares the compensated FFT against the double FFT and
checks inverse round trips, shared twiddles, and phase arithmetic.

The comparison is a strict baseline regression gate: any changed float32
sample bit fails, including signed-zero changes. Incorrect output counts,
non-finite samples, or a non-terminating flush also fail. The pure 23 kHz
test additionally requires at least 100/130/150 dB rejection for
Fast/Good/High. Its independently generated category coverage includes a
20-second sweep; 30-second 23 kHz tone, tone-plus-noise, and two-tone signals;
impulse positions covering every rational phase; independently processed
track edges; and a route matrix with 1, 2, 3, and 8 channels, both boundary
modes, dither modes, and queued scheduling. The signal categories are informed
by [Hydrogenaudio technical material](https://src.hydrogenaudio.org/technical)
and [dBpoweramp's sample-rate conversion discussion](https://www.dbpoweramp.com/sample_rate_conversion).

This is not a reproduction or certification of either site's plots or scores.
A pass means the candidate matches the chosen baseline for these cases; it does
not prove that the baseline meets an absolute fidelity specification or that
all possible inputs are covered. The existing Meson passband, alias-rejection,
and other quality tests remain the absolute quality gate.

See [the measured history-buffering results](optimization-results.md) for the
2026-10-24 baseline comparison and its limits.

See [the static FFT-table measurements](static-tables-results.md) for repeated
open and throughput comparisons of the lazy shared tables.

See [the shared-spectrum measurements](shared-spectrum-results.md) for the
per-channel sharing results and test coverage.

See [the preparation measurements](preparation-results.md) for allocator
checks and native API-call timing results.

See [the compensated float High results](float-high-results.md) for quality,
throughput, and requested heap measurements.
