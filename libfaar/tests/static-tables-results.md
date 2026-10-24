# Shared FFT twiddle tables: measurements

The implementation replaces repeated per-plan twiddle construction for common
complex FFT sizes with lazily initialized, immutable process-wide tables. Sizes
16 through 65,536 have a bounded static BSS reservation of about 2 MiB in a
double build or 1 MiB in a float build; only sizes actually used are
initialized. Initialization uses the shared `common/atomic.h` once helper.
Tables have library/process lifetime, while each FFT plan retains private work
buffers and scratch. The static arrays are cache-line aligned, which was needed
to avoid a throughput regression relative to the allocator-aligned tables.
Larger FFT sizes continue to build dynamic per-plan tables. No public API
changed.

The figures below are medians from seven alternating, isolated baseline and
candidate runs. The baseline library is revision `7a1ed3f0`; the candidate has
the aligned lazy tables. Open benchmarks keep all handles alive and process no
PCM. Peak RSS is the whole process's high-water mark, so it includes library
and process memory and is not a live-allocation measurement.

## Open time and memory

Values are baseline → candidate. `h` is the number of simultaneously live
handles. Open times are milliseconds; RSS is KiB.

| Configuration | First open | Mean subsequent open | Peak process RSS |
|---|---:|---:|---:|
| Stereo High, 48,000→44,100, h1 | 1.299 → 1.170 | — | 5,200 → 5,216 |
| Stereo High, 48,000→44,100, h8 | 1.241 → 1.053 | 1.227 → 0.795 | 29,088 → 24,272 |
| Stereo High, 48,000→44,100, h32 | 1.237 → 1.070 | 1.222 → 0.786 | 112,048 → 89,936 |
| 8-channel High, 48,000→44,100, h8 | 3.473 → 2.089 | 3.534 → 1.837 | 109,600 → 88,832 |

## Streaming throughput

Seven-run median throughput ratios ranged from 0.994× to 1.022× baseline
across the sampled stereo and 8-channel cases, including tiny and ordinary
output capacities and an awkward rational rate. This
sample shows no material throughput change. Startup gains are separate from
streaming throughput, which excludes setup.

## Quality and concurrency checks

The baseline comparison passed 372 double and 248 float cases with every
output PCM bit identical, including sweeps, alias tones, two-tone signals,
phase impulses, boundaries, channel layouts, and dither. The absolute
23 kHz rejection gates also passed. This establishes equivalence for the
tested cases; it is not a proof for every possible input or a certification
of the baseline's fidelity.

The release double suite passed 48 checks; release float passed 46;
AddressSanitizer + UndefinedBehaviorSanitizer passed 47. The concurrent FFT
initialization test also passed under ThreadSanitizer. It checks racing first
initialization, common-table reuse, private mutable workspaces, clear/reopen,
and independently allocated tables above the static size limit.
