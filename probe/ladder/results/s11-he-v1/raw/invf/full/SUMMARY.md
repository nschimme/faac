# Inverse-filtering and joint-noise full-corpus summary

The run covers 49 clips at 32/48/64/96 kbps. Six new variants were freshly scored; strict FAAD and FFmpeg each accepted 1176 streams with 0 and 0 failures, respectively. FDK acceptance was mandatory before every MOS score. The unset encoder matched packet+PCM baseline on all 196 full-corpus clip/rate controls.

BD-rate is measured against the named reference using actual AAC payload bitrate; negative favors the candidate. Confidence intervals bootstrap over clips.

| Candidate vs reference | Mean BD-rate (95% CI) | Median | Worst clip | Best clip |
|---|---:|---:|---|---|
| invf_raw12_vs_baseline_original | +0.269% [-0.282%, +1.075%] | -0.062% | glk.wav +12.302% | bah.wav -3.782% |
| invf_db12_vs_baseline_original | +0.299% [-0.255%, +1.057%] | -0.082% | glk.wav +12.539% | bah.wav -3.295% |
| invf_raw24_vs_static24 | +0.275% [-0.294%, +1.086%] | -0.070% | glk.wav +13.209% | girl.16b48k.wav -3.686% |
| invf_db24_vs_static24 | +0.313% [-0.258%, +1.101%] | -0.065% | glk.wav +13.354% | girl.16b48k.wav -3.347% |
| joint_raw12_vs_baseline_original | -0.268% [-0.975%, +0.506%] | -0.206% | glk.wav +10.722% | girl.16b48k.wav -8.207% |
| joint_raw12_vs_static18 | +0.317% [-0.246%, +1.116%] | -0.071% | glk.wav +14.138% | bah.wav -3.582% |
| joint_raw12_vs_static24 | +0.338% [-0.235%, +1.159%] | -0.056% | glk.wav +14.522% | bah.wav -3.515% |
| joint_raw12_vs_invf_raw12 | -0.580% [-0.973%, -0.250%] | -0.429% | Robots_old.16b48k.wav +1.693% | girl.16b48k.wav -5.398% |
| joint_db12_vs_baseline_original | -0.099% [-0.853%, +0.732%] | -0.231% | glk.wav +11.561% | girl.16b48k.wav -8.603% |
| joint_db12_vs_static18 | +0.475% [-0.145%, +1.334%] | +0.048% | glk.wav +14.574% | bah.wav -3.431% |
| joint_db12_vs_static24 | +0.496% [-0.134%, +1.371%] | +0.070% | glk.wav +14.963% | bah.wav -3.363% |
| joint_db12_vs_invf_db12 | -0.455% [-0.859%, -0.137%] | -0.226% | hrp.wav +1.425% | girl.16b48k.wav -6.244% |

## Paired per-rate MOS for joint12

| Comparison | Rate | MOS delta (95% paired CI) | Actual bitrate delta |
|---|---:|---:|---:|
| joint_raw12_vs_invf_raw12 | 32 kbps | +0.00521 [+0.00106, +0.00940] | -0.002% |
| joint_raw12_vs_invf_raw12 | 48 kbps | +0.00279 [+0.00027, +0.00533] | +0.005% |
| joint_raw12_vs_invf_raw12 | 64 kbps | +0.00435 [+0.00170, +0.00748] | +0.001% |
| joint_raw12_vs_invf_raw12 | 96 kbps | +0.00347 [+0.00182, +0.00538] | +0.001% |
| joint_raw12_vs_static24 | 32 kbps | -0.00181 [-0.00577, +0.00236] | +0.003% |
| joint_raw12_vs_static24 | 48 kbps | +0.00028 [-0.00373, +0.00437] | +0.016% |
| joint_raw12_vs_static24 | 64 kbps | -0.00117 [-0.00485, +0.00196] | +0.010% |
| joint_raw12_vs_static24 | 96 kbps | -0.00249 [-0.00662, +0.00106] | +0.007% |
| joint_db12_vs_invf_db12 | 32 kbps | +0.00473 [+0.00194, +0.00764] | -0.001% |
| joint_db12_vs_invf_db12 | 48 kbps | +0.00159 [-0.00073, +0.00411] | +0.005% |
| joint_db12_vs_invf_db12 | 64 kbps | +0.00248 [+0.00035, +0.00487] | -0.002% |
| joint_db12_vs_invf_db12 | 96 kbps | +0.00399 [+0.00229, +0.00603] | +0.001% |
| joint_db12_vs_static24 | 32 kbps | -0.00167 [-0.00576, +0.00236] | +0.020% |
| joint_db12_vs_static24 | 48 kbps | -0.00217 [-0.00642, +0.00199] | +0.023% |
| joint_db12_vs_static24 | 64 kbps | -0.00203 [-0.00616, +0.00143] | +0.011% |
| joint_db12_vs_static24 | 96 kbps | -0.00231 [-0.00672, +0.00144] | +0.009% |

Inverse filtering alone is inconclusive against its matched static controls and baseline; its worst clip is `glk.wav` (+12.3% to +13.4% BD-rate). Joint raw/dB variants improve significantly over corresponding invf-only settings, but do not establish a gain against static noise levels 18 or 24 because those CIs cross zero. Joint raw vs static24 has a worst clip `glk.wav` at +14.522%. Therefore the joint probe is not promoted, despite its incremental improvement over filter-only. No timing or production ROM measurement was made.

The stereo `sizeof` allocation model estimates +2,308 B incremental state; this excludes the resampler and is not ROM, peak RSS, or stack.

See [all full-corpus pairwise results](direct-comparisons.json), [raw runner rows](invf-results.json), [configuration and hashes](invf-config.json), [decoder outcomes](interop-results.json), and [probe patch](../invf-probe.patch).
