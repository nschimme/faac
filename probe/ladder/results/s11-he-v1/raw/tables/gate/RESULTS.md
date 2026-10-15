# HE-AAC v1 frequency-table experiment

Corpus: 4 clips; rates: 32, 48, 64, 96 kbps.
BD-rate is calculated with the benchmark PCHIP helper using actual packet payload bitrate and Zimtohrli MOS.

| Variant | MOS reuse matches | Mean BD-rate (95% bootstrap CI) | Median BD-rate | Valid clips |
|---|---:|---:|---:|---:|
| scale1 | 0 | +0.853% [+0.086%, +1.558%] | +0.916% | 4/4 |
| scale2 | 0 | +0.820% [+0.364%, +1.276%] | +0.725% | 4/4 |
| scale3 | 0 | +0.000% [+0.000%, +0.000%] | +0.000% | 4/4 |
| stop16 | 0 | +2.477% [+0.191%, +6.422%] | +0.819% | 4/4 |
| stop18 | 0 | +0.660% [-0.098%, +1.498%] | +0.580% | 4/4 |

## Per-rate paired deltas

| Variant | Rate | Baseline MOS | Variant MOS | MOS delta (95% bootstrap CI) | Mean actual bitrate delta |
|---|---:|---:|---:|---:|---:|
| scale1 | 32 kbps | 3.82641 | 3.78867 | -0.03773 [-0.06722, -0.01807] | +0.028% |
| scale1 | 48 kbps | 4.17969 | 4.17050 | -0.00919 [-0.02089, +0.00252] | +0.062% |
| scale1 | 64 kbps | 4.43086 | 4.43467 | +0.00381 [-0.00232, +0.00995] | +0.055% |
| scale1 | 96 kbps | 4.67168 | 4.67480 | +0.00313 [+0.00124, +0.00565] | +0.029% |
| scale2 | 32 kbps | 3.82641 | 3.80797 | -0.01844 [-0.04841, +0.00956] | +0.015% |
| scale2 | 48 kbps | 4.17969 | 4.17158 | -0.00811 [-0.01550, -0.00071] | +0.046% |
| scale2 | 64 kbps | 4.43086 | 4.42819 | -0.00267 [-0.00911, +0.00232] | +0.026% |
| scale2 | 96 kbps | 4.67168 | 4.67215 | +0.00047 [-0.00420, +0.00468] | +0.012% |
| scale3 | 32 kbps | 3.82641 | 3.82641 | +0.00000 [+0.00000, +0.00000] | +0.000% |
| scale3 | 48 kbps | 4.17969 | 4.17969 | +0.00000 [+0.00000, +0.00000] | +0.000% |
| scale3 | 64 kbps | 4.43086 | 4.43086 | +0.00000 [+0.00000, +0.00000] | +0.000% |
| scale3 | 96 kbps | 4.67168 | 4.67168 | +0.00000 [+0.00000, +0.00000] | +0.000% |
| stop16 | 32 kbps | 3.82641 | 3.81700 | -0.00940 [-0.03713, +0.01832] | -0.079% |
| stop16 | 48 kbps | 4.17969 | 4.16295 | -0.01674 [-0.04135, -0.00122] | -0.054% |
| stop16 | 64 kbps | 4.43086 | 4.40512 | -0.02574 [-0.06692, -0.00361] | -0.049% |
| stop16 | 96 kbps | 4.67168 | 4.65277 | -0.01891 [-0.04184, -0.00435] | -0.024% |
| stop18 | 32 kbps | 3.82641 | 3.80604 | -0.02037 [-0.02972, -0.00626] | -0.004% |
| stop18 | 48 kbps | 4.17969 | 4.17982 | +0.00013 [-0.00633, +0.00712] | -0.005% |
| stop18 | 64 kbps | 4.43086 | 4.42676 | -0.00410 [-0.01576, +0.00423] | -0.000% |
| stop18 | 96 kbps | 4.67168 | 4.66549 | -0.00619 [-0.01755, +0.00057] | +0.002% |

The unset candidate control must match the original baseline at both AAC packet and strict FDK-decoded PCM level. Cached MOS is reused only after packet and PCM identity.

Scale values 1, 2, and 3 and stop targets 16 kHz and 18 kHz are compared against the unset candidate control.

Size and speed remain untested. These are experiment results, not a shipping decision.


## Direct comparisons

Negative BD-rate favors the named candidate; intervals bootstrap over clips.

| Candidate vs reference | Mean BD-rate (95% CI) | Median | Packet ties | PCM ties |
|---|---:|---:|---:|---:|
| scale1_vs_baseline_original | +0.853% [+0.086%, +1.558%] | +0.916% | 0/16 | 0/16 |
| scale2_vs_baseline_original | +0.820% [+0.364%, +1.276%] | +0.725% | 0/16 | 0/16 |
| scale3_vs_baseline_original | +0.000% [+0.000%, +0.000%] | +0.000% | 16/16 | 16/16 |
| stop16_vs_baseline_original | +2.477% [+0.191%, +6.422%] | +0.819% | 0/16 | 0/16 |
| stop18_vs_baseline_original | +0.660% [-0.098%, +1.498%] | +0.580% | 0/16 | 0/16 |

Per-rate paired MOS deltas and actual payload-rate deltas are machine-readable in `direct-comparisons.json`.
