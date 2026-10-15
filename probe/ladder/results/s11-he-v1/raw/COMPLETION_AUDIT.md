# HE-AAC v1 experiment completion audit

## Scope and method

The original bounded study starts at upstream/master
`99ce2b0980180194f9ccd1993a786208d277cf3e`. Isolated probes use forced
HE-AAC v1, ffprobe profile checks, strict FDK decoding, original-source
Zimtohrli audio MOS, actual AAC packet bytes divided by source duration,
and per-clip PCHIP BD-rate with 2,000 clip-bootstrap samples and seed 0.
Negative BD-rate favors the candidate. Full studies use 49 stereo 48 kHz
clips at 32, 48, 64 and 96 kbps unless their report says otherwise.

Completion covers the named hypotheses and variants, not every possible SBR
design. A failed or inconclusive screen completes its hypothesis but does
not justify shipping. Retained candidates require production validation and
cost measurements. Main tracked files remain untouched; only the user's
untracked AGENTS.md is present.

## Completed original experiment evidence

| Family | Quality and control evidence | Outcome |
|---|---|---|
| Static noise and gated limiter | Matched full ladders; production packet/FDK PCM identity 196/196 per candidate; 1,060 strict FAAD3/FFmpeg streams | Noise18 and gated limiter retained |
| Single-band adaptive noise | Gate plus full 49-clip static18/24 comparison; 196 unset controls; 392 new strict decoder streams | Regressed against static controls |
| Single-band inverse filtering and joint noise | Six full variants, 1,176 streams; 16 gate and 196 full unset controls; strict FAAD3/FFmpeg | Joint improves versus inverse-filter alone; no demonstrated gain versus static noise |
| Frequency-local noise/inverse filtering | 1,176 full variant rows, four two-band variants; 16 gate unset controls; 1,568 unique strict decoder streams | Two-band variants regressed against matched one-band controls |
| Grid and envelope frequency resolution | Four full variants, 784 feature rows; 196 full unset controls; 1,176 strict decoder streams | Late FIXVAR regressed; center VARVAR and low-resolution policies inconclusive |
| Original-vs-source tonality missing-harmonic detector | Five full detector conditions, 980 detector rows; 196 unset controls; 1,764 strict decoder streams | Tone/ratio and midpoint no significant matched-static gain; max-energy ablation regressed |
| Frequency-table policy | Five one-factor gate settings; 16 unset controls; 112 strict decoder streams | No screening gain; no full follow-up |
| Adaptive envelope count 1/2/4 | Full corpus, fixed-count attribution, production validation, footprint and timing | Adaptive selector did not resolve a gain over fixed4; tier17 policy retained after low-rate outlier review |

Raw rows, configurations, patches, controls, provenance and decoder reports
are linked by [REPORT.md](REPORT.md) and [EXPERIMENTS.md](EXPERIMENTS.md).
The harmonic experiments complete those bounded comparator/threshold/energy
hypotheses; they do not establish universal detector calibration.

## Retained original production evidence

Tier17 uses one steady-frame envelope below 17 kbps/channel and four above;
transient VARFIX retains two. Its measured BD-rate was -1.685% versus the
matching base (95% CI [-2.208%, -1.161%]), -1.599% versus noise+limiter
([-2.090%, -1.112%]), and the three-feature combination -2.6185% versus
the frozen baseline ([-3.2800%, -1.9957%]). The cutoff was selected after
reviewing this corpus; no held-out evidence is claimed.

Packet/FDK PCM identity passed 196/196 per context. LC 16/16, HE
CBR/boundary/flush and decimation4 checks passed strict decoders. GCC13/LTO
ROM for the clean envelope step was +96 B over noise+limiter, BSS unchanged;
persistent stereo SBR state +6,176 B and local scratch capacity +1,024 B.
Native ARM64 timing used 40 alternating pairs: mean duration +3.680%,
95% paired CI [+0.984%, +7.390%], median +1.063%; slower on this workload.
See [production validation](envelope-count/tier17/production-final/PRODUCTION-VALIDATION.md),
[timing](envelope-count/tier17/final-production-timing-v3/TIMING-RESULTS.md),
[footprint](envelope-count/production-footprint/gcc13/tier17/CLEAN-FOOTPRINT.md)
and [combined quality](envelope-count/tier17/hybrid-vs-frozen-baseline.json).

Original noise/limiter matched GCC13.3 release/LTO ROM: baseline/noise
76,184 B, limiter/stack 76,328 B; BSS 20,552 B throughout. Original
noise+limiter timing: mean +0.445%, median +0.418%, paired CI
[-0.470%, +1.288%], median RSS 2,352 KiB both. No speed win.
These measurements belong to their recorded historical sources/toolchains.

