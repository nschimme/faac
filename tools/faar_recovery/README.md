# FAAR recovery diagnostics

These tools provide independent common-rate benchmarking and quality diagnostics for FAAR. The production Meson default is `staged-short`; planned common routes use its DFT-plus-phase pipeline, while unplanned pairs retain a compatibility fallback. Staged-short is the only production build. The checked-in current snapshot covers 16 common conversions and records performance, passband, transient, and image probes.

## Build

The historical `build.py` FAAR reconstruction, `stage_timers.py`, and
`fft_bench.cpp` require the pre-cleanup source checkout at `70caa24f`. They
intentionally retain its old FFT and backend names to reproduce the archived
measurements; they cannot rebuild those variants from current sources.
The `--external-only` build and diagnostics against supplied libraries remain
available in this checkout.

Requirements: GCC/G++, CMake, Ninja, Python + NumPy, Valgrind, GNU binutils, and
local pinned SoX/KBSRC checkouts. The ARDFTSRC source adapter is included here;
its external AudioFile, Eigen, and FFTW headers/libraries must be supplied.
The defaults reuse the existing `/tmp` reference checkouts and extracted Debian
FFTW/Eigen development files. To obtain source checkouts use `gh repo clone`,
then check out the revisions in `reference-notices/NOTICE.md`.

```sh
python3 tools/faar_recovery/build.py --root /tmp/faar-recovery \
  --sox-source /tmp/sox-vulkan-audit --kbsrc-source /tmp/kbsrc \
  --audiofile-include /tmp/ARDFTSRC-AudioFile \
  --ardft-deps /tmp/ardft-deps/root/usr
```

The build creates three isolated double-coefficient FAAR variants: default
polyphase, the archived scaled-cutoff regression, and reconstructed staged
baseline with fixed 0.49 cycles/input-sample output cutoff. It refuses an
unrecognized cutoff configuration. It also builds native reference drivers.
External source instrumentation is confined to scratch copies; numerical code
and reference checkouts are unchanged. All sources, commands, and binaries are
identified in `build-manifest.json`. KBSRC uses a portable build with both
baseline and AVX2 dispatch; the existing native-only build cannot test its
portable backend. Its coefficient designer may spawn workers despite the
one-thread processing option, and that CPU is included in setup timing.

## Run serially

```sh
python3 tools/faar_recovery/diagnose.py --phase all
python3 tools/faar_recovery/oracle.py
python3 tools/faar_recovery/stage_timers.py
python3 tools/faar_recovery/profile.py --method callgrind
python3 tools/faar_recovery/profile.py --method massif
/tmp/faar-recovery/fft-bench > /tmp/faar-recovery/fft.json
python3 tools/faar_recovery/validate.py
python3 tools/faar_recovery/matched_kbsrc.py
```

`diagnose.py` supports `--phase baseline|signals|bench`, `--engines`, and an
optional `--reference` executable. Measurements default to seven alternating
serial rounds after one warmup on common approximately 30-second stereo input.
A common zero tail aligns the benchmark workload across engines. Setup, native
processing, whole child CPU, and whole child wall time are distinguished.
SoX's `bufsiz` is in samples: the driver sets `block_frames * channels`, not
bytes. The `sox-wide` reference uses CPU `rate -v -b 99.5`.
An experimental candidate library can be included with
`--engines faar-candidate sox-wide ardftsrc --candidate-library PATH`; use
`--suffix=-staged-short` to keep its signal and benchmark JSON separate from
the baseline records.

Quality probes use common float32 PCM and output, with the same interior time
window, recorded ARDFTSRC delay removal, and zero endpoints. Extra precision is
internal and recorded. Sample fingerprints are not cross-engine quality tests.
The two-tone control reports selected IMD product amplitudes; multitone/noise
and endpoint impulse records remain diagnostics rather than composite scores.

