# Persistent border versus tier17 policy

This paired comparison uses the same 49 clips and four target rates for both measured policies. For the tier17 envelope-count policy, the base curve uses `baseline_original` at 32 kbps and `all_base` at 48/64/96; the stack curve uses `stack_reference` at 32 and `all_stack` at 48/64/96. The persistent policy is cross-frame mode 2, `future_pcm=0`, λ=.003. Positive BD-rate means persistent uses more bits than tier17 for equal fitted MOS. The result compares alternatives; it is not an estimate for combining features.

| Context | Persistent vs tier17 mean BD-rate | 49-clip bootstrap 95% CI | Worst persistent delta | Best persistent delta |
|---|---:|---:|---|---|
| Base | +0.164% | [−0.451%, +0.740%] | glk.wav +6.831% | cst.wav −5.948% |
| Retained stack | +0.370% | [−0.167%, +0.890%] | glk.wav +4.719% | cst.wav −5.375% |

Both intervals cross zero. This comparison does not establish a quality advantage for persistent border placement over the measured tier17 policy. Results were derived from the existing scored rows; no new encoding or MOS work was performed.

See `tier17-policy-comparison-derived.json` and `tier17-policy-provenance.json` for all per-clip data, source hashes, helper hashes, and the reproducible derivation command. The derivation script is `../tools/derive_tier17_comparison.py`.
