# HE-v1 remaining requirements (status snapshot)

This checklist records the evidence needed before the study is complete. Current outstanding work is PR #70 build and benchmark CI review.

## Quality families and current disposition

| Family | Current conclusion / retention |
|---|---|
| Noise18 + gated limiter | Retained in the merged PR #47 baseline. |
| Single-band adaptive noise | Full matched comparison regressed; rejected. |
| Single-band inverse filtering / joint noise | No demonstrated gain over static noise; rejected. |
| Frequency-local noise / inverse filtering | Four two-band variants regressed against matched controls; rejected. |
| Missing-harmonic tonality detector | No matched-static gain for tone/ratio/midpoint; max-energy ablation regressed; rejected. |
| Transient grid / envelope resolution | Late FIXVAR regressed; remaining settings inconclusive; no candidate retained. |
| Frequency-table policy | No screening gain; no full follow-up. |
| Adaptive envelope count | Tier17 hybrid is merged with noise18 and the gated limiter in upstream PR #47, commit 098e8900. Fork PR #66 is closed. Earlier unconstrained selector had no resolved gain over fixed4. Historical CI failures are recorded separately. |
| Transient-specific border: fixed8 | Merged-baseline full49 is −0.512570% [−0.762929%, −0.281702%], 392/392 strict decoder rows; provisionally selected. Production cost, smoke, and publication complete; PR #70 CI pending. |
| Transient-specific border: adaptive mode1 | Old-base full run showed a small resolved fixed8 advantage. Frozen merged-baseline full49 λ=.001/.003 improved against baseline but neither resolved a gain over fixed8: −0.132520% [−0.333368%, +0.063008%] and −0.151927% [−0.323619%, +0.014367%]. All 392 active streams were FDK-decoded and passed strict FAAD/FFmpeg. Old/new FDK PCM equivalence is established for the separate 32-row adaptive gate, not this full matrix. No held-out corpus. Provisional quality choice: fixed8; adaptive not retained. |
| Cross-frame mode2 / persistent framing | Full quality checks pass, but no resolved advantage over simpler tier17; do not retain. |

## Required next evidence

1. The matched adaptive mode1 λ=.001/.003 full49 run is complete. It used original-WAV anchors, actual packet bitrate, forced HE-AAC v1 profile, frozen settings, and clip-bootstrap confidence intervals. No additional tuning or held-out claim.
2. Production footprint, native timing, smoke, LC-tail diagnostics, and publication are complete. Fixed8 has −16 B GNU size text+data under GCC13/LTO emulated amd64, no BSS growth; native mean timing +0.029598% with 95% CI [−0.392430%, +0.412281%] over 40 pairs. Four LC ADTS smoke bitstreams were byte-identical; eight HE-AAC streams decoded with FDK/FAAD2/FFmpeg. No Meson tests were defined in the checkout, so no test-suite pass is claimed. HE baseline/fixed8 frame counts matched, but PCM identity was not asserted. The separate 24-case 16 kHz LC tail check matched expected lengths and old/new FDK PCM; this is not a general gapless guarantee. See `transient-border/upstream098-fixed8-gate-v2/FIXED8-PRODUCTION-RESULTS.md`.
3. PR #70 is published from upstream/master. Its build and benchmark workflow jobs are queued; review those results when they finish. Historical CI quality cases remain distinct from the resolved FAAD2 flush failure.
4. Retention choice is fixed8 based on current quality and measured cost; adaptive and cross-frame selectors are not retained. No overall completion or merge-readiness claim before PR #70 CI passes.

## Decoder evidence already established

The merged-baseline fixed8 full49 matrix passed FDK, strict FAAD and strict FFmpeg for all 392 streams; current/new FDK PCM matched the previous FDK PCM exactly for those rows. The merged-context adaptive gate passed all 32 active streams with FDK, strict FAAD and strict FFmpeg, and its new/old FDK PCM also matched. Full49 new/old FDK equivalence is established for fixed8 only; the adaptive full49 was decoded using the new FDK. The adaptive full49 decoder evidence is archived separately at `transient-border/upstream098-mode1-full49-v1/strict-interop.json`.
