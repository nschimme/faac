# Adaptive SBR noise-floor experiment

Corpus: 4 clips; rates: 32, 48, 64, 96 kbps.
BD-rate is calculated with the benchmark PCHIP helper using actual packet payload bitrate and Zimtohrli MOS.

| Variant | MOS reuse matches | Mean BD-rate (95% bootstrap CI) | Median BD-rate | Valid clips |
|---|---:|---:|---:|---:|
| static18 | 16 | -0.702% [-1.236%, -0.213%] | -0.658% | 4/4 |
| static24 | 0 | -0.721% [-1.271%, -0.221%] | -0.670% | 4/4 |
| adaptive_wide | 0 | -0.107% [-0.988%, +0.774%] | -0.356% | 4/4 |
| adaptive12 | 0 | -0.107% [-0.988%, +0.774%] | -0.356% | 4/4 |
| adaptive18 | 0 | -0.728% [-1.236%, -0.238%] | -0.710% | 4/4 |
| adaptive18_low_offset | 0 | -0.570% [-0.901%, -0.216%] | -0.664% | 4/4 |
| adaptive18_high_offset | 0 | -0.702% [-1.236%, -0.213%] | -0.658% | 4/4 |

## Per-rate paired deltas

| Variant | Rate | Baseline MOS | Variant MOS | MOS delta (95% bootstrap CI) | Mean actual bitrate delta |
|---|---:|---:|---:|---:|---:|
| static18 | 32 kbps | 3.82641 | 3.83236 | +0.00596 [+0.00064, +0.01127] | +0.000% |
| static18 | 48 kbps | 4.17969 | 4.18386 | +0.00418 [+0.00124, +0.00766] | +0.000% |
| static18 | 64 kbps | 4.43086 | 4.43744 | +0.00658 [+0.00165, +0.01258] | +0.000% |
| static18 | 96 kbps | 4.67168 | 4.67909 | +0.00741 [+0.00218, +0.01240] | +0.000% |
| static24 | 32 kbps | 3.82641 | 3.83245 | +0.00604 [+0.00057, +0.01151] | +0.000% |
| static24 | 48 kbps | 4.17969 | 4.18396 | +0.00428 [+0.00131, +0.00776] | +0.000% |
| static24 | 64 kbps | 4.43086 | 4.43770 | +0.00684 [+0.00172, +0.01325] | +0.000% |
| static24 | 96 kbps | 4.67168 | 4.67915 | +0.00747 [+0.00222, +0.01254] | +0.000% |
| adaptive_wide | 32 kbps | 3.82641 | 3.82607 | -0.00034 [-0.01889, +0.01121] | +0.001% |
| adaptive_wide | 48 kbps | 4.17969 | 4.17820 | -0.00148 [-0.01693, +0.01173] | -0.000% |
| adaptive_wide | 64 kbps | 4.43086 | 4.43457 | +0.00371 [+0.00025, +0.00717] | -0.002% |
| adaptive_wide | 96 kbps | 4.67168 | 4.67670 | +0.00502 [+0.00013, +0.00910] | +0.005% |
| adaptive12 | 32 kbps | 3.82641 | 3.82607 | -0.00034 [-0.01889, +0.01121] | +0.001% |
| adaptive12 | 48 kbps | 4.17969 | 4.17820 | -0.00148 [-0.01693, +0.01173] | -0.000% |
| adaptive12 | 64 kbps | 4.43086 | 4.43457 | +0.00371 [+0.00025, +0.00717] | -0.002% |
| adaptive12 | 96 kbps | 4.67168 | 4.67670 | +0.00502 [+0.00013, +0.00910] | +0.005% |
| adaptive18 | 32 kbps | 3.82641 | 3.83452 | +0.00811 [+0.00064, +0.01558] | +0.001% |
| adaptive18 | 48 kbps | 4.17969 | 4.18432 | +0.00463 [+0.00168, +0.00766] | +0.000% |
| adaptive18 | 64 kbps | 4.43086 | 4.43662 | +0.00576 [+0.00117, +0.01223] | +0.000% |
| adaptive18 | 96 kbps | 4.67168 | 4.67910 | +0.00742 [+0.00218, +0.01243] | +0.000% |
| adaptive18_low_offset | 32 kbps | 3.82641 | 3.83346 | +0.00705 [+0.00064, +0.01414] | +0.001% |
| adaptive18_low_offset | 48 kbps | 4.17969 | 4.18373 | +0.00405 [+0.00168, +0.00642] | -0.003% |
| adaptive18_low_offset | 64 kbps | 4.43086 | 4.43430 | +0.00344 [+0.00117, +0.00527] | -0.003% |
| adaptive18_low_offset | 96 kbps | 4.67168 | 4.67822 | +0.00654 [+0.00130, +0.01243] | +0.004% |
| adaptive18_high_offset | 32 kbps | 3.82641 | 3.83236 | +0.00596 [+0.00064, +0.01127] | +0.000% |
| adaptive18_high_offset | 48 kbps | 4.17969 | 4.18386 | +0.00418 [+0.00124, +0.00766] | +0.000% |
| adaptive18_high_offset | 64 kbps | 4.43086 | 4.43744 | +0.00658 [+0.00165, +0.01258] | +0.000% |
| adaptive18_high_offset | 96 kbps | 4.67168 | 4.67909 | +0.00741 [+0.00218, +0.01240] | +0.000% |

Actual candidate controls use packet plus strict FDK-decoded PCM identity. Baseline and static18 MOS are inherited only when their new artifacts match old measured rows. Static24 and adaptive variants are freshly scored.

Frame noise-level distributions are recorded from encoder stderr tails when present; no output-level distribution is inferred when absent.

Size and speed remain untested. These are experiment results, not a shipping decision.
