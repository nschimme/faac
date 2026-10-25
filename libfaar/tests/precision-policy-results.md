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

## Mixed float-backend specialization experiment

The checkpoint before this experiment is `bd4bf536`. Mixed's float backend
never receives High, so it can omit compensated High arithmetic and
`fft_float2.c`. Standalone float retains compensated High. This experiment
keeps the public double build option as a comparison baseline; removing the
option itself would not change what the mixed target links.

A fresh GCC 13 amd64 release/LTO build in the same `faac-fp:latest` image
reproduced the checkpoint's section sizes exactly, then measured:

| Mixed build | GNU size text | Data | BSS |
|---|---:|---:|---:|
| Checkpoint | 143,911 bytes | 752 bytes | 3,147,080 bytes |
| Float backend without compensated High | 124,739 bytes | 744 bytes | 3,147,080 bytes |

The text column falls by 19,172 bytes (13.3%). This is a GCC
binary-footprint comparison under amd64 emulation, not native amd64 timing.

The native mixed and standalone float suites passed all 51 and 52 checks.
An independent public-API comparison against the checkpoint passed 1,080
cases bit for bit: ten rate pairs, 1/2/8 channels, all three qualities, both
boundary modes, all three dither modes, and prepared/unprepared streaming.
Each stream used 4,097 input frames, input blocks up to 1,024 frames, output
capacity 137, and flush to completion.

The same native 27-case rate/channel/quality matrix used seven alternating
checkpoint/specialized timing runs per case. Specialized/checkpoint median
throughput ratios ranged from 0.982 to 1.038. These exploratory macOS timings
do not establish a performance improvement or replace native x86_64 CI.

Requested prepared peak heap at stereo 48,000 → 44,100 Hz became 804,096
bytes for Fast, 972,064 for Good, and 6,234,992 for High. Across the 27-case
matrix, removing compensation-only fields saved 120–1,280 bytes for Fast/Good
relative to the checkpoint; High retained its 16-byte dispatch overhead over
double. All closes returned tracked live heap to zero.

This specialization reduces mixed's code footprint while preserving the
standalone float build and the double comparison option.

## FFT workspace reduction experiment

On top of the float-backend specialization, two unused workspaces can be
removed without changing arithmetic or FFT geometry:

- Small, nonpadded transforms need their two complex ping-pong buffers but
  never use padded scratch. They now omit the scratch allocation when the
  ordinary work allocation also bounds the core's scratch-pointer arithmetic.
  Tiny plans retain scratch so the processing loop can remain unchanged.
- The smaller plan in an unequal-sized forward/inverse FIR pair needs only
  buffer 0 when padded. It now uses a private single-buffer initializer;
  ordinary FFT initializers keep their existing two-buffer contract.

The FFT plan records its buffer count so preparation stays within the
allocation. Both small-transform ping-pong buffers and shared padded scratch
remain available where used.

Prepared peak requested heap, compared with the preceding specialization:

| Stereo 48,000 → 44,100 Hz quality | Before | After | Saved |
|---|---:|---:|---:|
| Fast | 804,096 bytes | 765,696 bytes | 38,400 bytes |
| Good | 972,064 bytes | 972,064 bytes | 0 bytes |
| High | 6,234,992 bytes | 5,972,848 bytes | 262,144 bytes |

Across the same 27-case 48/96/192 kHz, 1/2/8-channel matrix, savings were
0–153,600 bytes for Fast (up to 10.6%) and 0–1,048,576 bytes for High
(up to 4.8%). Good's peak was unchanged. Every close returned tracked live
heap to zero. These are actual requested-heap reductions, not a transfer of
BSS reservation into heap.

The final GCC 13 amd64 release/LTO library measured 126,195 text bytes,
744 data bytes, 3,147,080 BSS bytes, and 142,992 linked file bytes. The workspace
changes add 1,456 text bytes over the preceding specialization while retaining
a 17,716-byte text saving over the committed mixed checkpoint. BSS is unchanged.

The final mixed suite passed all 52 checks and the final
AddressSanitizer/UndefinedBehaviorSanitizer suite passed all 51 checks
available in that configuration. Standalone float and double passed their
52 and 51 checks before adding the further 4,096-point FFT case.
The complex FFT checks now compare the smaller single-buffer plan bit for bit
with an ordinary plan, including preparation requesting both buffers. The
4,096-point case explicitly covers the float padded-transform threshold.
The 1,080 public-API cases also remained bit identical to the preceding
specialization.

A separate attempt to exclude mixed's unused real-FFT wrappers produced no
useful shared-LTO footprint saving and was removed.

