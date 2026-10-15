# HE-AAC v1 limiter and static-noise stack experiment

Corpus: 49 clips; rates: 32, 48, 64, 96 kbps.
BD-rate is calculated with the benchmark PCHIP helper using actual packet payload bitrate and Zimtohrli MOS.

| Variant | MOS reuse matches | Mean BD-rate (95% bootstrap CI) | Median BD-rate | Valid clips |
|---|---:|---:|---:|---:|
| limiter | 0 | -0.540% [-0.946%, -0.195%] | -0.361% | 49/49 |
| static18 | 196 | -0.607% [-1.012%, -0.292%] | -0.116% | 49/49 |
| static24 | 196 | -0.625% [-1.033%, -0.304%] | -0.135% | 49/49 |
| stack18 | 0 | -1.107% [-1.626%, -0.667%] | -0.689% | 49/49 |
| stack24 | 0 | -1.117% [-1.643%, -0.676%] | -0.700% | 49/49 |

## Per-rate paired deltas

| Variant | Rate | Baseline MOS | Variant MOS | MOS delta (95% bootstrap CI) | Mean actual bitrate delta |
|---|---:|---:|---:|---:|---:|
| limiter | 32 kbps | 3.84259 | 3.84259 | +0.00000 [+0.00000, +0.00000] | +0.000% |
| limiter | 48 kbps | 4.20630 | 4.21198 | +0.00569 [+0.00164, +0.01096] | -0.018% |
| limiter | 64 kbps | 4.44092 | 4.44525 | +0.00433 [+0.00020, +0.00856] | -0.018% |
| limiter | 96 kbps | 4.68014 | 4.68587 | +0.00573 [+0.00205, +0.01045] | -0.013% |
| static18 | 32 kbps | 3.84259 | 3.84775 | +0.00517 [+0.00301, +0.00782] | +0.000% |
| static18 | 48 kbps | 4.20630 | 4.20965 | +0.00335 [+0.00171, +0.00538] | +0.000% |
| static18 | 64 kbps | 4.44092 | 4.44459 | +0.00366 [+0.00190, +0.00574] | +0.000% |
| static18 | 96 kbps | 4.68014 | 4.68425 | +0.00412 [+0.00230, +0.00635] | +0.000% |
| static24 | 32 kbps | 3.84259 | 3.84787 | +0.00528 [+0.00314, +0.00796] | +0.000% |
| static24 | 48 kbps | 4.20630 | 4.20986 | +0.00356 [+0.00191, +0.00561] | +0.000% |
| static24 | 64 kbps | 4.44092 | 4.44453 | +0.00361 [+0.00187, +0.00571] | +0.000% |
| static24 | 96 kbps | 4.68014 | 4.68432 | +0.00418 [+0.00236, +0.00650] | +0.000% |
| stack18 | 32 kbps | 3.84259 | 3.84775 | +0.00517 [+0.00301, +0.00782] | +0.000% |
| stack18 | 48 kbps | 4.20630 | 4.21526 | +0.00896 [+0.00496, +0.01401] | -0.018% |
| stack18 | 64 kbps | 4.44092 | 4.44865 | +0.00773 [+0.00345, +0.01166] | -0.018% |
| stack18 | 96 kbps | 4.68014 | 4.69011 | +0.00997 [+0.00588, +0.01473] | -0.013% |
| stack24 | 32 kbps | 3.84259 | 3.84787 | +0.00528 [+0.00314, +0.00796] | +0.000% |
| stack24 | 48 kbps | 4.20630 | 4.21524 | +0.00894 [+0.00494, +0.01394] | -0.018% |
| stack24 | 64 kbps | 4.44092 | 4.44874 | +0.00781 [+0.00353, +0.01178] | -0.018% |
| stack24 | 96 kbps | 4.68014 | 4.69025 | +0.01011 [+0.00597, +0.01490] | -0.013% |

The unset candidate control must match the original baseline at both AAC packet and strict FDK-decoded PCM level. Cached MOS is reused only after packet and PCM identity.

The limiter is independently opt-in; static noise defaults to level 12 and the tested overrides are 18 and 24.

Size and speed remain untested. These are experiment results, not a shipping decision.


## Direct comparisons

Negative BD-rate favors the named candidate; intervals bootstrap over clips.

| Candidate vs reference | Mean BD-rate (95% CI) | Median | Packet ties | PCM ties |
|---|---:|---:|---:|---:|
| limiter_vs_baseline_original | -0.540% [-0.946%, -0.195%] | -0.361% | 49/196 | 49/196 |
| static18_vs_baseline_original | -0.607% [-1.012%, -0.292%] | -0.116% | 0/196 | 0/196 |
| static24_vs_baseline_original | -0.625% [-1.033%, -0.304%] | -0.135% | 0/196 | 0/196 |
| stack18_vs_baseline_original | -1.107% [-1.626%, -0.667%] | -0.689% | 0/196 | 0/196 |
| stack24_vs_baseline_original | -1.117% [-1.643%, -0.676%] | -0.700% | 0/196 | 0/196 |
| stack18_vs_static18 | -0.531% [-0.948%, -0.175%] | -0.331% | 49/196 | 49/196 |
| stack18_vs_limiter | -0.592% [-0.997%, -0.292%] | -0.081% | 0/196 | 0/196 |
| stack24_vs_static24 | -0.526% [-0.946%, -0.172%] | -0.325% | 49/196 | 49/196 |
| stack24_vs_limiter | -0.603% [-1.009%, -0.300%] | -0.109% | 0/196 | 0/196 |

Per-rate paired MOS deltas and actual payload-rate deltas are machine-readable in `direct-comparisons.json`.
