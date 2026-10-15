# HE-AAC v1 missing-harmonic SBR probe summary

BD-rate uses actual ffprobe packet payload rate and Zimtohrli MOS. Positive means more bits are required for equal quality.

| Variant | Valid clips | Mean BD-rate (95% clip bootstrap CI) | Median BD-rate |
|---|---:|---:|---:|
| candidate_default | 49/49 | +0.000% [+0.000%, +0.000%] | +0.000% |
| static18 | 49/49 | -0.607% [-1.012%, -0.292%] | -0.116% |
| static24 | 49/49 | -0.625% [-1.033%, -0.304%] | -0.135% |
| missing_t30_r4 | 49/49 | -0.555% [-1.610%, +0.236%] | +0.000% |
| missing_midpoint | 49/49 | -0.643% [-1.713%, +0.167%] | +0.000% |
| missing_t30_r4_n18 | 49/49 | -1.063% [-2.107%, -0.309%] | -0.181% |
| missing_midpoint_n18 | 49/49 | -1.168% [-2.277%, -0.388%] | -0.397% |
| missing_max_energy_n18 | 49/49 | -0.314% [-0.899%, +0.233%] | -0.073% |

## Direct comparisons

Negative BD-rate favors the candidate; intervals bootstrap over clips.

| Candidate vs reference | Mean BD-rate (95% CI) | Median | Packet ties | PCM ties |
|---|---:|---:|---:|---:|
| static18_vs_baseline_default | -0.607% [-1.012%, -0.292%] | -0.116% | 0/196 | 0/196 |
| static18_vs_static24 | +0.019% [+0.006%, +0.035%] | +0.005% | 0/196 | 0/196 |
| static24_vs_baseline_default | -0.625% [-1.033%, -0.304%] | -0.135% | 0/196 | 0/196 |
| missing_t30_r4_vs_baseline_default | -0.555% [-1.610%, +0.236%] | +0.000% | 21/196 | 21/196 |
| missing_t30_r4_vs_static24 | +0.056% [-1.017%, +1.043%] | +0.130% | 0/196 | 0/196 |
| missing_midpoint_vs_baseline_default | -0.643% [-1.713%, +0.167%] | +0.000% | 22/196 | 22/196 |
| missing_midpoint_vs_static24 | -0.031% [-1.111%, +0.962%] | +0.113% | 0/196 | 0/196 |
| missing_t30_r4_n18_vs_baseline_default | -1.063% [-2.107%, -0.309%] | -0.181% | 0/196 | 0/196 |
| missing_t30_r4_n18_vs_static24 | -0.457% [-1.473%, +0.343%] | +0.007% | 0/196 | 0/196 |
| missing_midpoint_n18_vs_baseline_default | -1.168% [-2.277%, -0.388%] | -0.397% | 0/196 | 0/196 |
| missing_midpoint_n18_vs_static24 | -0.562% [-1.627%, +0.256%] | +0.001% | 0/196 | 0/196 |
| missing_max_energy_n18_vs_baseline_default | -0.314% [-0.899%, +0.233%] | -0.073% | 0/196 | 0/196 |
| missing_max_energy_n18_vs_static24 | +0.332% [-0.256%, +1.006%] | +0.072% | 0/196 | 0/196 |
