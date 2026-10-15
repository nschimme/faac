# Cross-frame border full-corpus follow-up

This preregistered full-corpus run evaluates only the gate-screened setting: cross-frame mode 2, future_pcm=0, λ=.003, in base and retained-stack contexts. It covers 49 clips at 32/48/64/96 kbps. The future_pcm=0 branch compares delayed-tail q0..5 against the sampled current-N highband mean; it is a detector policy, not a pure future-data ablation. Negative BD-rate means fewer bits for equal fitted MOS.

The matrix contains 1,176 rows: baseline and retained-stack references, exact mode-off controls, and the two selected feature settings across all clip/rate pairs. Controls were checked for packet and strict FDK PCM identity before MOS. MOS used actual AAC packet payload rates, forced HE-AAC v1, strict FDK decode before Zimtohrli; all 1,176 streams passed strict FAAD3 and FFmpeg. This corpus has no held-out clips.

| Setting | Mean BD-rate vs matched reference | 49-clip bootstrap 95% CI | Worst per-clip BD-rate | Best per-clip BD-rate |
|---|---:|---:|---|---|
| x2_f0_lam0003_base | -1.631% | [-2.213%, -1.119%] | Greatest_Love_of_All_2min57.16b48k.wav +0.510% | cst.wav -7.380% |
| x2_f0_lam0003_stack | -1.323% | [-1.828%, -0.849%] | Greatest_Love_of_All_2min57.16b48k.wav +1.111% | mof.wav -7.176% |

## Per-rate detail: x2_f0_lam0003_base

| Target | Mean MOS delta | Mean payload delta | Worst MOS delta (clip) |
|---:|---:|---:|---|
| 32 kbps | +0.01997 | +0.0020 kb/s | -0.03902 (Greatest_Love_of_All_2min57.16b48k.wav) |
| 48 kbps | +0.01402 | -0.0015 kb/s | -0.00439 (35_SQAM_glockenspiel_cut.16b48k.wav) |
| 64 kbps | +0.01180 | -0.0017 kb/s | -0.00669 (bas.wav) |
| 96 kbps | +0.01071 | -0.0018 kb/s | -0.00372 (Greatest_Love_of_All_2min57.16b48k.wav) |

## Per-rate detail: x2_f0_lam0003_stack

| Target | Mean MOS delta | Mean payload delta | Worst MOS delta (clip) |
|---:|---:|---:|---|
| 32 kbps | +0.01684 | +0.0020 kb/s | -0.04211 (Greatest_Love_of_All_2min57.16b48k.wav) |
| 48 kbps | +0.01011 | -0.0021 kb/s | -0.01231 (Greatest_Love_of_All_2min57.16b48k.wav) |
| 64 kbps | +0.01146 | -0.0007 kb/s | -0.00778 (Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav) |
| 96 kbps | +0.00904 | -0.0013 kb/s | -0.00427 (35_SQAM_glockenspiel_cut.16b48k.wav) |

Both contexts show a resolved mean BD-rate reduction against their matched references on this corpus. The greatest positive per-clip BD-rate is Greatest_Love_of_All_2min57 (base +0.510%, stack +1.111%); the largest improvements are on cst.wav / mof.wav. This confirms the selected screened setting for the measured corpus; it does not establish generalization to held-out audio or a production recommendation. Frozen-source, upstream-base, state/ROM, timing, and feature-combination decisions remain separate gates.

See `config.json`, `rows.json`, `summary.json`, and `interop-results.json` for source hashes, exact commands, scores, and decoder validation. The probe uses frozen source HEAD `99ce2b0980180194f9ccd1993a786208d277cf3e`, patch SHA-256 `f3e3c10c89c8762d307c4e7f0cd8474a50f07021a3a3285e5167b381fbf864d2`.

A direct comparison against the measured tier17 envelope-count policy is in [TIER17-COMPARISON.md](TIER17-COMPARISON.md). It uses baseline/stack at 32 kbps and the corresponding all-base/all-stack rows at 48/64/96; both persistent-versus-tier17 intervals cross zero.
