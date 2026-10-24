# Per-channel shared-spectrum results

Within one multichannel prefilter, corresponding channel pairs now share the
first-stage immutable DFT spectrum or halfband coefficients. Per-channel
history, queues, and FFT workspaces remain private. Teardown clears borrower
stages before the owning stage. This adds no public API, global cache, or
reference counting.

The measurements compare baseline `23094333` with the candidate. Each entry is
the median of seven alternating runs in fresh processes. Open measurements
keep all handles alive and process no PCM. The open-only RSS reduction of up to
69% reflects avoided temporary FFT work, page touching, and allocator
behavior; it is **not** a 69% steady-state memory reduction. Open benchmark RSS
is the whole process's high-water mark, not a live-allocation measurement.

## Open-only measurements

Times are milliseconds and memory is KiB. Values are baseline → candidate.

| Configuration | Handles | Mean subsequent open | Peak process RSS |
|---|---:|---:|---:|
| 1-channel High, 48,000→44,100 | 8 | 0.730 → 0.725 | 24,304 → 24,320 |
| Stereo High, 48,000→44,100 | 8 | 0.782 → 0.762 | 24,256 → 24,272 |
| 3-channel High, 48,000→44,100 | 8 | 1.064 → 0.752 | 45,376 → 24,896 |
| 8-channel High, 48,000→44,100 | 1 | — | 13,152 → 5,616 |
| 8-channel High, 48,000→44,100 | 8 | 1.799 → 0.816 | 88,832 → 27,616 |
| 8-channel Good, 48,000→44,100 | 8 | 0.414 → 0.325 | 14,240 → 8,736 |
| 8-channel High, 192,000→44,100 | 8 | 1.791 → 0.844 | 88,704 → 27,424 |
| 8-channel High, 384,000→44,100 | 1 | — | 5,904 → 3,856 |
| 8-channel High, 47,999→44,100 | 1 | — | 3,616 → 2,960 |

The 1- and 2-channel cases are effectively unchanged because they do not gain
the same per-channel sharing opportunity. The multichannel open-only savings
come from reusing immutable prefilter data; they should not be read as a
steady-state process-memory estimate.

## Processing benchmark

The PCM benchmark processes three four-second streams per invocation. For
8-channel High at 48,000→44,100 with input block 1024 and output capacity 1024,
median throughput was 6.859 → 6.899 million input frames per second, and peak
process RSS was 22,800 → 21,520 KiB (about 1.25 MiB lower). Mono and stereo
throughput were effectively unchanged.

Across the complete throughput matrix, median candidate/baseline ratios were
0.996–1.007. This indicates no material throughput change in the measured
cases.

## Validation

The release suites passed 49 checks in double and 47 in float. The sanitizer
runs passed 48 ASan/UBSan checks and 2 ThreadSanitizer checks. PCM comparisons were
bit-identical in 480 double and 320 float cases, including the added
three-channel matrix. These measurements and comparisons describe the tested
configurations; they are not a universal fidelity guarantee.
