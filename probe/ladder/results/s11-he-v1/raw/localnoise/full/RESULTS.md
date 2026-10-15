# HE-AAC v1 frequency-local noise/INVF experiment

Corpus: 49 clips; rates: 32, 48, 64, 96 kbps.
BD-rate is calculated with the benchmark PCHIP helper using actual packet payload bitrate and Zimtohrli MOS.

| Variant | MOS reuse matches | Mean BD-rate (95% bootstrap CI) | Median BD-rate | Worst clip | Valid clips |
|---|---:|---:|---:|---|---:|
| oneband_static18 | 196 | -0.607% [-1.012%, -0.292%] | -0.116% | ExitMusic.16b48k.wav (+0.167%) | 49/49 |
| oneband_static24 | 196 | -0.625% [-1.033%, -0.304%] | -0.135% | ExitMusic.16b48k.wav (+0.173%) | 49/49 |
| local2_static18 | 0 | -0.205% [-0.654%, +0.180%] | +0.104% | hrp.wav (+2.339%) | 49/49 |
| local2_static24 | 0 | -0.220% [-0.680%, +0.176%] | +0.105% | hrp.wav (+2.380%) | 49/49 |
| local2_adaptive18 | 0 | -0.257% [-0.736%, +0.149%] | +0.148% | 9-Have-big-expensive-car.441.16b48k.wav (+2.082%) | 49/49 |
| local2_joint18 | 0 | +0.004% [-0.716%, +0.772%] | -0.041% | glk.wav (+10.202%) | 49/49 |

## Actual noise tables by selector, variant, and target rate

| Variant | Rate | Selector | Actual nq | QMF band edges |
|---|---:|---:|---:|---|
| oneband_static18 | 32 | 0 | 1 | 31, 54 |
| oneband_static18 | 48 | 0 | 1 | 31, 54 |
| oneband_static18 | 64 | 0 | 1 | 31, 54 |
| oneband_static18 | 96 | 0 | 1 | 31, 54 |
| oneband_static24 | 32 | 0 | 1 | 31, 54 |
| oneband_static24 | 48 | 0 | 1 | 31, 54 |
| oneband_static24 | 64 | 0 | 1 | 31, 54 |
| oneband_static24 | 96 | 0 | 1 | 31, 54 |
| local2_static18 | 32 | 2 | 2 | 31, 37, 54 |
| local2_static18 | 48 | 2 | 2 | 31, 37, 54 |
| local2_static18 | 64 | 2 | 2 | 31, 37, 54 |
| local2_static18 | 96 | 2 | 2 | 31, 37, 54 |
| local2_static24 | 32 | 2 | 2 | 31, 37, 54 |
| local2_static24 | 48 | 2 | 2 | 31, 37, 54 |
| local2_static24 | 64 | 2 | 2 | 31, 37, 54 |
| local2_static24 | 96 | 2 | 2 | 31, 37, 54 |
| local2_adaptive18 | 32 | 2 | 2 | 31, 37, 54 |
| local2_adaptive18 | 48 | 2 | 2 | 31, 37, 54 |
| local2_adaptive18 | 64 | 2 | 2 | 31, 37, 54 |
| local2_adaptive18 | 96 | 2 | 2 | 31, 37, 54 |
| local2_joint18 | 32 | 2 | 2 | 31, 37, 54 |
| local2_joint18 | 48 | 2 | 2 | 31, 37, 54 |
| local2_joint18 | 64 | 2 | 2 | 31, 37, 54 |
| local2_joint18 | 96 | 2 | 2 | 31, 37, 54 |

## Per-rate paired deltas

| Variant | Rate | Baseline MOS | Variant MOS | MOS delta (95% bootstrap CI) | Mean actual bitrate delta |
|---|---:|---:|---:|---:|---:|
| oneband_static18 | 32 kbps | 3.84259 | 3.84775 | +0.00517 [+0.00301, +0.00782] | +0.000% |
| oneband_static18 | 48 kbps | 4.20630 | 4.20965 | +0.00335 [+0.00171, +0.00538] | +0.000% |
| oneband_static18 | 64 kbps | 4.44092 | 4.44459 | +0.00366 [+0.00190, +0.00574] | +0.000% |
| oneband_static18 | 96 kbps | 4.68014 | 4.68425 | +0.00412 [+0.00230, +0.00635] | +0.000% |
| oneband_static24 | 32 kbps | 3.84259 | 3.84787 | +0.00528 [+0.00314, +0.00796] | +0.000% |
| oneband_static24 | 48 kbps | 4.20630 | 4.20986 | +0.00356 [+0.00191, +0.00561] | +0.000% |
| oneband_static24 | 64 kbps | 4.44092 | 4.44453 | +0.00361 [+0.00187, +0.00571] | +0.000% |
| oneband_static24 | 96 kbps | 4.68014 | 4.68432 | +0.00418 [+0.00236, +0.00650] | +0.000% |
| local2_static18 | 32 kbps | 3.84259 | 3.84199 | -0.00060 [-0.00644, +0.00443] | -0.013% |
| local2_static18 | 48 kbps | 4.20630 | 4.20567 | -0.00063 [-0.00444, +0.00332] | +0.007% |
| local2_static18 | 64 kbps | 4.44092 | 4.44339 | +0.00246 [+0.00013, +0.00496] | -0.000% |
| local2_static18 | 96 kbps | 4.68014 | 4.68347 | +0.00333 [+0.00125, +0.00573] | -0.002% |
| local2_static24 | 32 kbps | 3.84259 | 3.84202 | -0.00056 [-0.00639, +0.00446] | -0.013% |
| local2_static24 | 48 kbps | 4.20630 | 4.20570 | -0.00059 [-0.00445, +0.00338] | +0.007% |
| local2_static24 | 64 kbps | 4.44092 | 4.44356 | +0.00264 [+0.00026, +0.00517] | -0.000% |
| local2_static24 | 96 kbps | 4.68014 | 4.68354 | +0.00340 [+0.00130, +0.00587] | -0.002% |
| local2_adaptive18 | 32 kbps | 3.84259 | 3.84229 | -0.00029 [-0.00642, +0.00523] | -0.011% |
| local2_adaptive18 | 48 kbps | 4.20630 | 4.20607 | -0.00022 [-0.00400, +0.00362] | +0.006% |
| local2_adaptive18 | 64 kbps | 4.44092 | 4.44335 | +0.00242 [+0.00006, +0.00486] | +0.001% |
| local2_adaptive18 | 96 kbps | 4.68014 | 4.68307 | +0.00294 [+0.00091, +0.00530] | -0.002% |
| local2_joint18 | 32 kbps | 3.84259 | 3.84201 | -0.00058 [-0.00523, +0.00430] | +0.013% |
| local2_joint18 | 48 kbps | 4.20630 | 4.20565 | -0.00065 [-0.00539, +0.00460] | +0.034% |
| local2_joint18 | 64 kbps | 4.44092 | 4.44317 | +0.00224 [-0.00183, +0.00620] | +0.017% |
| local2_joint18 | 96 kbps | 4.68014 | 4.68179 | +0.00165 [-0.00326, +0.00596] | +0.008% |

