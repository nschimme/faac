# Fixed steady-envelope controls and adaptive attribution

All results use 49 48 kHz stereo clips at 32/48/64/96 kbps. Each stream was forced to HE-AAC v1; actual AAC payload bitrate, strict FDK decode, Zimtohrli audio MOS, PCHIP BD-rate, and 2,000-sample clip bootstrap mean CIs were used. The four control binaries are hash-locked in `config.json`; MOS cache reuse required identical AAC packet and strict FDK PCM hashes. Strict FAAD3 and FFmpeg validation passed all 784 streams (784/784 each).

Negative BD-rate means the left-hand condition needs fewer bits than the named matched reference at equivalent MOS. In comparisons below, negative values are improvements for the adaptive condition; positive values are regressions. Sign counts are computed from per-clip BD-rate values, not the benchmark helper's generic `wins/losses` fields.

## Fixed controls against matched reference

| Condition | Reference | Mean BD-rate (95% clip-bootstrap CI) | Negative / positive clip deltas |
|---|---|---:|---:|
| base fixed1 | frozen baseline | +3.755% [+2.749,+4.812] | 5 / 44 |
| base fixed2 | frozen baseline | -0.333% [-0.456,-0.204] | 43 / 6 |
| stack fixed1 | retained stack | +3.661% [+2.682,+4.666] | 4 / 45 |
| stack fixed2 | retained stack | -0.284% [-0.409,-0.152] | 41 / 8 |

A steady count of one is materially worse. A steady count of two is close to the existing tiered 1/2-envelope behavior (`tier_base` vs baseline: -0.347% [-0.409,-0.289]; `tier_stack` vs stack: -0.348% [-0.410,-0.290]).

## Adaptive versus fixed and tier/all controls

| Adaptive condition | Reference | Mean BD-rate (95% CI) | Clips improved / regressed |
|---|---|---:|---:|
| ad001_base | base fixed1 | -5.560% [-7.041,-4.111] | 46 / 3 |
| ad001_base | base fixed2 | -1.754% [-2.423,-1.116] | 40 / 9 |
| ad003_base | base fixed1 | -5.814% [-7.240,-4.448] | 46 / 3 |
| ad003_base | base fixed2 | -1.988% [-2.560,-1.438] | 46 / 3 |
| ad001_stack | stack fixed1 | -5.153% [-6.591,-3.746] | 45 / 4 |
| ad001_stack | stack fixed2 | -1.497% [-2.114,-0.880] | 39 / 10 |
| ad003_stack | stack fixed1 | -5.373% [-6.733,-4.018] | 46 / 3 |
| ad003_stack | stack fixed2 | -1.673% [-2.257,-1.085] | 40 / 9 |
| ad001_base | tier_base | -1.640% [-2.293,-1.028] | 38 / 11 |
| ad003_base | tier_base | -1.861% [-2.418,-1.323] | 45 / 4 |
| ad001_stack | tier_stack | -1.325% [-1.904,-0.750] | 39 / 10 |
| ad003_stack | tier_stack | -1.493% [-2.047,-0.932] | 41 / 8 |
| ad001_base | all_base | +0.024% [-0.249,+0.316] | 25 / 24 |
| ad003_base | all_base | -0.186% [-0.506,+0.177] | 27 / 22 |
| ad001_stack | all_stack | +0.149% [-0.195,+0.526] | 26 / 23 |
| ad003_stack | all_stack | -0.014% [-0.352,+0.383] | 24 / 25 |

Both adaptive lambdas beat fixed1, fixed2, and the 1/2 tier on these paired curves. Against steady four envelopes, every 95% CI crosses zero and clip signs are balanced. The full corpus therefore does not establish an incremental adaptive benefit over the fixed-four control; this result does not support promoting adaptive envelope selection on quality evidence alone.

The most negative 32 kbps MOS deltas for adaptive versus fixed2 were `bas.wav` (-0.104 MOS for ad001_base; -0.102 for ad003_base; bitrate deltas +0.043 and -0.001 kbps) and `bas.wav` (-0.106 for ad001_stack; -0.104 for ad003_stack; bitrate deltas +0.043 and -0.001 kbps). The complete per-clip and per-rate deltas, including all worst 32 kbps rows, are in `comparisons.json`.

For adaptive versus steady-four, the worst 32 kbps MOS deltas are much smaller: `lob.wav` at -0.031 MOS for ad001_base (bitrate -0.021 kbps), `Can't Wait Until Tonight (Dry Wurlitzer Mix)` at -0.031 for ad003_base (-0.022 kbps), `lob.wav` at -0.031 for ad001_stack (-0.021 kbps), and the same Wurlitzer clip at -0.034 for ad003_stack (-0.022 kbps). Against the 1/2 tier, the largest 32 kbps drops are `bas.wav` (-0.130/-0.129 MOS for ad001/ad003 base; -0.130/-0.128 for stack), each with at most +0.049 kbps payload delta. These are per-clip observations; the corpus CIs and balanced fixed-four comparisons above do not show a resolved adaptive gain.

## Reproducibility and detailed comparisons

- Fixed control inputs, binary/library hashes, source worktrees and run settings: `config.json`.
- Encoded/FDK-decoded stream rows and packet/PCM hashes: `fixed-results.json`.
- Strict interop outcomes: `interop-results.json`.
- Fixed and adaptive-vs-fixed1/fixed2 results with per-clip rows and worst 32 kbps deltas: `comparisons.json`.
- Adaptive-vs-tier/all direct matched comparisons: `../../adaptive-vs-tier-all.json`.
- Adaptive-vs-all4 direct matched comparisons: `../../adaptive-vs-all4.json`.
- Main full49 variant scores: `../../full-active-lambda/RESULTS.md` and `../../full-active-lambda/direct-comparisons.json`.
