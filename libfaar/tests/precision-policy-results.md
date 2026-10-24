# Precision policy measurements, 2026-10-24

These measurements compare whole-library float and double builds with a mixed
precision build that runs Fast/Good in float and High in double.

## Native arm64 performance and memory

Measured on macOS arm64 with Apple Clang 21, release optimization and LTO.
The streaming benchmark uses stereo 48,000 → 44,100 Hz, 1,024-frame input
blocks and output capacity 1,024. Its throughput is input frames per second.
RSS is whole-process peak RSS, not live library allocation.

| Quality | Float Mframes/s | Double Mframes/s | Mixed Mframes/s | Mixed selection |
|---|---:|---:|---:|---|
| Fast | 50.179 | 34.599 | 49.750 | Float |
| Good | 49.336 | 34.000 | 49.311 | Float |
| High | 2.868 | 28.443 | 28.128 | Double |

Each entry is the median of seven alternating runs. Float is about 1.45×
faster for Fast/Good; double is about 9.9× faster than compensated float for
High. Mixed tracks its selected standalone engine within 1.2% on this route.

The linked float, double, and mixed dylibs were 87,104, 70,416, and 140,288
bytes. Their text sections were 57,008, 42,076, and 98,748 bytes. Their reserved BSS
sections were 1,049,280, 2,097,728, and 3,147,008 bytes. Mixed therefore adds
about 55 KiB of text and 1 MiB of BSS over double. The three-run median open
RSS for stereo 48,000 → 44,100 Hz was:

| Quality | Float | Double | Mixed |
|---|---:|---:|---:|
| Fast | 1,952 KiB | 2,176 KiB | 1,984 KiB |
| Good | 2,096 KiB | 2,160 KiB | 2,128 KiB |
| High | 3,136 KiB | 5,216 KiB | 5,248 KiB |

RSS is coarse and includes process and resident table pages.

The mixed release suite passed 51 checks. Strict public-API comparisons also
passed 320 Fast/Good cases bit for bit against float and 160 High cases against
double. The standalone float and double suites passed 52 and 51 checks.

Requested heap was measured with allocator hooks at 48,000 → 44,100 Hz,
stereo, with 8,192 frames reserved for preparation. This counts requested
library allocation sizes, including transient peak during prepare; it excludes
allocator metadata, static BSS, process memory, and caller buffers. Closing
each handle returned tracked live heap to zero.

| Quality | Float prepared peak | Double prepared peak | Mixed prepared peak |
|---|---:|---:|---:|
| Fast | 804,200 bytes | 1,138,896 bytes | 804,216 bytes |
| Good | 972,168 bytes | 1,468,144 bytes | 972,184 bytes |
| High | 3,468,088 bytes | 6,234,976 bytes | 6,234,992 bytes |

Across the 48/96/192 kHz and 1/2/8-channel matrix, mixed used exactly 16 bytes
more prepared peak heap than the selected standalone engine, for the dispatch
handle. Float Fast used 1.38–1.71× less prepared peak heap than double; float
Good used 1.43–1.71× less. Float High used 1.80–2.08× less heap than double,
but its throughput was 8.19–10.56× lower.

An expanded native arm64 matrix used 48,000, 96,000 and 192,000 Hz inputs to
44,100 Hz, with 1, 2 and 8 channels. Each throughput point is the median of
seven alternating benchmark runs (each run processes three four-second
streams). The checked-in `bench_precision.py` harness reproduced the matrix:
float Fast was 1.40–1.69× as fast as double, float Good was 1.25–1.70× as
fast, and double High was 8.15–10.49× as fast as compensated float High.
Mixed stayed within 1.14% of the corresponding standalone engine for each
selected quality.

The same matrix measured open-only peak RSS, but its values varied between
Docker runs under amd64 emulation. The requested-heap measurements above are
the per-handle RAM comparison; process RSS should be treated only as a coarse
whole-process check.

## GCC 13 amd64 footprint build

The `faac-fp:latest` Docker image provides Alpine 3.19.9, GCC 13.2.1, amd64,
release and LTO. The Docker engine host is arm64 (`VirtualApple @ 2.50GHz`), so
the container is emulated. Its binary size and footprint observations are
useful for that build configuration; its throughput must not be treated as
native amd64 performance.

The linked double, float, and mixed libraries were 77,416, 98,048, and 163,440
bytes. GNU `size` reports these text, data, and BSS sections:

| Build | Text | Data | BSS |
|---|---:|---:|---:|
| Float | 85,247 bytes | 640 bytes | 1,049,352 bytes |
| Double | 62,070 bytes | 632 bytes | 2,097,800 bytes |
| Mixed | 143,911 bytes | 752 bytes | 3,147,080 bytes |

Requested heap at stereo 48,000 → 44,100 Hz, with 8,192 frames reserved,
measured:

| Quality | Float prepared peak | Double prepared peak | Mixed prepared peak |
|---|---:|---:|---:|
| Fast | 804,200 bytes | 1,138,896 bytes | 804,216 bytes |
| Good | 972,168 bytes | 1,468,144 bytes | 972,184 bytes |
| High | 3,468,088 bytes | 6,234,976 bytes | 6,234,992 bytes |

The allocation-hook results match the native arm64 heap values exactly. A
fresh heap-only run of all 27 rate/channel/quality cases confirmed mixed used
exactly 16 bytes more prepared peak heap than the selected standalone build;
all cases returned tracked live heap to zero after close. Process RSS varied
substantially between emulated Docker runs and is not used to infer per-handle
heap. A fresh seven-repeat timing sweep was prohibitively slow under emulation
and was stopped after partial results; those timings are not used as x86
evidence. The GCC 13 amd64 LTO release suite passed all 51 tests.

The CI workflow now includes a dedicated GCC 13 job on an x86_64 Ubuntu runner.
It runs the same float/double/mixed throughput and requested-heap matrix and
uploads CSV results with compiler and host information. This development host
is arm64, so that native x86_64 artifact has not been collected here yet.

## Current conclusion

On the measured native arm64 machine, mixed captures float's Fast/Good speed
and double's High speed. Its cost is about 55 KiB more text and 1 MiB more
reserved BSS than double. Mixed is now the default policy for general CPU
builds; `float` remains available for microcontrollers that favor a single
float engine, and `double` remains available to reduce code and table
reservation. The GCC 13 amd64 build confirms the mixed binary footprint, but
native amd64 performance from the CI benchmark and representative embedded RAM
limits remain follow-up measurements before treating this as optimal for every
target.