The 70-digit Decimal oracle checks SoX prototype generation, full phase-bank
ordering, and selected interior cascade impulse samples against observed output.
The stage timers wrap only processing boundaries in a scratch FAAR library;
outputs must match the uninstrumented library byte for byte before timing.
FFT microbenchmarks include necessary packing and inverse normalization and
validate round trips before timing. The current FAAR real FFT rejects non-power
of-two lengths; rational-size tests use its existing complex fallback explicitly.

Callgrind uses **processing-only** collection and reports instruction counts,
not CPU time. Massif reports peak heap including driver input/output storage;
it is not a streaming-memory comparison. Native FAAR allocation accounting
includes arena allocations **and mmap allocations**, and reports sampled retained
streaming high water separately from transient peak allocations. KBSRC's zero
`setup_live_heap_bytes` field means unavailable, not zero memory consumption.
`validation.json` contains exact adapter/CLI comparisons and binary size data.

The bounded KBSRC sweep derives lengths from the reference 220.5 Hz transition
and tests beta 16, 20, 24 in portable Fast mode; it is a reference-setting
comparison, not a change to FAAR.

Tone diagnostics can target explicit pairs with `--signal-pair IN OUT`. For a
full response sweep, `--signal-matrix --tones-only` runs the 357 ordered rate
and +/-1 Hz neighbor pairs in FAAR's matrix fixture while skipping impulse,
two-tone, and noise cases. For example:

```sh
python3 tools/faar_recovery/diagnose.py --phase signals --signal-matrix \
  --tones-only --engines faar-candidate ardftsrc \
  --candidate-library /tmp/faac-faar-native-build/libfaar/libfaar.so \
  --faar-quality 2 --suffix=-response-matrix-high
```

Upsampling image measurements use a separate least-squares sinusoid fit on a
one-second steady-state output segment. `--image-pair IN OUT` measures one
upsampling conversion; `--image-matrix` covers supported upsampling pairs and
their +/-1 Hz neighbors. The input tone is 0.45 times the input rate (the
documented High passband edge), and all in-band images separated from the
wanted tone by at least 10 Hz are recorded. Run `--faar-quality 2`, `1`, and
`0` to characterize High, Good, and Fast respectively. ARDFTSRC uses its fixed
reference settings for all three runs; lower-preset image records are
characterization, not a pass/fail comparison to an equivalent preset. No
Good/Fast image acceptance mask is currently specified.
Both linear amplitudes and attenuation relative to the wanted tone are kept so
results near the float32 floor are not mistaken for precise dB measurements.
Non-tone signal runs also record central-impulse peak alignment and pre/post
ringing energy outside a 1 ms guard around the main pulse. These are descriptive
measurements, not automatic acceptance thresholds. `--transient-matrix` runs a
central impulse and endpoint-energy probe over the 357 ordered supported-rate
and +/-1 Hz neighbor pairs. It uses a two-second input with impulses at the
start, center, and end; the diagnostic FAAR driver explicitly selects ZERO
boundary extension. The output records per-pair symmetry, peak alignment, and
10 ms endpoint energy. It does not compare HOLD-boundary behavior or define an
automatic symmetry/endpoint threshold.
Example:

```sh
python3 tools/faar_recovery/diagnose.py --phase signals --image-matrix \
  --engines faar-candidate ardftsrc \
  --candidate-library /tmp/faac-faar-native-build/libfaar/libfaar.so \
  --faar-quality 2 --suffix=-image-matrix-high

python3 tools/faar_recovery/diagnose.py --phase signals --image-matrix \
  --engines faar-candidate ardftsrc \
  --candidate-library /tmp/faac-faar-native-build/libfaar/libfaar.so \
  --faar-quality 1 --suffix=-image-matrix-good

python3 tools/faar_recovery/diagnose.py --phase signals --image-matrix \
  --engines faar-candidate ardftsrc \
  --candidate-library /tmp/faac-faar-native-build/libfaar/libfaar.so \
  --faar-quality 0 --suffix=-image-matrix-fast

python3 tools/faar_recovery/diagnose.py --phase signals --transient-matrix \
  --engines faar-candidate ardftsrc \
  --candidate-library /tmp/faac-faar-native-build/libfaar/libfaar.so \
  --faar-quality 2 --suffix=-transient-matrix-high
```