The final native performance comparison used eleven alternating runs per
case, after other builds had finished. Across all 27 cases, the ratio of
median throughput to the preceding specialization was 0.986–1.020. The
geometric mean by quality was 0.996 for Fast, 0.999 for Good, and 1.002 for
High. Stereo 48,000 → 44,100 Hz measured 49.101, 49.201 and 28.073 Mframes/s,
versus 49.404, 49.302 and 27.991 before these workspace changes. This finds no
material native arm64 throughput loss in the measured matrix; native x86_64
performance remains unmeasured. The original FFT processing loop is unchanged;
workspace sizing and preparation contain the new decisions.

## Single-engine mixed build

Mixed previously linked complete float and double engines behind a dispatch
facade. It is now the float engine compiled with `FAAR_WIDE_HIGH`. High's
sensitive kernels then run in native double over float sample planes, where
standalone float uses float2 compensation:

- the paired overlap-save convolution, through a double instantiation of
  `dft_fir_pair.inc` on `faar_rfftd_*`;
- the sparse half-band;
- the polyphase bank, including the exact reversed rows.

The double build already passes float buffers between stages, so these
kernels reproduce its arithmetic exactly. Against the preceding mixed library,
output was bit identical in every case:

- 136 rate pairs (8–192 kHz, ratio up to 8);
- 1, 2 and 3 channels;
- all three qualities, dither off and shaped;
- prepared and unprepared streaming;
- that is 4,896 cases, plus the 480-case `compare_quality.py` gate, both with
  and without `--prepare`.

Standalone float and double stayed bit identical to their previous builds over
the same matrix.

GCC 13 amd64 release/LTO, Berkeley `size` text:

| Build | Before | After | Change |
|---|---:|---:|---:|
| Mixed | 126,195 bytes | 87,066 bytes | −39,129 bytes (−31.0%) |
| Float | 85,983 bytes | 86,127 bytes | +144 bytes |
| Double | 62,806 bytes | 63,062 bytes | +256 bytes |

The float and double deltas are within the LTO variation seen for
semantically neutral edits.

Prepared peak requested heap at High falls by 17–22% across the 48/96/192 kHz,
1/2/8-channel matrix. Stereo 48,000 → 44,100 Hz drops from 5,972,848 to
4,836,376 bytes, because sample planes are float. Fast/Good gain 120–1,456
bytes: the wide fields in each FIR stage, less the removed 16-byte dispatch
handle.

Native arm64 throughput, median of nine alternating runs against the
preceding mixed library, as a ratio of new to old:

- High: 1.002–1.161 across 48/96/192 kHz to 44.1 kHz at 1/2/8 channels, plus
  22.05→44.1, 44.1→48, 44.1→32, 44.1→96 and 88.2→48 kHz.
- Stereo 48,000 → 44,100 Hz: Fast 1.007, Good 0.987, High 1.018.

Leaving out the double reversed rows cost 3–4% on the exact 20-tap routes, so
wide mode keeps them. These macOS timings are a screen only; native x86_64
throughput comes from CI.

The mixed suite passed 52 checks, and the mixed AddressSanitizer and
UndefinedBehaviorSanitizer suite passed 51. Float and double passed 53 and 52.

## Removing the double option

Mixed now matches or beats the double build's throughput at every measured
quality (local arm64, 39 cases: Fast 1.24-1.70x, Good 1.23-1.66x, High
1.01-1.15x), equals its High output bit for bit, and uses less heap. Double's
only remaining advantage was ~24 KB less text and 1 MiB less BSS, which `float`
covers for constrained targets, so the option was removed.


## Sharing FFT workspace across channel pairs

After e23e3003, paired FIR stages within one prefilter borrow the first
stage's FFT plans and workspace. Processing and flushing execute these stages
sequentially. Each stage retains its own overlap, pending input and output,
and stream counters; independent public handles retain separate workspace.
This applies to ordinary float, mixed High double, and compensated float High.
Half-band stages retain private working buffers. Borrowers must be cleared
before their owner, as with the existing shared coefficients.

Paired spectrum setup also transfers ownership of the scaled spectrum rather
than allocating and copying a second spectrum. At stereo 48,000 → 44,100 Hz
High, open peak requested heap falls from 3,918,936 to 3,774,040 bytes;
open live heap is unchanged.

Native macOS arm64 requested heap, mixed, 48,000 → 44,100 Hz, prepared for
8,192 input frames:

| Channels | Quality | Before live bytes | After live bytes | Reduction |
|---|---|---:|---:|---:|
| 3 | Fast | 1,120,396 | 1,071,244 | 4.4% |
| 3 | Good | 1,476,860 | 1,303,292 | 11.8% |
| 3 | High | 8,032,292 | 5,639,460 | 29.8% |
| 8 | Fast | 2,827,800 | 2,680,344 | 5.2% |
| 8 | Good | 3,561,496 | 3,040,792 | 14.6% |
| 8 | High | 16,915,416 | 9,736,920 | 42.4% |