Historical stacked heads were noise60bf043e, limiter5d1bd747 and envelope
8c5596ff. They were replaced with independent master-targeted PRs 63/64/66
and combined PR68 on upstream01e7. All 980 fresh packet/FDK PCM comparisons
passed. Their publication snapshot is [here](CURRENT-PUBLICATION.md).
Upstream subsequently merged all three features and an envelope-analysis
rewrite in PR47, commit `098e8900289b87d1592a859351ca0bd420397d4b`.
Current candidate comparisons use this merged baseline directly.

## Later experiment evidence and remaining work

Transient border mode1 completed 1,568 full rows with strict FAAD3/FFmpeg.
Fixed8 full attribution completed 784 rows; corrected 49-clip summary and
independent derivation are archived under
[fixed8-full-v2](transient-border/fixed8-full-v2/RESULTS.md).
Adaptive-vs-fixed8 has small resolved gains in the old base, but not the
noise+limiter context. The direct merged-context four-clip gate is now complete:
λ=.001 vs baseline −0.402% [−1.574,+0.646] and vs fixed8 +0.331%
[−0.280,+0.923]; λ=.003 vs baseline −0.509% [−1.869,+0.637] and vs fixed8
+0.221% [−0.578,+0.912]. All intervals cross zero. This bounded experiment
therefore did not establish the historical 49-clip adaptive gain on the merged
baseline. The frozen matched full49 λ=.001/.003 comparison is now complete:
versus baseline −0.6448767% [−0.981606,−0.349516] and −0.6637911%
[−0.989343,−0.391152]; versus fixed8 −0.1325196% [−0.333368,+0.063008] and
−0.1519271% [−0.323619,+0.014367]. Both improve against baseline; neither
resolves a gain over fixed8. All 392 active streams passed strict FAAD and
FFmpeg as well as FDK. Old/new FDK PCM equivalence is available for the separate
32-row adaptive gate, not asserted for this full adaptive matrix. No held-out
corpus.
The quality choice is fixed8 and adaptive is not retained. Details:
transient-border/upstream098-mode1-full49-v1/FULL-RESULTS.md. Fixed8 footprint,
native timing and smoke gates are complete; see
transient-border/upstream098-fixed8-gate-v2/FIXED8-PRODUCTION-RESULTS.md.
Implementation is published in PR #70 from upstream/master; build and benchmark
CI jobs are queued and are the only remaining gate.

The fresh merged-baseline fixed8 full comparison completed 392 original-
anchored rows: mean BD-rate −0.512570%, 95% CI [−0.762929%, −0.281702%].
All 392 passed FDK, strict FAAD and strict FFmpeg; new FDK decoded PCM was
byte-identical across the full fixed8 matrix (see
transient-border/upstream098-fixed8-gate-v2/validation-build/full392-normalized-equivalence.json).
Its raw evidence is under transient-border/upstream098-fixed8-gate-v2/full49/. Fixed8 footprint/timing,
smoke and publication are complete; PR #70 build/benchmark CI remains pending.

Persistent cross-frame mode2/future_pcm0/lambda.003 completed 1,176 strict
full streams: -1.631% versus base, -1.323% versus noise+limiter. Direct
comparison with tier17 was +0.164% [-0.451%, +0.740%] in base and +0.370%
[-0.167%, +0.890%] in stack. No resolved advantage over the simpler retained
policy, so this complex probe is not retained. Actual-future screens were
inconclusive. See the ledger and crossframe-border/full-x2-future0-lam003/.

Benchmark HEAD bdce18f fixes FAAD2 tail flushing and rebuilds FDK on libfaam.
Scorer provenance is audited separately in
[BENCHMARK-SCORER-AUDIT.md](BENCHMARK-SCORER-AUDIT.md); recorded study commits
contain the bounded-lag fix. Old/new FDK PCM is identical on 16 HE gate streams, 24 synthetic 16 kHz LC
tail cases, and the full 392-row fixed8 matrix.
Historical CI MOS cases and gapless failures require distinct attribution;
the FAAD2 fix explains a clean-master gapless failure, not automatically
all recorded MOS cases. No overall CI pass or merge-readiness claim.

This audit was reconstructed from the earlier captured completion evidence
after a scorer audit accidentally replaced the file. The scorer audit now
has its own path. Raw study reports, measurements and configs were preserved.
The full goal remains active until PR #70 build/benchmark CI completes and its
results are reviewed. No CI pass or merge-readiness is claimed while jobs are
queued.
