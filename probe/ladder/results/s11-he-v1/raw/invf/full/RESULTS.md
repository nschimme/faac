# HE-AAC v1 inverse-filtering and noise experiment

Corpus: 49 clips; rates: 32, 48, 64, 96 kbps.
BD-rate is calculated with the benchmark PCHIP helper using actual packet payload bitrate and Zimtohrli MOS.

| Variant | MOS reuse matches | Mean BD-rate (95% bootstrap CI) | Median BD-rate | Valid clips |
|---|---:|---:|---:|---:|
| static18 | 196 | -0.607% [-1.012%, -0.292%] | -0.116% | 49/49 |
| static24 | 196 | -0.625% [-1.033%, -0.304%] | -0.135% | 49/49 |
| noise_only18 | 196 | -0.538% [-0.912%, -0.244%] | -0.080% | 49/49 |
| invf_raw12 | 0 | +0.269% [-0.282%, +1.075%] | -0.062% | 49/49 |
| invf_db12 | 0 | +0.299% [-0.255%, +1.057%] | -0.082% | 49/49 |
| invf_raw24 | 0 | -0.323% [-1.027%, +0.431%] | -0.273% | 49/49 |
| invf_db24 | 0 | -0.277% [-1.017%, +0.494%] | -0.247% | 49/49 |
| joint_raw12 | 0 | -0.268% [-0.975%, +0.506%] | -0.206% | 49/49 |
| joint_db12 | 0 | -0.099% [-0.853%, +0.732%] | -0.231% | 49/49 |

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
| noise_only18 | 32 kbps | 3.84259 | 3.84715 | +0.00457 [+0.00269, +0.00681] | -0.001% |
| noise_only18 | 48 kbps | 4.20630 | 4.20905 | +0.00275 [+0.00085, +0.00488] | +0.001% |
| noise_only18 | 64 kbps | 4.44092 | 4.44433 | +0.00341 [+0.00178, +0.00542] | -0.000% |
| noise_only18 | 96 kbps | 4.68014 | 4.68445 | +0.00431 [+0.00243, +0.00661] | +0.000% |
| invf_raw12 | 32 kbps | 3.84259 | 3.84085 | -0.00173 [-0.00495, +0.00175] | +0.005% |
| invf_raw12 | 48 kbps | 4.20630 | 4.20734 | +0.00105 [-0.00334, +0.00525] | +0.011% |
| invf_raw12 | 64 kbps | 4.44092 | 4.43902 | -0.00191 [-0.00623, +0.00145] | +0.009% |
| invf_raw12 | 96 kbps | 4.68014 | 4.67836 | -0.00178 [-0.00584, +0.00155] | +0.006% |
| invf_db12 | 32 kbps | 3.84259 | 3.84147 | -0.00111 [-0.00472, +0.00269] | +0.021% |
| invf_db12 | 48 kbps | 4.20630 | 4.20611 | -0.00019 [-0.00420, +0.00377] | +0.019% |
| invf_db12 | 64 kbps | 4.44092 | 4.44002 | -0.00090 [-0.00489, +0.00248] | +0.013% |
| invf_db12 | 96 kbps | 4.68014 | 4.67802 | -0.00212 [-0.00647, +0.00141] | +0.008% |
| invf_raw24 | 32 kbps | 3.84259 | 3.84582 | +0.00323 [-0.00046, +0.00713] | +0.005% |
| invf_raw24 | 48 kbps | 4.20630 | 4.21090 | +0.00460 [+0.00011, +0.00917] | +0.011% |
| invf_raw24 | 64 kbps | 4.44092 | 4.44278 | +0.00185 [-0.00279, +0.00612] | +0.009% |
| invf_raw24 | 96 kbps | 4.68014 | 4.68275 | +0.00262 [-0.00228, +0.00696] | +0.006% |
| invf_db24 | 32 kbps | 3.84259 | 3.84625 | +0.00366 [-0.00074, +0.00832] | +0.021% |
| invf_db24 | 48 kbps | 4.20630 | 4.20930 | +0.00300 [-0.00151, +0.00755] | +0.019% |
| invf_db24 | 64 kbps | 4.44092 | 4.44387 | +0.00294 [-0.00144, +0.00706] | +0.013% |
| invf_db24 | 96 kbps | 4.68014 | 4.68262 | +0.00248 [-0.00263, +0.00723] | +0.008% |
| joint_raw12 | 32 kbps | 3.84259 | 3.84606 | +0.00348 [-0.00082, +0.00822] | +0.003% |
| joint_raw12 | 48 kbps | 4.20630 | 4.21014 | +0.00384 [-0.00066, +0.00840] | +0.016% |
| joint_raw12 | 64 kbps | 4.44092 | 4.44336 | +0.00244 [-0.00146, +0.00612] | +0.010% |
| joint_raw12 | 96 kbps | 4.68014 | 4.68183 | +0.00170 [-0.00299, +0.00585] | +0.007% |
| joint_db12 | 32 kbps | 3.84259 | 3.84620 | +0.00361 [-0.00083, +0.00817] | +0.020% |
| joint_db12 | 48 kbps | 4.20630 | 4.20769 | +0.00140 [-0.00326, +0.00616] | +0.023% |
| joint_db12 | 64 kbps | 4.44092 | 4.44250 | +0.00158 [-0.00287, +0.00571] | +0.011% |
| joint_db12 | 96 kbps | 4.68014 | 4.68201 | +0.00187 [-0.00315, +0.00633] | +0.009% |

