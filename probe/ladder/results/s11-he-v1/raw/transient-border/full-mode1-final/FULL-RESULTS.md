# Transient-specific SBR border placement: full-corpus results

This is the full 49-clip follow-up for the two preregistered mode-1 lambdas selected by the four-clip gate, measured in baseline and retained-stack contexts. The screening clips are part of the 49-clip set, so this is not held-out validation. Four variants were scored at 32/48/64/96 kbps using actual AAC packet rates, forced HE-AAC v1, strict FDK before Zimtohrli, PCHIP BD-rate, and clip bootstrap. The 392 baseline/stack reference and mode-off control rows passed exact packet and strict FDK PCM identity before feature scoring.

All 1,568 rows were scored; strict FAAD3 accepted 1,568/1,568 streams and FFmpeg accepted 1,568/1,568. Negative BD-rate means fewer packet bits at equal fitted MOS.

| Variant | Matched reference | Mean BD-rate | Median | 95% clip-bootstrap CI | Worst per-clip BD-rate | Best per-clip BD-rate |
|---|---|---:|---:|---:|---|---|
| `mode1_lam0001_base` | `baseline_ref` | -0.790% | -0.517% | [-1.126, -0.479]% | Robots_old.16b48k.wav +1.344% | girl.16b48k.wav -4.423% |
| `mode1_lam0001_stack` | `stack_ref` | -0.695% | -0.304% | [-1.004, -0.418]% | Greatest_Love_of_All_2min57.16b48k.wav +0.517% | girl.16b48k.wav -4.469% |
| `mode1_lam0003_base` | `baseline_ref` | -0.824% | -0.401% | [-1.165, -0.510]% | 21-classic.441.16b48k.wav +1.225% | girl.16b48k.wav -5.003% |
| `mode1_lam0003_stack` | `stack_ref` | -0.626% | -0.253% | [-0.925, -0.344]% | 9-Have-big-expensive-car.441.16b48k.wav +1.338% | mof.wav -3.967% |

Per-rate mean MOS and packet bitrate deltas versus each variant’s matched reference. The last column lists the worst clip MOS delta and that clip’s packet-rate delta.

| Variant | Rate | Mean MOS Δ | Mean payload Δ (kbps) | Worst clip: MOS Δ / payload Δ (kbps) |
|---|---:|---:|---:|---|
| `mode1_lam0001_base` | 32 | +0.0045 | -0.0009 | Robots_old.16b48k.wav: -0.0282 / +0.0008 |
| `mode1_lam0001_base` | 48 | +0.0073 | +0.0007 | 21-classic.441.16b48k.wav: -0.0203 / +0.0008 |
| `mode1_lam0001_base` | 64 | +0.0064 | -0.0003 | Girl_In_The_Fire__Sample_.16b48k.wav: -0.0099 / +0.0008 |
| `mode1_lam0001_base` | 96 | +0.0057 | -0.0002 | Changes.16b48k.wav: -0.0081 / -0.0016 |
| `mode1_lam0001_stack` | 32 | +0.0029 | -0.0009 | Robots_old.16b48k.wav: -0.0280 / +0.0008 |
| `mode1_lam0001_stack` | 48 | +0.0058 | +0.0005 | take_your_finger_frin_my_head.16b48k.wav: -0.0087 / +0.0016 |
| `mode1_lam0001_stack` | 64 | +0.0062 | +0.0002 | Only Time.16b48k.wav: -0.0086 / -0.0016 |
| `mode1_lam0001_stack` | 96 | +0.0056 | +0.0008 | Changes.16b48k.wav: -0.0094 / -0.0008 |
| `mode1_lam0003_base` | 32 | +0.0049 | -0.0012 | 9-Have-big-expensive-car.441.16b48k.wav: -0.0131 / +0.0066 |
| `mode1_lam0003_base` | 48 | +0.0076 | +0.0008 | 21-classic.441.16b48k.wav: -0.0203 / +0.0008 |
| `mode1_lam0003_base` | 64 | +0.0061 | +0.0003 | Girl_In_The_Fire__Sample_.16b48k.wav: -0.0107 / -0.0008 |
| `mode1_lam0003_base` | 96 | +0.0056 | -0.0005 | Changes.16b48k.wav: -0.0081 / -0.0016 |
| `mode1_lam0003_stack` | 32 | +0.0033 | -0.0012 | 9-Have-big-expensive-car.441.16b48k.wav: -0.0273 / +0.0066 |
| `mode1_lam0003_stack` | 48 | +0.0048 | -0.0012 | 9-Have-big-expensive-car.441.16b48k.wav: -0.0238 / -0.0008 |
| `mode1_lam0003_stack` | 64 | +0.0064 | +0.0005 | Only Time.16b48k.wav: -0.0058 / +0.0016 |
| `mode1_lam0003_stack` | 96 | +0.0057 | +0.0003 | Changes.16b48k.wav: -0.0094 / -0.0032 |

All four mean BD-rate intervals are below zero in their matched contexts. The largest positive per-clip BD-rate (worst case) is +1.344% for `Robots_old` under `mode1_lam0001_base`, +0.517% for `Greatest_Love_of_All` under `mode1_lam0001_stack`, +1.225% for `21-classic` under `mode1_lam0003_base`, and +1.338% for `9-Have-big-expensive-car` under `mode1_lam0003_stack`. These full-corpus results support the tested mode-1 settings, but the same corpus informed gate selection and is not held out. The subsequent native tier17 timing measured a duration increase, documented separately; the quality results alone do not establish a production decision.

Artifacts: [`FULL-RESULTS.json`](FULL-RESULTS.json) is the reviewed derived summary; raw runner artifacts remain [`summary.json`](summary.json), [`rows.json`](rows.json), [`interop-results.json`](interop-results.json), and [`config.json`](config.json).
