# HE-AAC v1 SBR grid and resolution probe

Corpus: 49 clips; rates: 32, 48, 64, 96 kbps.
BD-rate is calculated with the benchmark PCHIP helper using actual packet payload bitrate and Zimtohrli MOS.

| Variant | MOS reuse matches | Mean BD-rate (95% bootstrap CI) | Median BD-rate | Valid clips |
|---|---:|---:|---:|---:|
| late | 0 | +0.588% [+0.282%, +0.938%] | +0.281% | 49/49 |
| center | 0 | -0.084% [-0.227%, +0.060%] | -0.097% | 49/49 |
| short20 | 0 | +0.022% [-0.071%, +0.140%] | +0.000% | 49/49 |
| flat20 | 0 | -0.036% [-0.135%, +0.066%] | -0.075% | 49/49 |

## Per-rate paired deltas

| Variant | Rate | Baseline MOS | Variant MOS | MOS delta (95% bootstrap CI) | Mean actual bitrate delta |
|---|---:|---:|---:|---:|---:|
| late | 32 kbps | 3.84259 | 3.83832 | -0.00427 [-0.00841, -0.00006] | -0.000% |
| late | 48 kbps | 4.20630 | 4.20164 | -0.00466 [-0.00800, -0.00156] | +0.001% |
| late | 64 kbps | 4.44092 | 4.43630 | -0.00462 [-0.00723, -0.00225] | +0.000% |
| late | 96 kbps | 4.68014 | 4.67622 | -0.00392 [-0.00689, -0.00162] | -0.000% |
| center | 32 kbps | 3.84259 | 3.83976 | -0.00282 [-0.00835, +0.00124] | +0.008% |
| center | 48 kbps | 4.20630 | 4.20813 | +0.00183 [-0.00015, +0.00381] | +0.004% |
| center | 64 kbps | 4.44092 | 4.44224 | +0.00132 [-0.00016, +0.00283] | +0.005% |
| center | 96 kbps | 4.68014 | 4.68094 | +0.00080 [-0.00003, +0.00161] | +0.003% |
| short20 | 32 kbps | 3.84259 | 3.84223 | -0.00036 [-0.00226, +0.00123] | -0.002% |
| short20 | 48 kbps | 4.20630 | 4.20669 | +0.00039 [-0.00057, +0.00147] | -0.002% |
| short20 | 64 kbps | 4.44092 | 4.44091 | -0.00001 [-0.00068, +0.00069] | +0.000% |
| short20 | 96 kbps | 4.68014 | 4.68001 | -0.00013 [-0.00056, +0.00014] | +0.000% |
| flat20 | 32 kbps | 3.84259 | 3.84370 | +0.00111 [-0.00146, +0.00362] | -0.019% |
| flat20 | 48 kbps | 4.20630 | 4.20723 | +0.00093 [-0.00035, +0.00228] | -0.004% |
| flat20 | 64 kbps | 4.44092 | 4.44075 | -0.00017 [-0.00113, +0.00076] | -0.003% |
| flat20 | 96 kbps | 4.68014 | 4.67959 | -0.00055 [-0.00109, +0.00004] | -0.002% |

The unset candidate control must match the original baseline at both AAC packet and strict FDK-decoded PCM level. Cached MOS is reused only after packet and PCM identity.

Grid borders stay within the analyzed frame. Low-resolution selection uses a spectral-flatness constraint and a frequency-only Huffman cost proxy; actual whole-stream bits are charged in BD-rate.

Size and speed remain untested. These are experiment results, not a shipping decision.


## Direct comparisons

Negative BD-rate favors the named candidate; intervals bootstrap over clips.

| Candidate vs reference | Mean BD-rate (95% CI) | Median | Packet ties | PCM ties |
|---|---:|---:|---:|---:|
| late_vs_baseline_original | +0.588% [+0.282%, +0.938%] | +0.281% | 0/196 | 0/196 |
| center_vs_baseline_original | -0.084% [-0.227%, +0.060%] | -0.097% | 20/196 | 20/196 |
| short20_vs_baseline_original | +0.022% [-0.071%, +0.140%] | +0.000% | 88/196 | 93/196 |
| flat20_vs_baseline_original | -0.036% [-0.135%, +0.066%] | -0.075% | 22/196 | 22/196 |

Per-rate paired MOS deltas and actual payload-rate deltas are machine-readable in `direct-comparisons.json`.