The unset candidate control must match the original baseline at both AAC packet and strict FDK-decoded PCM level. Cached MOS is reused only after packet and PCM identity.

Each result records the actual decoder-compatible noise-band count and QMF edges from the encoder trace; the header selector value alone is not treated as a band count.

Size and speed remain untested. These are probe results, not a shipping decision.


## Direct comparisons

Negative BD-rate favors the named candidate; intervals bootstrap over clips.

| Candidate vs reference | Mean BD-rate (95% CI) | Median | Worst clip | Packet ties | PCM ties |
|---|---:|---:|---:|---|---:|---:|
| oneband_static18_vs_baseline_original | -0.607% [-1.012%, -0.292%] | -0.116% | ExitMusic.16b48k.wav (+0.167%) | 0/196 | 0/196 |
| oneband_static24_vs_baseline_original | -0.625% [-1.033%, -0.304%] | -0.135% | ExitMusic.16b48k.wav (+0.173%) | 0/196 | 0/196 |
| local2_static18_vs_baseline_original | -0.205% [-0.654%, +0.180%] | +0.104% | hrp.wav (+2.339%) | 0/196 | 0/196 |
| local2_static18_vs_oneband_static18 | +0.401% [+0.172%, +0.622%] | +0.361% | hrp.wav (+2.554%) | 0/196 | 0/196 |
| local2_static18_vs_oneband_static24 | +0.419% [+0.198%, +0.637%] | +0.377% | hrp.wav (+2.494%) | 0/196 | 0/196 |
| local2_static24_vs_baseline_original | -0.220% [-0.680%, +0.176%] | +0.105% | hrp.wav (+2.380%) | 0/196 | 0/196 |
| local2_static24_vs_oneband_static18 | +0.385% [+0.154%, +0.608%] | +0.346% | hrp.wav (+2.595%) | 0/196 | 0/196 |
| local2_static24_vs_oneband_static24 | +0.403% [+0.179%, +0.622%] | +0.369% | hrp.wav (+2.535%) | 0/196 | 0/196 |
| local2_adaptive18_vs_baseline_original | -0.257% [-0.736%, +0.149%] | +0.148% | 9-Have-big-expensive-car.441.16b48k.wav (+2.082%) | 0/196 | 0/196 |
| local2_adaptive18_vs_oneband_static18 | +0.355% [+0.109%, +0.591%] | +0.364% | 9-Have-big-expensive-car.441.16b48k.wav (+2.596%) | 0/196 | 0/196 |
| local2_adaptive18_vs_oneband_static24 | +0.374% [+0.138%, +0.602%] | +0.377% | 9-Have-big-expensive-car.441.16b48k.wav (+2.615%) | 0/196 | 0/196 |
| local2_joint18_vs_baseline_original | +0.004% [-0.716%, +0.772%] | -0.041% | glk.wav (+10.202%) | 0/196 | 0/196 |
| local2_joint18_vs_oneband_static18 | +0.590% [+0.018%, +1.372%] | +0.204% | glk.wav (+13.882%) | 0/196 | 0/196 |
| local2_joint18_vs_oneband_static24 | +0.611% [+0.027%, +1.404%] | +0.192% | glk.wav (+14.260%) | 0/196 | 0/196 |
| local2_adaptive18_vs_local2_static18 | -0.046% [-0.151%, +0.039%] | +0.000% | girl.16b48k.wav (+0.622%) | 138/196 | 142/196 |
| local2_adaptive18_vs_local2_static24 | -0.030% [-0.130%, +0.049%] | +0.003% | girl.16b48k.wav (+0.695%) | 0/196 | 0/196 |
| local2_joint18_vs_local2_static18 | +0.184% [-0.355%, +0.982%] | -0.124% | glk.wav (+14.173%) | 0/196 | 0/196 |
| local2_joint18_vs_local2_static24 | +0.202% [-0.344%, +1.005%] | -0.158% | glk.wav (+14.428%) | 0/196 | 0/196 |
| local2_joint18_vs_local2_adaptive18 | +0.262% [-0.366%, +1.185%] | -0.161% | glk.wav (+17.004%) | 0/196 | 0/196 |

Per-rate paired MOS deltas and actual payload-rate deltas are machine-readable in `direct-comparisons.json`.
