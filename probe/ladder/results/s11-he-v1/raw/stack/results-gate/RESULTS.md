# HE-AAC v1 limiter and static-noise stack experiment

Corpus: 4 clips; rates: 32, 48, 64, 96 kbps.
BD-rate is calculated with the benchmark PCHIP helper using actual packet payload bitrate and Zimtohrli MOS.

| Variant | MOS reuse matches | Mean BD-rate (95% bootstrap CI) | Median BD-rate | Valid clips |
|---|---:|---:|---:|---:|
| limiter | 0 | -0.227% [-0.638%, +0.184%] | -0.240% | 4/4 |
| static18 | 16 | -0.702% [-1.236%, -0.213%] | -0.658% | 4/4 |
| static24 | 16 | -0.721% [-1.271%, -0.221%] | -0.670% | 4/4 |
| stack18 | 0 | -0.848% [-1.535%, -0.303%] | -0.707% | 4/4 |
| stack24 | 0 | -0.856% [-1.559%, -0.302%] | -0.707% | 4/4 |

## Per-rate paired deltas

| Variant | Rate | Baseline MOS | Variant MOS | MOS delta (95% bootstrap CI) | Mean actual bitrate delta |
|---|---:|---:|---:|---:|---:|
| limiter | 32 kbps | 3.82641 | 3.82641 | +0.00000 [+0.00000, +0.00000] | +0.000% |
| limiter | 48 kbps | 4.17969 | 4.17935 | -0.00034 [-0.00979, +0.00580] | -0.032% |
| limiter | 64 kbps | 4.43086 | 4.43518 | +0.00432 [-0.00198, +0.00907] | -0.026% |
| limiter | 96 kbps | 4.67168 | 4.67310 | +0.00142 [-0.00280, +0.00404] | -0.011% |
| static18 | 32 kbps | 3.82641 | 3.83236 | +0.00596 [+0.00064, +0.01127] | +0.000% |
| static18 | 48 kbps | 4.17969 | 4.18386 | +0.00418 [+0.00124, +0.00766] | +0.000% |
| static18 | 64 kbps | 4.43086 | 4.43744 | +0.00658 [+0.00165, +0.01258] | +0.000% |
| static18 | 96 kbps | 4.67168 | 4.67909 | +0.00741 [+0.00218, +0.01240] | +0.000% |
| static24 | 32 kbps | 3.82641 | 3.83245 | +0.00604 [+0.00057, +0.01151] | +0.000% |
| static24 | 48 kbps | 4.17969 | 4.18396 | +0.00428 [+0.00131, +0.00776] | +0.000% |
| static24 | 64 kbps | 4.43086 | 4.43770 | +0.00684 [+0.00172, +0.01325] | +0.000% |
| static24 | 96 kbps | 4.67168 | 4.67915 | +0.00747 [+0.00222, +0.01254] | +0.000% |
| stack18 | 32 kbps | 3.82641 | 3.83236 | +0.00596 [+0.00064, +0.01127] | +0.000% |
| stack18 | 48 kbps | 4.17969 | 4.18255 | +0.00286 [-0.00687, +0.01111] | -0.032% |
| stack18 | 64 kbps | 4.43086 | 4.44098 | +0.01012 [+0.00394, +0.01546] | -0.026% |
| stack18 | 96 kbps | 4.67168 | 4.68034 | +0.00866 [+0.00596, +0.01065] | -0.011% |
| stack24 | 32 kbps | 3.82641 | 3.83245 | +0.00604 [+0.00057, +0.01151] | +0.000% |
| stack24 | 48 kbps | 4.17969 | 4.18255 | +0.00287 [-0.00680, +0.01112] | -0.032% |
| stack24 | 64 kbps | 4.43086 | 4.44103 | +0.01017 [+0.00398, +0.01560] | -0.026% |
| stack24 | 96 kbps | 4.67168 | 4.68059 | +0.00891 [+0.00597, +0.01113] | -0.011% |

The unset candidate control must match the original baseline at both AAC packet and strict FDK-decoded PCM level. Cached MOS is reused only after packet and PCM identity.

The limiter is independently opt-in; static noise defaults to level 12 and the tested overrides are 18 and 24.

Size and speed remain untested. These are experiment results, not a shipping decision.


## Direct comparisons

Negative BD-rate favors the named candidate; intervals bootstrap over clips.

| Candidate vs reference | Mean BD-rate (95% CI) | Median | Packet ties | PCM ties |
|---|---:|---:|---:|---:|
| limiter_vs_baseline_original | -0.227% [-0.638%, +0.184%] | -0.240% | 4/16 | 4/16 |
| static18_vs_baseline_original | -0.702% [-1.236%, -0.213%] | -0.658% | 0/16 | 0/16 |
| static24_vs_baseline_original | -0.721% [-1.271%, -0.221%] | -0.670% | 0/16 | 0/16 |
| stack18_vs_baseline_original | -0.848% [-1.535%, -0.303%] | -0.707% | 0/16 | 0/16 |
| stack24_vs_baseline_original | -0.856% [-1.559%, -0.302%] | -0.707% | 0/16 | 0/16 |
| stack18_vs_static18 | -0.146% [-0.526%, +0.235%] | -0.083% | 4/16 | 4/16 |
| stack18_vs_limiter | -0.620% [-1.045%, -0.208%] | -0.607% | 0/16 | 0/16 |
| stack24_vs_static24 | -0.135% [-0.511%, +0.240%] | -0.076% | 4/16 | 4/16 |
| stack24_vs_limiter | -0.628% [-1.066%, -0.204%] | -0.613% | 0/16 | 0/16 |

Per-rate paired MOS deltas and actual payload-rate deltas are machine-readable in `direct-comparisons.json`.