The unset candidate control must match the original baseline at both AAC packet and strict FDK-decoded PCM level. Cached MOS is reused only after packet and PCM identity.

Raw inverse-filtering tonality and explicit 10*log10(T) are exploratory alternatives, not a normative calibration. One noise band is retained.

Size and speed remain untested. These are experiment results, not a shipping decision.


## Direct comparisons

Negative BD-rate favors the named candidate; intervals bootstrap over clips.

| Candidate vs reference | Mean BD-rate (95% CI) | Median | Packet ties | PCM ties |
|---|---:|---:|---:|---:|
| static18_vs_baseline_original | -0.607% [-1.012%, -0.292%] | -0.116% | 0/196 | 0/196 |
| static24_vs_baseline_original | -0.625% [-1.033%, -0.304%] | -0.135% | 0/196 | 0/196 |
| noise_only18_vs_static18 | +0.067% [+0.011%, +0.142%] | +0.000% | 151/196 | 159/196 |
| invf_raw12_vs_baseline_original | +0.269% [-0.282%, +1.075%] | -0.062% | 0/196 | 0/196 |
| invf_db12_vs_baseline_original | +0.299% [-0.255%, +1.057%] | -0.082% | 0/196 | 0/196 |
| invf_raw24_vs_static24 | +0.275% [-0.294%, +1.086%] | -0.070% | 0/196 | 0/196 |
| invf_db24_vs_static24 | +0.313% [-0.258%, +1.101%] | -0.065% | 0/196 | 0/196 |
| joint_raw12_vs_baseline_original | -0.268% [-0.975%, +0.506%] | -0.206% | 0/196 | 0/196 |
| joint_raw12_vs_static18 | +0.317% [-0.246%, +1.116%] | -0.071% | 0/196 | 0/196 |
| joint_raw12_vs_static24 | +0.338% [-0.235%, +1.159%] | -0.056% | 0/196 | 0/196 |
| joint_raw12_vs_invf_raw12 | -0.580% [-0.973%, -0.250%] | -0.429% | 0/196 | 0/196 |
| joint_db12_vs_baseline_original | -0.099% [-0.853%, +0.732%] | -0.231% | 0/196 | 0/196 |
| joint_db12_vs_static18 | +0.475% [-0.145%, +1.334%] | +0.048% | 0/196 | 0/196 |
| joint_db12_vs_static24 | +0.496% [-0.134%, +1.371%] | +0.070% | 0/196 | 0/196 |
| joint_db12_vs_invf_db12 | -0.455% [-0.859%, -0.137%] | -0.226% | 0/196 | 0/196 |

Per-rate paired MOS deltas and actual payload-rate deltas are machine-readable in `direct-comparisons.json`.
