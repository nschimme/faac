# Transient-border four-clip gate results

Frozen source: `/private/tmp/faac-he-v1-transient-border`, upstream base `99ce2b0980180194f9ccd1993a786208d277cf3e`; final probe patch SHA-256 `0a9d3a2e201ca59e25f00943d0efb9ee68d8e76da99ff91de00cc1062eb2249`. The 192-row gate scored four clips at 32/48/64/96 kbps, with forced HE-AAC v1, strict FDK before Zimtohrli, actual AAC packet bitrate, PCHIP BD-rate, and four-clip bootstrap. Strict FAAD3 and FFmpeg each accepted all 192 streams.

Negative BD-rate means fewer packet bits at equal fitted MOS. This four-clip result is a screening signal, not a full-corpus or production claim. Bootstrap `wins/losses` counters are omitted because their helper sign convention is not a reliable quality label.

| Feature | Matched context | Mean BD-rate | 95% clip-bootstrap CI | Per-clip BD-rate (sandman / velvet / classic / fms) |
|---|---|---:|---:|---|
| `mode1_lam0001_base` | `baseline_ref` | -0.753% | [-1.927, +0.597]% | -2.381% / -0.566% / +1.225% / -1.289% |
| `mode1_lam0001_stack` | `stack_ref` | -1.142% | [-2.122, -0.162]% | -2.684% / -0.183% / -0.140% / -1.561% |
| `mode1_lam0003_base` | `baseline_ref` | -0.756% | [-2.061, +0.637]% | -2.559% / -0.566% / +1.225% / -1.125% |
| `mode1_lam0003_stack` | `stack_ref` | -1.036% | [-2.070, -0.162]% | -2.698% / -0.183% / -0.140% / -1.122% |
| `mode2_lam0001_base` | `baseline_ref` | -1.077% | [-2.724, +0.375]% | -3.340% / -0.875% / +0.798% / -0.892% |
| `mode2_lam0001_stack` | `stack_ref` | -1.309% | [-3.318, +0.424]% | -4.085% / -1.017% / +0.916% / -1.050% |
| `mode2_lam0003_base` | `baseline_ref` | -1.009% | [-2.706, +0.613]% | -3.317% / -0.875% / +1.149% / -0.993% |
| `mode2_lam0003_stack` | `stack_ref` | -1.350% | [-2.855, +0.008]% | -3.468% / -1.017% / +0.474% / -1.388% |

Worst MOS delta per rate versus each candidate’s matched baseline/stack reference:

| Feature | 32 kbps | 48 kbps | 64 kbps | 96 kbps |
|---|---|---|---|---|
| `mode1_lam0001_base` | -0.0088 (velvet.16b48k.wav) | -0.0203 (21-classic.441.16b48k.wav) | +0.0026 (21-classic.441.16b48k.wav) | -0.0025 (21-classic.441.16b48k.wav) |
| `mode1_lam0001_stack` | -0.0108 (velvet.16b48k.wav) | -0.0005 (velvet.16b48k.wav) | +0.0002 (21-classic.441.16b48k.wav) | -0.0001 (21-classic.441.16b48k.wav) |
| `mode1_lam0003_base` | -0.0088 (velvet.16b48k.wav) | -0.0203 (21-classic.441.16b48k.wav) | +0.0026 (21-classic.441.16b48k.wav) | -0.0025 (21-classic.441.16b48k.wav) |
| `mode1_lam0003_stack` | -0.0108 (velvet.16b48k.wav) | -0.0005 (velvet.16b48k.wav) | +0.0002 (21-classic.441.16b48k.wav) | -0.0001 (21-classic.441.16b48k.wav) |
| `mode2_lam0001_base` | -0.0156 (fms.wav) | -0.0208 (21-classic.441.16b48k.wav) | +0.0040 (21-classic.441.16b48k.wav) | -0.0027 (21-classic.441.16b48k.wav) |
| `mode2_lam0001_stack` | -0.0163 (fms.wav) | -0.0215 (21-classic.441.16b48k.wav) | -0.0003 (21-classic.441.16b48k.wav) | +0.0013 (21-classic.441.16b48k.wav) |
| `mode2_lam0003_base` | -0.0233 (fms.wav) | -0.0269 (21-classic.441.16b48k.wav) | +0.0034 (21-classic.441.16b48k.wav) | -0.0022 (21-classic.441.16b48k.wav) |
| `mode2_lam0003_stack` | -0.0219 (fms.wav) | -0.0126 (21-classic.441.16b48k.wav) | -0.0003 (21-classic.441.16b48k.wav) | -0.0005 (21-classic.441.16b48k.wav) |

The two mode-1 retained-stack settings are the strongest gate signals: `mode1_lam0001_stack` is −1.142% [−2.122, −0.162] and `mode1_lam0003_stack` is −1.036% [−2.070, −0.162]. Mode 1 baseline-context intervals cross zero. Both mode-2 retained-stack intervals include zero (−1.309% [−3.318, +0.424] and −1.350% [−2.855, +0.008]). The 32 kbps per-rate MOS intervals cross zero for all candidates; some higher-rate per-rate intervals are positive, so inspect these four-clip deltas rather than treating aggregate gate evidence as a quality guarantee.

Artifacts: [`summary.json`](summary.json), [`rows.json`](rows.json), [`interop-results.json`](interop-results.json), [`config.json`](config.json).
