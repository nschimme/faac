# HE-AAC v1 SBR grid and resolution probe

Corpus: 4 clips; rates: 32, 48, 64, 96 kbps.
BD-rate is calculated with the benchmark PCHIP helper using actual packet payload bitrate and Zimtohrli MOS.

| Variant | MOS reuse matches | Mean BD-rate (95% bootstrap CI) | Median BD-rate | Valid clips |
|---|---:|---:|---:|---:|
| late | 0 | +0.634% [-0.784%, +2.053%] | +0.556% | 4/4 |
| center | 0 | +0.191% [+0.001%, +0.382%] | +0.133% | 4/4 |
| both | 0 | +0.932% [-0.169%, +1.810%] | +1.162% | 4/4 |
| short15 | 0 | -0.000% [-0.000%, +0.000%] | +0.000% | 4/4 |
| short20 | 0 | +0.092% [-0.320%, +0.595%] | +0.000% | 4/4 |
| flat20 | 0 | +0.111% [-0.027%, +0.363%] | -0.003% | 4/4 |
| combined | 0 | +0.928% [-0.172%, +1.804%] | +1.156% | 4/4 |

## Per-rate paired deltas

| Variant | Rate | Baseline MOS | Variant MOS | MOS delta (95% bootstrap CI) | Mean actual bitrate delta |
|---|---:|---:|---:|---:|---:|
| late | 32 kbps | 3.82641 | 3.81823 | -0.00818 [-0.02511, +0.00841] | +0.012% |
| late | 48 kbps | 4.17969 | 4.17417 | -0.00551 [-0.02227, +0.01125] | -0.007% |
| late | 64 kbps | 4.43086 | 4.42691 | -0.00396 [-0.01154, +0.00268] | -0.007% |
| late | 96 kbps | 4.67168 | 4.66724 | -0.00443 [-0.01199, +0.00291] | -0.000% |
| center | 32 kbps | 3.82641 | 3.79269 | -0.03371 [-0.07350, +0.00074] | +0.013% |
| center | 48 kbps | 4.17969 | 4.18404 | +0.00435 [-0.00342, +0.01253] | +0.006% |
| center | 64 kbps | 4.43086 | 4.43032 | -0.00054 [-0.00406, +0.00371] | +0.006% |
| center | 96 kbps | 4.67168 | 4.67337 | +0.00169 [-0.00049, +0.00526] | +0.004% |
| both | 32 kbps | 3.82641 | 3.79237 | -0.03403 [-0.08030, -0.00679] | +0.022% |
| both | 48 kbps | 4.17969 | 4.17303 | -0.00665 [-0.01834, +0.00756] | -0.001% |
| both | 64 kbps | 4.43086 | 4.42903 | -0.00183 [-0.00818, +0.00246] | -0.001% |
| both | 96 kbps | 4.67168 | 4.66906 | -0.00262 [-0.00798, +0.00315] | +0.003% |
| short15 | 32 kbps | 3.82641 | 3.82641 | -0.00000 [-0.00000, +0.00000] | +0.000% |
| short15 | 48 kbps | 4.17969 | 4.17969 | +0.00000 [+0.00000, +0.00001] | +0.000% |
| short15 | 64 kbps | 4.43086 | 4.43086 | +0.00000 [+0.00000, +0.00000] | +0.000% |
| short15 | 96 kbps | 4.67168 | 4.67168 | -0.00000 [-0.00001, +0.00000] | +0.000% |
| short20 | 32 kbps | 3.82641 | 3.81911 | -0.00729 [-0.02352, +0.00164] | -0.001% |
| short20 | 48 kbps | 4.17969 | 4.17964 | -0.00004 [-0.00609, +0.00596] | -0.004% |
| short20 | 64 kbps | 4.43086 | 4.43159 | +0.00073 [-0.00018, +0.00236] | -0.001% |
| short20 | 96 kbps | 4.67168 | 4.67174 | +0.00007 [-0.00005, +0.00025] | -0.001% |
| flat20 | 32 kbps | 3.82641 | 3.81758 | -0.00883 [-0.02631, +0.00016] | -0.004% |
| flat20 | 48 kbps | 4.17969 | 4.18034 | +0.00065 [-0.00016, +0.00212] | -0.002% |
| flat20 | 64 kbps | 4.43086 | 4.43078 | -0.00008 [-0.00257, +0.00231] | -0.001% |
| flat20 | 96 kbps | 4.67168 | 4.67168 | -0.00000 [-0.00088, +0.00087] | -0.000% |
| combined | 32 kbps | 3.82641 | 3.79290 | -0.03351 [-0.08030, -0.00626] | +0.022% |
| combined | 48 kbps | 4.17969 | 4.17326 | -0.00643 [-0.01765, +0.00756] | -0.002% |
| combined | 64 kbps | 4.43086 | 4.42862 | -0.00224 [-0.00818, +0.00246] | +0.000% |
| combined | 96 kbps | 4.67168 | 4.66904 | -0.00264 [-0.00798, +0.00313] | +0.002% |

The unset candidate control must match the original baseline at both AAC packet and strict FDK-decoded PCM level. Cached MOS is reused only after packet and PCM identity.

Grid borders stay within the analyzed frame. Low-resolution selection uses a spectral-flatness constraint and a frequency-only Huffman cost proxy; actual whole-stream bits are charged in BD-rate.

Size and speed remain untested. These are experiment results, not a shipping decision.


## Direct comparisons

Negative BD-rate favors the named candidate; intervals bootstrap over clips.

| Candidate vs reference | Mean BD-rate (95% CI) | Median | Packet ties | PCM ties |
|---|---:|---:|---:|---:|
| late_vs_baseline_original | +0.634% [-0.784%, +2.053%] | +0.556% | 0/16 | 0/16 |
| center_vs_baseline_original | +0.191% [+0.001%, +0.382%] | +0.133% | 4/16 | 4/16 |
| both_vs_baseline_original | +0.932% [-0.169%, +1.810%] | +1.162% | 0/16 | 0/16 |
| short15_vs_baseline_original | -0.000% [-0.000%, +0.000%] | +0.000% | 12/16 | 12/16 |
| short20_vs_baseline_original | +0.092% [-0.320%, +0.595%] | +0.000% | 8/16 | 8/16 |
| flat20_vs_baseline_original | +0.111% [-0.027%, +0.363%] | -0.003% | 7/16 | 7/16 |
| combined_vs_baseline_original | +0.928% [-0.172%, +1.804%] | +1.156% | 0/16 | 0/16 |
| combined_vs_both | -0.004% [-0.013%, +0.000%] | +0.000% | 12/16 | 12/16 |

Per-rate paired MOS deltas and actual payload-rate deltas are machine-readable in `direct-comparisons.json`.
