# Fixed-8 transient-border full-corpus comparison

This full-corpus follow-up measures an unconditional eight-border placement in base and retained-stack contexts across 49 clips at 32/48/64/96 kbps. It reuses only verified packet/decoded-PCM/MOS rows from the partial v1 run; the v2 config hashes and names the exact resume source. The full matrix contains 784 rows (49 clips × 4 rates × two references plus two fixed-8 conditions). All 784 streams passed strict FAAD3 and FFmpeg; forced HE-AAC v1 streams were strict FDK-decoded before MOS. There are no held-out clips. Negative BD-rate means fewer bits for equal fitted MOS.

| Fixed-8 context | Mean BD-rate vs matched reference | 49-clip bootstrap 95% CI | Worst clip | Best clip |
|---|---:|---:|---|---|
| fixed8_base | -0.479% | [-0.730%, -0.235%] | lob.wav +1.761% | 12-German-male-speech.441.16b48k.wav -3.259% |
| fixed8_stack | -0.480% | [-0.740%, -0.214%] | 27-last-song-drums-and-trampets.441.16b48k.wav +1.382% | mof.wav -3.582% |

## Adaptive mode 1 versus fixed-8

| Adaptive setting | Context | Mean BD-rate vs fixed-8 | 49-clip bootstrap 95% CI | Worst positive clip delta |
|---|---|---:|---:|---|
| mode1_lam0001_base | base | -0.311% | [-0.553%, -0.082%] | 21-classic.441.16b48k.wav +1.374% |
| mode1_lam0001_stack | stack | -0.211% | [-0.472%, +0.020%] | 24-Greensleeves-Korean-male-speech.441.16b48k.wav +1.294% |
| mode1_lam0003_base | base | -0.345% | [-0.598%, -0.121%] | 21-classic.441.16b48k.wav +1.374% |
| mode1_lam0003_stack | stack | -0.146% | [-0.361%, +0.043%] | 24-Greensleeves-Korean-male-speech.441.16b48k.wav +1.261% |

Fixed-8 reduced BD-rate against both matched references in the measured corpus. Compared with fixed-8, both adaptive mode-1 settings show small resolved reductions in the base context; both retained-stack intervals cross zero. This supports a modest base-context adaptive attribution, not a universal advantage over fixed-8. The per-rate MOS/payload deltas and worst MOS cases are recorded in `summary.json`; raw evidence and resume provenance are in `rows.json` and `config.json`; the corrected 49-clip summary audit is `SUMMARY-AUDIT.md`. No production recommendation follows without separate state/ROM, timing, and updated-upstream-base gates.
