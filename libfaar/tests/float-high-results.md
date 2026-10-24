# Compensated float High: quality and resource measurements

Float builds now support High using high/low float expansions in the FFT,
filter spectrum, preliminary halfband filters, and final phase bank. Sample
histories and public input/output remain ordinary floats. Processing uses
float arithmetic and `fmaf`; coefficient and twiddle setup retains the
existing double calculations. Fast and Good keep their previous arithmetic.

The arithmetic retains rounding residuals using
[TwoSum and TwoProductFMA](https://www.tuhh.de/ti3/paper/rump/OgRuOi05.pdf).
The FFT uses an in-place radix-2 transform. Immutable twiddles and coefficients
are shared across channel stages within a handle, while workspaces are private.
This version prioritizes correct High output and bounded memory; it has not
received the radix-4 and cache tuning of the existing double FFT. Builds must
preserve IEEE rounding rather than reassociating expressions with fast-math.

## Quality

The existing High passband, residual, alias, boundary, impulse symmetry, length,
streaming, and wideband limits pass without relaxed tolerances. The 30 second
23 kHz test at 96,000 → 44,100 Hz measured **152.723 dB** rejection, above its
150 dB requirement and matching the double baseline at the shown precision.
The new `high_stopband` group additionally checks 150 dB rejection with one,
two, three, and eight channels, including loud in-band neighbouring channels.

The library suites pass 36 tests in float and 35 in double. Nine selected
ASan/UBSan tests cover the compensated FFT, rate matrix, passband, multichannel
stopband, streaming, and preparation. Preparation tests include every
allocation-failure position during High open on two routes, shared channel
state, zero heap calls after preparation, and cleanup. The FFT arithmetic test
compares against the independent double transform at sizes 16 through 65,536,
checks a relative RMS error below 1e-12, and checks inverse round trips and
borrowed twiddle ownership. The three selected frontend engine/CLI checks pass
with float High, including FAAC's automatic resampling above 48 kHz.

Against baseline `320ad6ce`, `compare_quality.py` reports 320 bit-identical
float Fast/Good cases and 160 bit-identical double High cases. Compensated
float High is assessed with the absolute quality tests; equality of every
output bit to double High is not required. These finite cases do not establish
the response for every possible signal and rate.

## Throughput

Measurements were made locally on macOS arm64 with Apple Clang 21, release
optimization and LTO. The double reference uses baseline `320ad6ce`. Input
blocks contain 4,096 frames; output capacity is 8,192 frames. The existing
`bench_faar` tool runs three complete four second streams per process. The
table gives medians across three fresh processes per configuration, with
float/double runs alternated. Processing time includes drain and flush and
excludes open/close. Rates are input frames per second, across all channels.

| Input → output | Channels | Double Mframes/s | Float Mframes/s | Float CPU ratio | Float time per 30 s of audio |
|---|---:|---:|---:|---:|---:|
| 96,000 → 44,100 | 1 | 84.045 | 7.984 | 10.53× | 0.361 s |
| 96,000 → 44,100 | 2 | 46.200 | 4.325 | 10.68× | 0.666 s |
| 96,000 → 44,100 | 8 | 11.407 | 1.101 | 10.36× | 2.616 s |
| 192,000 → 44,100 | 1 | 108.679 | 12.726 | 8.54× | 0.453 s |
| 192,000 → 44,100 | 2 | 55.483 | 6.030 | 9.20× | 0.955 s |
| 192,000 → 44,100 | 8 | 13.642 | 1.690 | 8.07× | 3.408 s |

Open averaged about 2.96–3.15 ms for compensated float and 0.72–0.80 ms for
double in these runs. The final column is derived from measured throughput;
it is not a callback deadline guarantee. Results vary with platform, compiler,
rate plan, block size, and hardware support for `fmaf`.

To run the native timing tool for a configured build:

```sh
meson compile -C build-float bench_faar
build-float/libfaar/tests/bench_faar 96000 44100 8 2 4096 8192
```

## Requested heap

A separate allocation-counter build wraps the library's `malloc`, `calloc`,
`realloc`, `free`, and aligned allocations. It measures High open, preparation
for 8,192 input frames, one second of streaming, and flush. Caller buffers and
counter bookkeeping are excluded. Each close returned tracked live heap to
zero. Peak occurred during preparation; processing did not increase it.

| Input → output | Channels | Double peak bytes | Float peak bytes | Float share |
|---|---:|---:|---:|---:|
| 96,000 → 44,100 | 1 | 5,120,272 | 2,714,096 | 53.0% |
| 96,000 → 44,100 | 2 | 5,788,176 | 3,217,904 | 55.6% |
| 96,000 → 44,100 | 8 | 20,046,528 | 9,743,792 | 48.6% |
| 192,000 → 44,100 | 1 | 5,198,944 | 2,804,808 | 54.0% |
| 192,000 → 44,100 | 2 | 5,680,288 | 3,134,992 | 55.2% |
| 192,000 → 44,100 | 8 | 19,604,944 | 9,405,184 | 48.0% |
| 48,000 → 44,100 | 1 | 5,760,804 | 3,178,828 | 55.2% |
| 48,000 → 44,100 | 2 | 6,234,968 | 3,468,088 | 55.6% |
| 48,000 → 44,100 | 8 | 21,833,696 | 10,744,528 | 49.2% |

These are requested heap bytes, excluding allocator overhead and static BSS,
rather than process RSS. Compensated twiddles and workspaces are included.
The ordinary FFT's lazy static table reservation remains about 1 MiB in float
builds and 2 MiB in double builds; reservation is not the same as resident
storage. The two floats of an expansion occupy the same space as one double;
the lower measured heap comes from smaller sample buffers and the in-place
FFT workspace layout.
