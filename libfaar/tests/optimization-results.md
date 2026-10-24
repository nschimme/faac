# History buffering optimization, 2026-10-24

Baseline: `528472818587ca659af0f16c5ac68917e3bd5172`.
Measured on macOS arm64 with Apple Clang 21, release optimization and LTO,
double arithmetic, stereo. Timing values below are medians of seven isolated,
alternating baseline/candidate runs. Each run processes three four-second
streams. Units are million input frames per second; input chunks are 1024 frames.

| Conversion, High quality | Output capacity | Baseline | Candidate | Ratio |
|---|---:|---:|---:|---:|
| 48,000 → 44,100 | 17 | 4.118 | 25.896 | 6.29× |
| 44,100 → 48,000 | 17 | 3.449 | 22.040 | 6.39× |
| 192,000 → 44,100 | 17 | 14.323 | 52.347 | 3.65× |
| 96,000 → 48,000 | 17 | 15.213 | 79.039 | 5.20× |
| 48,000 → 44,100 | 1024 | 25.118 | 27.083 | 1.08× |
| 44,100 → 48,000 | 1024 | 21.692 | 23.551 | 1.09× |
| 192,000 → 44,100 | 1024 | 52.811 | 54.518 | 1.03× |
| 96,000 → 48,000 | 1024 | 79.574 | 84.235 | 1.06× |

Spot checks with 1024-frame output: Good 48,000 → 44,100 measured
32.392 → 32.751; High 47,999 → 44,100 measured 20.007 → 20.288.
These small differences are close to ordinary timing variation.

Setup times were approximately unchanged. Whole-process peak RSS increased
by 0–352 KiB in this matrix; there is no measured overall footprint reduction.
Flush no longer allocates a temporary interleaved tail, and channel growth
remains sequential to limit transient allocation. The moving history window
can touch more of the existing allocation. RSS includes runtime/allocator
behavior and is not a count of library allocations.

A separate experiment reduced the FFT sizing multiplier from five to two.
It reduced RSS but slowed several Good conversions by roughly 10–20%, so it
was discarded. Filter designs and FFT planning remain unchanged.

## Quality gate

`compare_quality.py` requires exact equality of every float32 output bit,
including signed zeros. It tests the categories described in the
[Hydrogenaudio technical reference](https://src.hydrogenaudio.org/technical)
and [dBpoweramp explanation](https://www.dbpoweramp.com/sample_rate_conversion):
sweep, alias tone/noise, two-tone distortion, phase impulses, and independently
processed track edges. It also covers boundary modes, channel layouts, dither,
and queued streaming across common and uncommon rates. The signals are generated
independently; this is not a reproduction of the sites' plots or scoring.

- Double: 372 cases, all output PCM bit-identical to baseline.
- Float: 248 cases, all output PCM bit-identical to baseline.
- Release double suite: 47 passed.
- Release float suite: 45 passed.
- AddressSanitizer + UndefinedBehaviorSanitizer debug suite: 46 passed.

The pure 23 kHz tone, resampled from 96 kHz to 44.1 kHz, also has an absolute
rejection gate of 100/130/150 dB for Fast/Good/High. Measured rejection, excluding
one second at each endpoint, was identical before and after:

| Precision | Fast | Good | High |
|---|---:|---:|---:|
| Double | 105.85 dB | 137.58 dB | 152.72 dB |
| Float | 105.85 dB | 135.06 dB | Unsupported |

The existing passband, alias, symmetry, impulse, boundary, and streaming tests
remain required. A baseline match does not establish that every existing
artifact is acceptable, nor does finite testing prove behavior for all inputs.
In particular, matching independently processed track edges preserves their
existing behavior; it does not establish an artifact-free gapless join.

## Why output should be preserved

The logical history sequence is unchanged: consuming frames advances its
pointer rather than moving the remaining samples. Before input would exceed
trailing capacity, compaction copies that same remaining sequence back to the
allocation's start. FIR coefficients, sample ordering, accumulation, rational
clocks, dither, and boundary values are unchanged. Flush writes the same repeated
endpoint samples directly into history instead of passing a temporary buffer
through the input path. The strict comparisons exercise these invariants; they
are evidence for preserving output, not a universal proof of fidelity.
