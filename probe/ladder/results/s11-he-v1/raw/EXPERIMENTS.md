# HE-AAC v1 experiment completion ledger

Frozen encoder base: upstream/master 99ce2b0980180194f9ccd1993a786208d277cf3e.
Completion means a reviewed probe, exact unset controls, measured quality against matched controls, decoder checks, and archived reproducible evidence. A failed quality screen completes a hypothesis; it does not justify shipping. Promising screens require full-corpus confirmation. Production speed/footprint gates apply to retained candidates.

| Family | Status | Evidence or next measurement |
|---|---|---|
| Static noise, constant inverse-filter modes, limiter/header controls | Complete | REPORT.md; gate and full records |
| Harmonic energy-concentration heuristic | Complete, no demonstrated gain | gate records; does not test source tonality |
| Single-band adaptive noise | Complete, rejected versus static controls | adaptive/SUMMARY.md |
| Single-band adaptive inverse filtering and joint noise | Complete, no demonstrated gain over static controls | invf/full/SUMMARY.md |
| Missing harmonics from original vs patched-source tonality | Complete, no demonstrated gain over matched static18; max-energy ablation regressed | [harmonic summary](harmonic/SUMMARY.md), [full results](harmonic/full/RESULTS.md), [matched comparisons](harmonic/full/matched-comparisons.json), [strict decoder results](harmonic/full/interop-results.json) |
| Static noise plus gated limiter interaction | Complete | stack/SUMMARY.md; full-corpus stack18/24 and paired comparisons to static18/24; production output identity verified 196/196 |
| Frequency-local noise and inverse filtering | Complete, all four two-band full-corpus variants regressed against matched static controls | [localnoise summary](localnoise/SUMMARY.md), [full comparisons](localnoise/full/direct-comparisons.json), [strict decoder results](localnoise/full/interop-results.json) |
| Transient grid and envelope frequency resolution | Complete, late FIXVAR regressed; remaining settings inconclusive | [grid summary](grid/SUMMARY.md), [full results](grid/full/RESULTS.md), [strict decoder results](grid/full/interop-results.json) |
| Frequency-table policy | Complete, no demonstrated gain | tables/SUMMARY.md; four-clip screen of scale1/2/3 and 16/18 kHz stop targets; no full-corpus follow-up |
| Adaptive SBR envelope count (1/2/4) | Complete; tier17 hybrid merged upstream in PR #47 | Full 49-clip, four-rate PCHIP/clip-bootstrap evidence. Tier17 hybrid (1 envelope below 17 kb/s/channel, 4 above; VARFIX transients unchanged) improved over its matching upstream base by -1.685% (95% CI [-2.208%, -1.161%]) and over the retained noise+limiter stack by -1.599% ([-2.090%, -1.112%]). Versus frozen upstream the hybrid measured -2.6185% ([-3.2800%, -1.9957%]); this is post-hoc with no held-out set. The earlier unconstrained adaptive selector did not resolve a gain over fixed-4. See [full adaptive results](envelope-count/full-active-lambda/RESULTS.md), [fixed-control attribution](envelope-count/fixed-count-controls/full/RESULTS.md), [tier17 production validation](envelope-count/tier17/production-final/PRODUCTION-VALIDATION.md), [final timing](envelope-count/tier17/final-production-timing-v3/TIMING-RESULTS.md), [matched footprint](envelope-count/production-footprint/gcc13/tier17/CLEAN-FOOTPRINT.md), and historical fork PR #66 (closed; merged upstream in PR #47). Production timing was +3.680% mean duration (95% paired CI [+0.984%, +7.390%]); GCC13/LTO ROM was +96 B over the retained stack parent, with +6176 B persistent stereo SBR state and +1024 B local scratch. Base workflow CI remains unverified. Structural-only lambdas `.01/.03/.1` collapsed to one envelope and were not quality-scored; preregistered `.0005/.001/.003` histograms are [here](envelope-count/structural-synthetics-active-weights/RESULTS.md). |
| Transient-specific border placement | Full merged-baseline quality comparison complete; adaptive not retained over simpler fixed8 | Historical 49-clip mode-1 run: negative matched-reference BD-rate CIs for all four preregistered λ/context settings (1,568/1,568 strict FAAD3/FFmpeg); old-base adaptive vs fixed-8 gains were −0.311% [−0.553,−0.082] and −0.345% [−0.598,−0.121], while old noise+limiter stack comparison was inconclusive. Fresh merged-098 four-clip screen was inconclusive. Follow-up full49 λ=.001/λ=.003 versus baseline: −0.6448767% [−0.981606,−0.349516] and −0.6637911% [−0.989343,−0.391152]; versus merged fixed8: −0.1325196% [−0.333368,+0.063008] and −0.1519271% [−0.323619,+0.014367]. Both fixed8 CIs cross zero, so no resolved adaptive advantage; provisional quality choice is fixed8. All 392 candidate streams were decoded by FDK and passed strict FAAD3/FFmpeg. The adaptive 32-row gate, not this full matrix, has old/new FDK PCM equivalence. No held-out corpus. See [merged-context full results](transient-border/upstream098-mode1-full49-v1/FULL-RESULTS.md), [four-clip screen](transient-border/upstream098-mode1-gate/scored-v2/summary.json), [historical mode-1 results](transient-border/full-mode1-final/FULL-RESULTS.md), and [fixed8 full results](transient-border/upstream098-fixed8-gate-v2/full49/review-summary.json). |
| Cross-frame lookahead or persistent framing | Quality study complete; probe not retained versus the simpler current four-envelope policy | Four-clip screen was followed by a 49-clip, four-rate full run in base and retained-stack contexts. Full measured BD-rate was −1.631% [−2.213%, −1.119%] vs base and −1.323% [−1.829%, −0.850%] vs stack; 1,176/1,176 streams passed strict FAAD3/FFmpeg. Direct tier17 comparison was +0.164% [−0.451%, +0.740%] in base and +0.370% [−0.167%, +0.890%] in stack, so there is no resolved advantage over the simpler policy. No held-out audio. The detector uses a distinct future_pcm=0 policy and is not a pure future-data ablation. See [gate results](crossframe-border/gate-v2/RESULTS.md), [full results](crossframe-border/full-x2-future0-lam003/RESULTS.md), and [full config](crossframe-border/full-x2-future0-lam003/config.json). |

