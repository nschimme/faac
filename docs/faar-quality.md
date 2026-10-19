# FAAR quality measurements

See the [recovery diagnosis](faar-recovery-diagnosis.md) for the current
candidate comparison and measured staged baseline. Older records below retain
the measurements and conclusions made at their recorded dates; their timings
and memory estimates have since been superseded by the recovery dataset.

See the [direct ARDFTSRC benchmark](faar-competitive-benchmark.md) for current
performance, methodology, and the remaining CPU gap.

FAAR quality work is in progress. The selected `staged-short` backend uses
float PCM and double-precision filter and FFT arithmetic. The legacy polyphase
backend remains available as a comparison build; its coefficient storage is
selectable at build time. Embedded arithmetic alternatives remain deferred.

## Local comparison

The Python tools in the sibling `faac-benchmark` repository measure actual
resampler output rather than an idealized filter response:

- `scripts/analyze_faar_src.py` measures wanted-tone gain, residual RMS, and
  total unwanted output energy. It records library hashes and output length.
- `scripts/benchmark_faar_src.py` compares host throughput using a C harness.
- `scripts/generate_src_benchmark_inputs.py` and
  `scripts/benchmark_src_reference.py` compare ARDFTSRC and two FAAR builds
  using common hashed WAVs, separate setup timing, and rotating serial runs.
  Build commands and timing boundaries are in `faac-benchmark/docs/scripts.md`.
- `scripts/score_ha_src_wavs.py` scores aligned WAVs using a Python port of the
  published HydrogenAudio bandwidth, alias-spike, and IMD calculations.
- `scripts/ardftsrc_oracle.py` independently reproduces the reference block
  algorithm with NumPy and optional executable-parity controls.
- `tests/test_analyze_faar_src.py` checks the analyzer against analytic controls.
- `tests/test_ha_src_metrics.py` checks Welch scaling and the score calculations.

Use `--input-peak` with the tone analyzer to check quiet input as well as nominal
levels. Input-relative residuals and alias attenuation remain measurable below
the absolute dBFS reporting floor. The scoring port has analytic controls;
execution parity with Octave has not yet been established. The inspected
published scripts do not supply an overall composite-score formula.

ARDFTSRC comparisons use the same float32 source and output format, quality
16384, and bandwidth setting 0.99. Its command-line output requires zero input
padding and removal of its block delay before comparing equal time intervals.
These comparisons do not reproduce every format or score on the HydrogenAudio
website. Deep rejection measurements can be limited by float32 source rounding.

## Historical polyphase-only limits

The current High preset does not yet match ARDFTSRC's combination of bandwidth
and rejection. At 96 kHz to 44.1 kHz, the corrected 511-tap implementation has
roughly 143 dB rejection for a 22.06 kHz probe, while the reference measures about
157 dB with the same float32 input. The wanted-tone residual at 20 kHz is about
−152 dBFS versus −162 dBFS for the reference, using a peak-0.5 source.
These are individual probes, not a guaranteed stopband specification or a
perceptual quality score.

## Earlier long staged backend (historical measurements)

These measurements describe the earlier long-Kaiser staged experiment, not the selected `staged-short` backend. The old experiment combined an overlap-save prefilter with the existing rational-phase output stage. On the 96→44.1 kHz
High probe set, its 20 kHz wanted gain is effectively identical to ARDFTSRC;
the 22.06 and 23 kHz alias attenuation differences are 0.110 and 0.211 dB.
The 1 kHz wanted gains also match. Residual RMS differs by 0.83 dB at 1 kHz
and 0.253 dB at 23 kHz; both values are near the float32 output floor. The
relative differences there are dominated by very small quantization residuals,
but the strict 0.25 dB criterion is not met for every reported metric. These
results cover this conversion and these tones only, so the full quality gate
remains open.

