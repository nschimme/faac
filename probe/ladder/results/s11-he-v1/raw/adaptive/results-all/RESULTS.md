# Adaptive SBR noise-floor experiment

Corpus: 49 clips; rates: 32, 48, 64, 96 kbps.
BD-rate is calculated with the benchmark PCHIP helper using actual packet payload bitrate and Zimtohrli MOS.

| Variant | MOS reuse matches | Mean BD-rate (95% bootstrap CI) | Median BD-rate | Valid clips |
|---|---:|---:|---:|---:|
| static18 | 196 | -0.607% [-1.012%, -0.292%] | -0.116% | 49/49 |
| static24 | 0 | -0.625% [-1.033%, -0.304%] | -0.135% | 49/49 |
| adaptive18 | 0 | -0.538% [-0.912%, -0.244%] | -0.080% | 49/49 |

## Per-rate paired deltas

| Variant | Rate | Baseline MOS | Variant MOS | MOS delta (95% bootstrap CI) | Mean actual bitrate delta |
|---|---:|---:|---:|---:|---:|
| static18 | 32 kbps | 3.84259 | 3.84775 | +0.00517 [+0.00301, +0.00782] | +0.000% |
| static18 | 48 kbps | 4.20630 | 4.20965 | +0.00335 [+0.00171, +0.00538] | +0.000% |
| static18 | 64 kbps | 4.44092 | 4.44459 | +0.00366 [+0.00190, +0.00574] | +0.000% |
| static18 | 96 kbps | 4.68014 | 4.68425 | +0.00412 [+0.00230, +0.00635] | +0.000% |
| static24 | 32 kbps | 3.84259 | 3.84787 | +0.00528 [+0.00314, +0.00796] | +0.000% |
| static24 | 48 kbps | 4.20630 | 4.20986 | +0.00356 [+0.00191, +0.00561] | +0.000% |
| static24 | 64 kbps | 4.44092 | 4.44453 | +0.00361 [+0.00187, +0.00571] | +0.000% |
| static24 | 96 kbps | 4.68014 | 4.68432 | +0.00418 [+0.00236, +0.00650] | +0.000% |
| adaptive18 | 32 kbps | 3.84259 | 3.84715 | +0.00457 [+0.00269, +0.00681] | -0.001% |
| adaptive18 | 48 kbps | 4.20630 | 4.20905 | +0.00275 [+0.00085, +0.00488] | +0.001% |
| adaptive18 | 64 kbps | 4.44092 | 4.44433 | +0.00341 [+0.00178, +0.00542] | -0.000% |
| adaptive18 | 96 kbps | 4.68014 | 4.68445 | +0.00431 [+0.00243, +0.00661] | +0.000% |

Actual candidate controls use packet plus strict FDK-decoded PCM identity. Baseline and static18 MOS are inherited only when their new artifacts match old measured rows. Static24 and adaptive variants are freshly scored.

Frame noise-level distributions are recorded from encoder stderr tails when present; no output-level distribution is inferred when absent.

Size and speed remain untested. These are experiment results, not a shipping decision.