Stereo coupling and envelope delta selection are already implemented and priced by exact bits in the base. Decoder-only PS/v2, low-power/downsampled synthesis, CRC/error concealment and decoder optimizations do not create HE-v1 encoder-quality experiments.

Each report will retain actual packet bitrate, forced HE profile, FDK decode before scoring, benchmark Zimtohrli audio-mode scores, clip-level BD-rate confidence intervals and strict FAAD3/FFmpeg acceptance. Full M4A metadata is excluded from AAC packet identity. No learned classifier or reference encoder source is used.

The original bounded study set and envelope-count quality/production gates are
complete. Fork PRs #66 and #68 are closed; noise18, gated limiter, and tier17
merged upstream in PR #47. The transient-border adaptive full49 comparison is
complete and does not resolve a gain over fixed8. Cross-frame lookahead/
persistent framing is a completed, unretained study. This ledger does not claim
that every possible SBR design was explored or that the missing-harmonic
heuristic is fully calibrated. See [completion audit](COMPLETION_AUDIT.md) for
the original study's scope, controls, decoder evidence, and timing limits.

Current unstacked PR heads and validation status are recorded in [the publication snapshot](CURRENT-PUBLICATION.md).

## Merged baseline and remaining follow-ups

Upstream `FreewareAdvancedAudio/faac` merged the noise18, gated limiter and
tier17 envelope policy together in PR #47, commit
`098e8900289b87d1592a859351ca0bd420397d4b`. This also rewrites envelope-energy
accumulation. The independent/combined draft PR descriptions above are
historical; follow-up candidate quality comparisons have now been run directly
against this merged baseline.

The initial combined fixed8 scorer used a compressed reference decode as its
MOS anchor and stopped without producing a valid BD-rate verdict. The
corrected scorer uses original WAV inputs and preserves the failed evidence.
Its archived four-clip gate measured -0.7264% BD-rate, with 95% clip-bootstrap
interval [-1.2391%, -0.3273%]. This is a screen, not full-corpus confirmation
on the merged baseline. See
[scorer audit](transient-border/upstream-fixed8-validation/scorer-audit.md).

The fresh merged-baseline fixed8 full-corpus comparison completed: mean
BD-rate −0.512570%, 95% CI [−0.762929%, −0.281702%], over 49 clips × 4 rates
(392 rows). All rows passed FDK, strict FAAD, and strict FFmpeg; old/new FDK
PCM was byte-identical on this complete fixed8 matrix. See the archived
[normalized equivalence report](transient-border/upstream098-fixed8-gate-v2/validation-build/full392-normalized-equivalence.json), [full49 review summary](transient-border/upstream098-fixed8-gate-v2/full49/review-summary.json)
and [strict interop evidence](transient-border/upstream098-fixed8-gate-v2/full49/strict-interop.json).

The adaptive mode-1 four-clip gate was followed by a frozen full49 λ=.001/.003
comparison against merged baseline and fixed8. Both variants improved against
baseline, but neither resolved an advantage over fixed8. The quality choice is fixed8; adaptive mode1 is not retained. Production
footprint, native timing, smoke, and 16 kHz LC tail diagnostics are complete;
see [fixed8 production results](transient-border/upstream098-fixed8-gate-v2/FIXED8-PRODUCTION-RESULTS.md).
The implementation is published as [PR #70](https://github.com/nschimme/faac/pull/70),
a new branch from upstream/master. Its build and benchmark CI jobs are queued;
CI is the only remaining gate. Historical ARM64 VBR/CBR CI quality failures
remain separate evidence and are not dismissed as harness failures.
