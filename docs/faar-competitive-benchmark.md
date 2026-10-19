# FAAR versus ARDFTSRC

The current candidate ranking and corrected benchmark set are in the
[recovery diagnosis](faar-recovery-diagnosis.md) and its
[machine-readable measurements](measurements/faar-recovery-2026-10-18/). Tables
below are historical snapshots and should not be used as current promotion
evidence.

## Current result

FAAR is not yet competitive with ARDFTSRC's desktop processing throughput.
The Q39 FFT prototype approaches its quality on the measured probes, but is
slower than current FAAR. Neither result establishes completion of the quality,
performance, and footprint goal.

Median processing CPU seconds for approximately 30 seconds of stereo audio:

| Conversion | ARDFTSRC | Current FAAR High | Q39 FFT prototype |
|---|---:|---:|---:|
| 96 → 44.1 kHz | 0.0641 | 1.5695 | 3.6080 |
| 48 → 44.1 kHz | 0.0475 | 1.0604 | 3.4331 |
| 44.1 → 48 kHz | 0.0496 | 1.1452 | 3.4918 |

Current FAAR takes 22–24 times the reference's processing CPU, and the prototype
takes 56–72 times. Current FAAR's filter is narrower and has lower rejection;
its timing does not represent reference-level quality. The prototype's selected
tone and alias tests approach the reference, but complete quality parity remains
unproven. The prototype is not the implementation shipped by `libfaar`.

## Method

- Host: AMD Ryzen 9 7940HS, x86-64 Linux; one process at a time.
- Seven fresh runs per implementation and conversion, after one warmup. Order
  rotates each round. No other agent's heavy workloads ran during the benchmark.
- C/C++ code and drivers use GCC 13.3, `-O3 -flto`. ARDFTSRC links the downloaded
  Debian FFTW static archive, which contains optimized code and has its own build
  provenance. This is a comparison with that actual reference implementation,
  not an instruction-set-normalized algorithm comparison.
- ARDFTSRC quality 16384 and bandwidth 0.99. FAAR uses High. The prototype uses
  a Q39 overlap-save prefilter and a 127-tap integer interpolation filter.
- Identical float32 stereo WAVs contain deterministic multitone/noise audio,
  rounded to whole ARDFTSRC input blocks. One zero block is appended to every
  implementation's input to drain ARDFTSRC's overlap. Logical audio durations
  are 30.24, 30.24, and 30.16 seconds respectively.
- `CLOCK_PROCESS_CPUTIME_ID` measures processing separately from setup. WAV
  loading, input-format conversion, symbol loading, output validation, and WAV
  writing are excluded. Processing includes output collection and FAAR flush.
  The reference retains double output internally before excluded WAV writing;
  FAAR emits float32 through its API.
- All output counts match the common padded-input duration; all outputs are
  finite. These are host measurements, not ESP32-S3 timings.

Median setup CPU time was 4.4–4.9 ms for ARDFTSRC, 2.4–4.4 ms for current FAAR,
and 3.3 ms for the prototype. It is excluded from the processing table.

The [raw measurements](measurements/faar-competitive-2026-10-17.json) contain
every sample, commands, binary/source/input hashes, output counts, setup times,
and memory counters. Peak RSS includes whole-file input/output buffers and is
not the minimum streaming working set. FAAR's net live heap across creation is
258,736 bytes at 96→44.1 kHz and 112,832 bytes in the other two cases; the
prototype uses 691,696–697,248 bytes before processing grows any buffers.

Separate one-pass reference runs measured setup allocations with the same glibc
live-heap counter, after input loading and before processing:

| Conversion | ARDFTSRC | Current FAAR | Q39 prototype |
|---|---:|---:|---:|
| 96 → 44.1 kHz | 3,524,400 B | 258,736 B | 691,696 B |
| 48 → 44.1 kHz | 2,382,032 B | 112,832 B | 691,696 B |
| 44.1 → 48 kHz | 2,231,472 B | 112,832 B | 697,248 B |

These are net live allocations across setup, not peak working memory or an
upper bound during processing. The prototype uses 3.2–5.1 times less setup heap
than the reference, but that advantage does not resolve its processing CPU gap.

## Required next work

Judge optimizations against ARDFTSRC as well as current FAAR. Reduce processing
cost while preserving measured rejection and bandwidth, then repeat the sweep,
distortion, impulse, quiet-input, boundary, duration, and streaming checks.
The new endpoint-hold behavior improves track boundaries but cannot reconstruct
missing neighboring audio or carry a sample clock between independent handles.

## Architecture reset

Two independent short-filter optimizations were measured with the same GCC 13
O3/LTO setup, fresh baseline/reference runs, seven repetitions, and common
30-second stereo workloads. Removing per-tap division reduced processing CPU
by 7.7–8.8%; caching recurring coefficient rows reduced it by 16.4–18.4%.
The cached variant still took 49.2–61.7 times ARDFTSRC's processing CPU.
Both variants preserved the prototype output byte for byte in deterministic
streaming controls. Neither changes its quality limitations or supports the
complete production API.

[Division-removal data](measurements/faar-groupmac-2026-10-17.json) and
[coefficient-cache data](measurements/faar-cached-2026-10-17.json) retain raw
samples and provenance. The cache raises net create heap from 691,696 to
732,288 bytes for the first two rate cases; its code and read-only data are
also larger. These variants will not be promoted into the production backend.

Historical staged comparisons above describe the earlier 127-tap-per-phase
architecture. The current desktop default is the independent double-precision
`staged-short` CPU backend: an overlap-save DFT prefilter followed by a short
fractional-delay phase bank. The `polyphase` backend remains available as an
explicit comparison build. Ooura's real FFT source is included under its
upstream permission notice; no SoX implementation source is linked into FAAR.

On the current workspace library, 10-trial Good and Fast candidate medians are lower than wide SoX on all 16
measured common conversions. High also measured lower on all 16 routes after a 4097-tap plan was selected for
32→48 kHz. Sixty alternating-order rounds measured 4.93% lower paired median CPU time for 32→48 and 1.21%
lower for 192→48; the latter remains close to parity. The 95% paired bootstrap intervals were 2.18–7.16%
faster and 0.04–3.01% faster, respectively. Selected passband gain probes differ from ARDFTSRC by at most
0.021 dB for High and Good and 0.143 dB for Fast. This is limited to the measured probes and host; it is not
perceptual certification.
See the [current recovery diagnosis](faar-recovery-diagnosis.md) and the
[common-rate measurements](measurements/faar-recovery-2026-10-18/staged-short-multirate-native/common-rates-current-summary.json)
and [60-round paired follow-up](measurements/faar-recovery-2026-10-18/staged-short-multirate-native/high-edge-routes-60trial-summary.json)
for raw scope and remaining validation.

[Matched SoX results](measurements/faar-staged-sox-cpu-2026-10-18.json) and
[local footprint results](measurements/faar-staged-footprint-2026-10-18.json)
retain the earlier runs and binary hashes.