Mono and stereo have only one paired FFT stage, so their prepared live heap
is unchanged. The 48/96/192 kHz → 44.1 kHz matrix at 1/2/3/8 channels and all
qualities returned tracked live heap to zero after close. These measurements
exclude static BSS, allocator metadata and caller buffers.

Seven alternating baseline/candidate timing runs at 48,000 → 44,100 Hz,
1,024-frame input blocks and output capacity, yielded candidate/baseline
median throughput ratios:

| Channels | Fast | Good | High |
|---|---:|---:|---:|
| 2 | 1.003 | 1.000 | 1.000 |
| 8 | 1.001 | 0.998 | 1.019 |

These are local arm64 observations; native x86_64 throughput remains unmeasured.
Both float and mixed passed the 480-case strict PCM comparison against HEAD,
and another 480 cases each with preparation. Shared-FIR regression coverage
also exercises small and padded transforms, expansion/decimation, High,
independent histories, reset, and use of the owner after borrower cleanup.

AddressSanitizer and UndefinedBehaviorSanitizer suites passed 51 checks in
mixed and 52 in float, including the expanded shared-workspace tests.


## Output-tiled half-band experiment

An output-tiled mixed High half-band experiment was rejected: seven alternating
runs per case showed roughly 9–11% slower 192,000 → 44,100 Hz processing at
1/2/8 channels, with no consistent gain elsewhere. The existing half-band
kernel is retained.


## Kaiser setup reuse and direct history handoff

Polyphase setup computes its Kaiser denominator once. Ordinary banks compute
one window value per tap and reuse it across phases; sampled long-prototype
banks retain their phase-dependent numerator. Sinc evaluation, division,
coefficient storage and normalization order are unchanged. The temporary
window uses at most 4 KiB of setup stack and adds no persistent heap.

Native macOS arm64 release/LTO mixed setup, median of seven alternating runs,
16 stereo handles per process. Subsequent-open time excludes the first handle:

| Route | Quality | Before ms/open | After ms/open | Speedup |
|---|---|---:|---:|---:|
| 48,000 → 44,100 | Fast | 0.261 | 0.147 | 1.78× |
| 48,000 → 44,100 | Good | 0.283 | 0.163 | 1.74× |
| 48,000 → 44,100 | High | 0.730 | 0.610 | 1.20× |
| 32,000 → 44,100 | Fast | 1.358 | 0.127 | 10.69× |
| 32,000 → 44,100 | Good | 1.354 | 0.161 | 8.41× |
| 32,000 → 44,100 | High | 1.401 | 0.169 | 8.29× |

First-open timings also improved in this matrix. Additional 44.1→48 kHz and
12,345→45,678 Hz routes improved at all qualities. These measurements isolate
Kaiser reuse by loading saved baseline and candidate libraries through the
same benchmark executable.

The downstream polyphase bank now reserves space for the actual prefilter
FIFO burst and drains directly into its planar history. Boundary padding,
last-sample retention, frame counters and filtering retain their prior order.
Process and flush both use this handoff; routes without a downstream bank
retain their intermediate output buffer. This removes one sample copy and
one prepared intermediate buffer per staged route.

Mixed live requested heap at 48,000 → 44,100 Hz, prepared for 8,192 frames,
relative to the preceding committed code:

| Channels | Quality | Before bytes | After bytes | Reduction |
|---|---|---:|---:|---:|
| 2 | Fast | 749,184 | 552,576 | 26.2% |
| 2 | Good | 955,552 | 758,944 | 20.6% |
| 2 | High | 4,819,728 | 4,623,120 | 4.1% |
| 8 | Fast | 2,680,344 | 1,893,912 | 29.3% |
| 8 | Good | 3,040,792 | 2,254,360 | 25.9% |
| 8 | High | 9,736,920 | 8,950,488 | 8.1% |

The 72-case float/mixed heap matrix at 48/96/192 kHz → 44.1 kHz,
1/2/3/8 channels and all qualities returned all tracked allocations on close.
Kaiser reuse and the combined changes each passed strict PCM comparisons in
both arithmetic policies: 480 normal cases and 480 prepared cases per policy.
The combined mixed suite passed 102 checks with one skip; float passed 53.
Mixed AddressSanitizer and UndefinedBehaviorSanitizer passed 51 checks.