[Staged ARDFTSRC probe data](measurements/faar-staged-ardftsrc-quality-2026-10-18.json)
records commands, hashes, alignment, and raw measurements. The staged backend
passes the local six-test suite, including the 357-pair rate matrix, streaming
invariants, channel isolation, and coefficient controls. That historical configuration lost the CPU throughput comparison: on the matched 30-second stereo 96→44.1 kHz run, it used 0.223 s process CPU versus 0.085 s for SoX CPU `rate -v`. The newer staged-short candidate supersedes that timing result; see [the current diagnosis and four-way benchmark](faar-recovery-diagnosis.md). Local default-versus-staged footprint figures are in
[the staged footprint record](measurements/faar-staged-footprint-2026-10-18.json).
The matched timing and output comparison is in
[the SoX CPU record](measurements/faar-staged-sox-cpu-2026-10-18.json).

The SoX rate plan uses a 20-tap-per-phase output filter after the 601-tap DFT
stage. Earlier 33- and 21-tap trials using FAAR's then-current Kaiser-sinc
design failed local controls. The selected backend now uses rate-specific short
banks on measured common conversions; the 48→32 kHz plan meets current tone and
alias probes but retains a small impulse time-reversal error.

Further selection work covers broader transient and perceptual behavior,
throughput on the slower standard-rate pairs, code and working-memory size,
streaming behavior, and float input dynamic range.
ESP32-S3 is an intended design target; cross-compilation is not a substitute
for on-device timing, which has not been measured.

## Existing FAAC FFT experiment

An isolated prototype adapts `libfaac/fft.c`'s float32 radix-4 Stockham
kernel to 16,384 points. It retains the experimental Q39 overlap-save
prefilter, Q39 spectral multiplication, and 127-tap integer resampling stage;
only the FFT arithmetic changes. It has not replaced the library backend.

At 96→44.1 kHz with peak-0.5 float32 sources, the 1 kHz residual was
−147.33 dBFS versus −160.61 dBFS for aligned ARDFTSRC. The 20 kHz residual
was −148.49 versus −162.44 dBFS. Total alias attenuation for 22.06 kHz was
143.66 versus 156.91 dB. Plain float32 FFT arithmetic therefore does not meet
the reference's precision in these controls. The optimized kernel's structure
remains a candidate for a backend with additional precision.

[Probe data and provenance](measurements/faar-faac-fft-float32-2026-10-17.json)
record the source/library hashes, compiler command, input scale, and measurement
window. Prototype sources are temporary experiment artifacts, not a supported
build target. No isolated throughput comparison for this variant is claimed.

## Track boundaries

The library and CLI default to HOLD endpoint extension; `FAAR_BOUNDARY_ZERO`
or `faar --boundary zero` selects silence. HOLD reduces the forced step to
silence at nonzero endpoints, but it cannot provide missing neighboring audio
or preserve one sample clock across independently created handles. Use one
handle for a continuous stream, such as a continuous album render.

In local High-preset 96-to-44.1 kHz checks, two independently converted
constant-0.5 tracks matched continuous resampling exactly across the 50-frame
join window with default HOLD (maximum difference 0); ZERO differed by up to
0.133743, and aligned ARDFTSRC differed by 0.135735. On the official
`gaplesstest_s` then `gaplesstest_m` pair, HOLD differed from continuous
resampling by at most 0.002106 in that window; ZERO differed by 0.249248, and
aligned ARDFTSRC by 0.253081. Frame counts matched continuous conversion in
both checks. These are small, measured boundary checks, not a claim of
gapless-aware track concatenation or perceptual equivalence. Results are from
the current library build, not the separate FFT prototype.

Linear-phase filtering also has pre- and post-ringing. Strong rejection and
wide bandwidth do not establish the absence of transient or boundary artifacts;
those require separate impulse, sweep, distortion, and track-join checks.

## Block-size exploration for the replacement

[ARDFTSRC quality/block-size sweep](measurements/ardft-quality-memory-sweep-2026-10-17.json)
compares quality 2048, 4096, 8192, and 16384 on five 96→44.1 kHz tone probes.
The two smaller settings lose rejection substantially. Quality 8192 is near
the float32 source floor on the wanted tones but loses about 0.9 dB of total
alias attenuation at 22.06 kHz. These controls guide the new block planner;
they do not establish equivalence across the full signal suite. The listed
buffer sizes count only the reference's four explicit FFT arrays, excluding
plans, Eigen matrices, and whole-file input/output storage.
