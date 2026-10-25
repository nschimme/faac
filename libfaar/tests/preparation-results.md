# Bounded streaming preparation: validation and timing

`faar_resampler_prepare(r, max_input_frames)` reserves storage for a nonzero
input bound before input or flush begins. Prepared process, drain, and flush
calls make no heap allocations or frees. Drain with `process(NULL, 0, ...)`
using nonzero output capacity until it returns zero frames before adding more
input; otherwise the bounded output queue can exhaust and return
`FAAR_ERR_INSUFFICIENT_MEM` or `FAAR_ERR_PROCESSING`. Repreparation is allowed before
streaming; if it fails, the previous mode and bound remain usable. The normal
unprepared flow is unchanged.

The latency comparison uses baseline `2d4d0fb7` and the final candidate. It is
the median of seven alternating fresh-process runs per configuration. The
table shows regular → prepared median p99 API-call time, median of each run's
maximum API-call time, peak process RSS, and preparation time. Times are
microseconds unless marked milliseconds; RSS is KiB.

| Configuration (input→output, channels, quality, block/capacity) | p99 | Median max | Peak process RSS | Prepare |
|---|---:|---:|---:|---:|
| 48,000→44,100, 2ch, High, 64/17 | 1 → 1 µs | 336 → 272 µs | 7,648 → 7,664 | 0.164 ms |
| 48,000→44,100, 8ch, High, 64/17 | 2 → 2 µs | 1,657 → 1,131 µs | 19,456 → 19,024 | 0.926 ms |
| 48,000→44,100, 8ch, High, 1024/1024 | 1,080 → 1,059 µs | 1,762 → 1,276 µs | 19,536 → 19,024 | 0.940 ms |
| 192,000→44,100, 8ch, Good, 64/17 | 7 → 7 µs | 237 → 175 µs | 5,296 → 5,584 | 0.200 ms |
| 384,000→44,100, 3ch, High, 1024/17 | 13 → 13 µs | 237 → 235 µs | 4,896 → 5,184 | 0.131 ms |
| 47,999→44,100, 2ch, High, 64/17 | 1 → 1 µs | 64 → 43 µs | 2,992 → 3,088 | 0.037 ms |
| 48,000→48,000, 2ch, Fast, 64/64 | 1 → 1 µs | 2 → 10 µs | 1,904 → 1,904 | 0.002 ms |

These numbers are individual native API-call timings, not total host callback
durations. The CSV reports microseconds to three decimal places, so timer
resolution and rounding affect small values. Maximums vary with FFT block
bursts and system scheduling; preparation does not remove FFT costs or provide
a real-time deadline guarantee. The main benefit is the zero-heap-call
contract. The remaining prepared-versus-regular peak RSS differences after
the tighter reservation ranged from −512 to +288 KiB in the sampled profiles.
Preparation itself took about 0.002–0.940 ms on this system. RSS is the
whole-process high-water mark, not a library live-allocation count.

The configured suites passed 50 checks in double, 48 in float, 49 under
ASan/UBSan, and 2 selected checks under ThreadSanitizer. The preparation test forces allocator failure
after preparation and verifies zero allocation/free calls during prepared
processing and flush, checks unprepared allocation snapshots (0–19 calls in
these cases), injects preparation failures and retries, tests failed
repreparation and queue exhaustion, and confirms PCM output. The regression
comparison reported 1,600 bit-identical comparisons: 800 prepared and 800
regular, each comprising 480 double and 320 float cases.
These are finite regression checks, not a universal fidelity guarantee.