Isolating the history handoff against the saved Kaiser-only library, seven
alternating native arm64 runs per case at 1,024-frame input/output blocks
found candidate/baseline throughput ratios of 1.002–1.037 (median 1.018)
across 48/96/192 kHz → 44.1 kHz, 2/8 channels, and all qualities. An unchanged
no-bank 48→24 kHz High route screened at 1.034 for stereo and 1.027 for eight
channels. These are local observations, not native x86_64 measurements.

Float AddressSanitizer and UndefinedBehaviorSanitizer passed 52 checks. The
expanded prefilter regression test additionally verifies direct drains into
separate channel destinations, partial drains and pending-frame accounting,
centered convolution, and repeated flush across short and long filters and
both boundary policies. It passed in both release modes and float sanitizers.


## Reusable immutable filter plans

`faar_design_open()` resolves the full configuration once. Its internal design
retains stage geometry, filter spectra, sparse half-band coefficients,
polyphase banks and immutable FFT twiddles. Temporary mutable setup buffers
are released before returning. `faar_resampler_open_from_design()` allocates
independent histories, queues and FFT work, and borrows only immutable arrays.
Channel pairs within each stream still share their serialized workspace.

Streams retain the plan through an atomic reference count. Closing the caller's
plan reference before processing or closing its streams is supported. Without
atomics, plan/stream open and close operations require caller serialization;
independent processing remains concurrent. There is no global filter-design
cache, and ordinary `faar_resampler_open()` remains available.

Native macOS arm64 release/LTO, requested live heap for 16 simultaneous handles,
48,000 → 44,100 Hz. The plan column includes the retained design and all streams:

| Arithmetic | Channels | Quality | Ordinary bytes | Plan + streams bytes | Reduction |
|---|---:|---|---:|---:|---:|
| Mixed | 2 | Fast | 2,827,904 | 2,221,512 | 21.4% |
| Mixed | 2 | Good | 5,893,760 | 5,041,608 | 14.5% |
| Mixed | 2 | High | 60,384,896 | 51,799,944 | 14.2% |
| Mixed | 8 | High | 88,866,944 | 80,283,192 | 9.7% |
| Float | 2 | High | 38,492,288 | 26,335,144 | 31.6% |
| Float | 8 | High | 66,973,568 | 54,817,576 | 18.2% |

Seven warm repetitions alternate ordinary and plan measurement order:

| Arithmetic | Channels | Quality | Ordinary 16 opens ms | Design ms | 16 plan-stream opens ms |
|---|---:|---|---:|---:|---:|
| Mixed | 2 | Fast | 3.213 | 0.199 | 0.042 |
| Mixed | 2 | Good | 2.761 | 0.173 | 0.039 |
| Mixed | 2 | High | 7.627 | 0.459 | 0.089 |
| Mixed | 8 | High | 7.556 | 0.461 | 0.315 |
| Float | 2 | High | 45.444 | 2.848 | 0.119 |
| Float | 8 | High | 45.910 | 2.858 | 0.418 |

Times are separate medians per phase; include design cost when comparing a
batch of new streams. `bench_faar_design` reproduces these observations with
allocator hooks. It verifies all tracked allocations are returned after each
measurement. Heap excludes allocator metadata, static BSS and caller buffers.
A single stream has little sharing benefit; this API targets repeated opens
and multiple concurrent streams with identical settings.

The new API adds 3,104 bytes to the local mixed Mach-O text section
(60,584 → 63,688); BSS stays at 3,147,008 bytes. Ordinary stereo handles gain
16 requested bytes in the measured route. Existing ordinary output remains bit
identical across 480 normal and 480 prepared strict comparisons. An 18-case
ordinary throughput screen had median candidate/baseline 1.000, range
0.988–1.009, across 48/96/192 kHz → 44.1 kHz at stereo/eight channels and all
qualities. These local arm64 timings do not establish native x86_64 results.

The plan tests cover 384 rate/quality/channel/boundary/dither configurations,
comparing four plan streams with independent normal references in each case,
including preparation and early caller-plan close. Dedicated, generic,
half-band, cascaded and equal-rate routes are included. Allocation failure
sweeps cover High, cascaded Good and generic Fast, with cleanup and plan reuse.
A start gate exercises concurrent plan opens, independent processing and close.

The full default suite passed 105 checks with one skip; float passed 56. The full mixed AddressSanitizer and
UndefinedBehaviorSanitizer suite passed 54 checks, and the three float plan
checks passed under the same sanitizers. The concurrent mixed plan test also
passed ThreadSanitizer.

Prepared plan processing and flushing were separately verified to make no
allocator calls and no frees in both arithmetic modes. The float allocation
and lifecycle test also passed with atomics disabled and the build channel
limit set to one; the single-channel concurrent test passed ThreadSanitizer.
