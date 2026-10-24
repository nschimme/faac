# FAAR verification

The library tests live in this directory and are registered by its local
`meson.build`. They cover the public streaming API, signal quality controls,
rate-matrix invariants, DFT filtering, prefilter setup, and the real FFT.
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

The `test_faar_fft_threads` test races first initialization for several table
sizes, checks that FFT work buffers remain private, then clears and reopens
plans and exercises the dynamic larger-size fallback. To run it under
ThreadSanitizer in a separate build:

```sh
meson setup build-tsan -Dencoder=false -Ddecoder=false -Dfrontend=false \
  -Db_sanitize=thread -Db_lto=false -Db_lundef=false
meson test -C build-tsan test_faar_fft_threads
```

## Quality regression comparison

`compare_quality.py` requires Python and NumPy. It loads two separately built
shared libraries through the public API, with the baseline first and candidate
second, and writes a JSON report:

```sh
python3 libfaar/tests/compare_quality.py /path/to/baseline/libfaar.dylib \
  /path/to/candidate/libfaar.dylib --report /tmp/quality-report.json
```

The default quality IDs are `0 1 2`. For float builds, select the supported
qualities explicitly, for example `--qualities 0 1`.

The comparison is a strict baseline regression gate: any changed float32
sample bit fails, including signed-zero changes. Incorrect output counts,
non-finite samples, or a non-terminating flush also fail. The pure 23 kHz
test additionally requires at least 100/130/150 dB rejection for
Fast/Good/High. Its independently generated category coverage includes a
20-second sweep; 30-second 23 kHz tone, tone-plus-noise, and two-tone signals;
impulse positions covering every rational phase; independently processed
track edges; and a route matrix with 1, 2, and 8 channels, both boundary
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
