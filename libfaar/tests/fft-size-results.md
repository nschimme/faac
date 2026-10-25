# FFT-size selection experiment, 2026-10-24

The current staged prefilter starts from its route's explicit FFT size, or 1,
then doubles until the transform is at least five times the tap count. I tested
the same sizing rule with 4× and 3× minima. These are thresholds before power
of two rounding; 3× and 4× selected the same geometries in the measured cases.
Explicit route FFT sizes were unchanged. Filter coefficients and quality
settings were unchanged.

## Measurements

Measured on macOS arm64 with Apple Clang 21, mixed precision, release/LTO.
Throughput used stereo, 1,024-frame input blocks and output capacity 1,024;
each value is the median of three alternating runs processing three
four-second streams. Heap is requested live heap after preparing for 8,192
frames. First-output counts are input frames from the existing throughput
probe. The tested conversions were 48,000, 96,000 and 192,000 Hz to 44,100 Hz.

| Input rate | Quality | Throughput 5× → 4× (Mframes/s) | Prepared heap 5× → 4× (bytes) | First output 5× → 4× (frames) |
|---:|---|---:|---:|---:|
| 48 kHz | Fast | 48.818 → 48.148 (-1.4%) | 569,240 → 499,608 (-12.2%) | 1,024 → 1,024 |
| 48 kHz | Good | 47.341 → 44.863 (-5.2%) | 775,608 → 561,080 (-27.7%) | 2,048 → 1,024 |
| 48 kHz | High | 28.063 → 28.085 (+0.1%) | 4,639,784 → 4,639,784 | 12,288 → 12,288 |
| 96 kHz | Fast | 80.391 → 77.393 (-3.7%) | 364,864 → 299,328 (-18.0%) | 2,048 → 1,024 |
| 96 kHz | Good | 76.413 → 74.136 (-3.0%) | 563,584 → 357,248 (-36.6%) | 4,096 → 2,048 |
| 96 kHz | High | 44.235 → 43.718 (-1.2%) | 4,455,136 → 4,455,136 | 24,576 → 24,576 |
| 192 kHz | Fast | 98.323 → 91.932 (-6.5%) | 565,336 → 359,000 (-36.5%) | 4,096 → 2,048 |
| 192 kHz | Good | 80.178 → 78.896 (-1.6%) | 706,632 → 500,296 (-29.2%) | 7,168 → 3,072 |
| 192 kHz | High | 60.052 → 61.132 (+1.8%) | 4,404,016 → 4,404,016 | 48,128 → 48,128 |

Prepared High callback timing was measured at 64-frame input blocks and output
capacity 17, with stereo and eight-channel cases. The stereo p99 was 1–2 μs
for both sizes; observed maximums ranged from 249–368 μs at 5× and 249–362 μs
at 4×. Eight-channel p99 and maximum were also mixed: p99 was unchanged at
48/96/192 kHz (2/2/6 μs), while maxima were 987/1,443/1,408 μs at 5× and
1,070/1,410/1,425 μs at 4×. These small-run maxima do not show a consistent
callback-latency improvement. Eight-channel High prepared heap and throughput
were effectively unchanged across the two sizes.

## Quality

The full configured mixed suite passed, 55/55. A metric-only PCM comparison
covered 480 cases because different FFT sizes naturally change rounding; 224
cases differed from the baseline, with a maximum absolute output difference
of 1 PCM LSB and a maximum per-case RMS difference of 0.197 LSB. High remained
bit-identical in all 160 comparison cases because its measured transform
geometry did not change. The 23 kHz alias probe measured 105.86/136.24/152.72
dB for Fast/Good/High, above the 100/130/150 dB gates. These results support
the tested geometry's quality, but do not establish quality for every possible
input or route.

## Decision

The smaller geometry offers a substantial prepared-heap reduction and often
earlier first output for Fast/Good. It also consistently reduced measured
Fast/Good throughput in this matrix (up to 6.5%), while High showed no material
heap, first-output, or callback-latency benefit. Since this task is a
performance optimization and the available local measurements do not support
a throughput win, the 5× rule is retained. The 4×/3× experiment is recorded
for future use if a memory- or startup-latency-focused policy is desired. Native
x86_64 performance remains unmeasured.