The proposed numerical-floor ceiling is diagnostic output for review; the
existing gain, alias, and fingerprint thresholds are unchanged. A whole-file
KBSRC/ARDFTSRC interface is never labeled as passing FAAR's streaming contract.
## Additional competitor engines

Extra `diagnose.py --engines` names, all with the same driver argv, timing,
heap, and output contract as the existing engines (`setup_cpu_seconds`,
`processing_cpu_seconds`, `setup_live_heap_bytes`, `whole_process_peak_rss_kib`,
plus an `io_type` field):

| Engine | Library and configuration | I/O |
| --- | --- | --- |
| `soxr-hq` | libsoxr `soxr_quality_spec(SOXR_HQ,0)`, one thread | float32 interleaved |
| `soxr-vhq` | libsoxr `SOXR_VHQ`, one thread | float32 interleaved |
| `tuneout-standard`, `tuneout-long`, `tuneout-extralong` | tuneout-resampler Generic policy with the Standard/Long/ExtraLong length | `process_f32` (f64 accumulation) |
| `ardftsrc-rs` | ardftsrc-rs `Config` quality 16384, bandwidth 0.99 (matches the C++ ARDFTSRC comparison) | file f32; samples f64 |
| `ardftsrc-rs-default` | ardftsrc-rs default config (quality 2304, bandwidth 0.975) | f32 |

qaac runs HQ with float32 and VHQ with float64; here both use float32 to match
FAAR's float API. libsoxr is driven in `block_frames` chunks and drained with a
NULL input. ardftsrc-rs rejects quality 16384 with f32 samples
(`QualityTooHighForF32`), so the matched variant converts to f64 inside the
timed region; its chunk size is dictated by the crate (`input_buffer_size()`),
so the driver buffers `block_frames` input into those chunks. Output counts
equal `ceil(n*out/in)` for all of them in the smoke run; ardftsrc-rs trims its
own algorithmic delay internally.

The Rust engines live in `rust_driver/` (a standalone crate, not part of the
FAAC build) and share the argv shape of `driver.cpp`:
`rust_driver <tuneout|ardftsrc-rs> <variant> input.f32 output.f32 in_rate out_rate channels block_frames output_capacity`.
It uses `CLOCK_PROCESS_CPUTIME_ID` via libc and `getrusage`. Live heap comes from
a counting global allocator, so it covers Rust allocations only (and also
reports `peak_live_heap_bytes`). Tuneout builds its coefficient bank in
construction, so its setup time and heap are large relative to the others.
`whole_process_peak_rss_kib` is the child's `ru_maxrss`, which can be inflated by
the parent process image on fork, as with the existing engines.

Rust >= 1.89 is required (tuneout-resampler). `build.py` uses
`ROOT/rust-toolchain/bin` when present, else `cargo` on PATH. The references are
cloned into `ROOT/refs` and checked out at the commits pinned in `build.py`; the
commit hashes, the soxr header/library hashes, and the rustc version are
recorded under `external_references` in `build-manifest.json`. `--external-only`
rebuilds just the soxr/Rust drivers and merges their manifest entries without
touching the FAAR/SoX/KBSRC builds. The `rust_driver/Cargo.toml` path
dependencies are fixed to `/tmp/faar-recovery/refs`.

```sh
python3 tools/faar_recovery/build.py --root /tmp/faar-recovery --external-only
python3 tools/faar_recovery/diagnose.py --phase bench --repeats 1 --seconds 2 \
  --bench-pair 96000 44100 --bench-pair 44100 48000 \
  --engines faar-default soxr-hq soxr-vhq tuneout-standard ardftsrc-rs --suffix=-external-smoke
```

Reference licenses and instrumented-copy policies are in
[reference-notices/NOTICE.md](reference-notices/NOTICE.md).
