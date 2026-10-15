# HE-AAC v1 missing-harmonic SBR probe summary

BD-rate uses actual ffprobe packet payload rate and Zimtohrli MOS. Positive means more bits are required for equal quality.

| Variant | Valid clips | Mean BD-rate (95% clip bootstrap CI) | Median BD-rate |
|---|---:|---:|---:|
| candidate_default | 4/4 | +0.000% [+0.000%, +0.000%] | +0.000% |
| static24 | 4/4 | -0.721% [-1.271%, -0.221%] | -0.670% |
| missing_t10_r4 | 4/4 | +0.283% [-0.373%, +1.240%] | -0.019% |
| missing_t10_r10 | 4/4 | +0.162% [-0.154%, +0.457%] | +0.229% |
| missing_t30_r4 | 4/4 | -0.263% [-0.541%, +0.015%] | -0.217% |
| missing_t30_r10 | 4/4 | -0.177% [-0.373%, -0.036%] | -0.122% |
| missing_t100_r4 | 4/4 | -0.107% [-0.322%, +0.000%] | +0.000% |
| missing_t100_r10 | 4/4 | -0.107% [-0.322%, +0.000%] | +0.000% |
| missing_max_energy | 4/4 | -0.109% [-0.240%, +0.049%] | -0.135% |
| missing_midpoint | 4/4 | -0.266% [-0.523%, -0.010%] | -0.226% |

## Direct comparisons

Negative BD-rate favors the candidate; intervals bootstrap over clips.

| Candidate vs reference | Mean BD-rate (95% CI) | Median | Packet ties | PCM ties |
|---|---:|---:|---:|---:|
| static24_vs_baseline_default | -0.721% [-1.271%, -0.221%] | -0.670% | 0/16 | 0/16 |
| missing_t10_r4_vs_baseline_default | +0.283% [-0.373%, +1.240%] | -0.019% | 0/16 | 0/16 |
| missing_t10_r4_vs_static24 | +1.002% [+0.333%, +1.818%] | +0.855% | 0/16 | 0/16 |
| missing_t10_r10_vs_baseline_default | +0.162% [-0.154%, +0.457%] | +0.229% | 0/16 | 0/16 |
| missing_t10_r10_vs_static24 | +0.888% [+0.553%, +1.142%] | +0.970% | 0/16 | 0/16 |
| missing_t30_r4_vs_baseline_default | -0.263% [-0.541%, +0.015%] | -0.217% | 0/16 | 0/16 |
| missing_t30_r4_vs_static24 | +0.463% [-0.253%, +1.001%] | +0.691% | 0/16 | 0/16 |
| missing_t30_r10_vs_baseline_default | -0.177% [-0.373%, -0.036%] | -0.122% | 0/16 | 0/16 |
| missing_t30_r10_vs_static24 | +0.547% [-0.148%, +1.163%] | +0.625% | 0/16 | 0/16 |
| missing_t100_r4_vs_baseline_default | -0.107% [-0.322%, +0.000%] | +0.000% | 12/16 | 12/16 |
| missing_t100_r4_vs_static24 | +0.621% [+0.129%, +1.275%] | +0.458% | 0/16 | 0/16 |
| missing_t100_r10_vs_baseline_default | -0.107% [-0.322%, +0.000%] | +0.000% | 12/16 | 12/16 |
| missing_t100_r10_vs_static24 | +0.621% [+0.129%, +1.275%] | +0.458% | 0/16 | 0/16 |
| missing_max_energy_vs_baseline_default | -0.109% [-0.240%, +0.049%] | -0.135% | 0/16 | 0/16 |
| missing_max_energy_vs_static24 | +0.614% [+0.114%, +1.115%] | +0.659% | 0/16 | 0/16 |
| missing_midpoint_vs_baseline_default | -0.266% [-0.523%, -0.010%] | -0.226% | 0/16 | 0/16 |
| missing_midpoint_vs_static24 | +0.459% [-0.241%, +0.976%] | +0.664% | 0/16 | 0/16 |
