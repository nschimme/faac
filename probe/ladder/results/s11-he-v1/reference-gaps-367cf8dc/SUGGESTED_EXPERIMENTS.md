# Suggested follow-up experiments

These are proposals, not completed work. Refresh the comparison against the current PR70 candidate and matched upstream base before using the historical gaps below to prioritize implementation. Do not treat PR70 as having won this reference comparison; it is absent from the source matrix.

## Priority 1: 48 kHz HE, 40–64 kbit/s

The historical four-rung ladder (40/48/56/64) shows a FAAC deficit against both references: +6.933% BD-rate versus FDK 1.0.9 (47 clips; 95% CI [3.119%, 11.181%]) and +5.443% versus Apple AAC 27.0.1 (45 clips; [1.098%, 9.907%]). Positive means more bits are needed by FAAC for equal MOS. The exact excluded clips are listed in `analysis/equal-rate-gaps.json`; exclusions arise from non-overlapping curve ranges, not silent imputation.

First refresh these curves with the current encoder and identical 48 kHz stereo input/profile controls. The exact Apple HE 40/48/56/64 streams are available in [`apple-s11-367cf8dc`](../../../ref/apple-s11-367cf8dc); use only source WAVs whose hashes match its manifest. The Apple LC 96/128/160/192 reference streams are also included there; the 256/320 Apple streams are not packaged, so the historical 128–320 LC BD result remains readable from the archived metadata but a fresh cloud reproduction should use the complete Apple rung set only if those missing references are independently obtained and hash-verified. The proposed narrower LC 96/128/160/192 diagnostic can run with the packaged references. New Apple reference encoding requires macOS. The packaged 49-clip Apple corpus is diagnostic, not held out; it cannot support an Apple-parity held-out claim. New held-out FAAC baseline-versus-candidate comparisons can run without Apple, while held-out Apple parity requires matching Apple references for those new clips.

Archive encoder executable/source hashes, command lines, actual encoded bitrate, stream headers, decoder executable/version/hash, decoded PCM hashes, scorer revision/config, and per-clip MOS/rate rows. Use the same FAAD and FDK decode paths for all encoders, plus the established common scorer; preserve Apple as a quality reference. Keep the held-out clip set fixed before examining candidate results.

If the gap persists, isolate causes before changing code: split core AAC and SBR contributions through controlled, bitstream-valid diagnostic builds or measured instrumentation; inspect the worst clips and compare quantizer/core decisions against SBR allocation at the same delivered rate. Keep diagnostic builds out of the production comparison. Test the 40/48/56/64 ladder on the same clips and include held-out material for FAAC baseline-versus-candidate comparisons to detect overfitting. Do not use a single-rate MOS delta in place of bits-adjusted BD-rate.

Only if a refreshed quality ceiling confirms a specific core-quantization limitation should the existing parked scalefactor ±1 idea be repriced. This means evaluating the already-known candidate against current rate, quality, CPU, and memory costs; it does not propose a new search algorithm or revive DP/scalefactor path search. Any time-coordinate explanation is lower priority and should be investigated only if source-level derivation and instrumentation can establish a real mismatch.

## Priority 2: 48 kHz LC, 128–192 kbit/s

The historical five-rung 128/160/192/256/320 ladder favors FAAC against FDK by −11.672% BD-rate (48 clips; 95% CI [−14.651%, −8.603%]) and is worse than Apple by +6.949% (49 clips; [3.638%, 10.072%]). A narrower 128/160/192 follow-up can locate where the Apple gap emerges, but it is a diagnostic slice, not a replacement for the five-rung summary. Include 96/128/160/192/256/320 only when stream-header checks confirm that the compared rows share the intended 48 kHz stereo LC format; the historical Apple low-rate LC rows demonstrate why scenario labels alone are insufficient.

## Comparability and stop rules

For each refreshed comparison, freeze the same corpus, profile, target rungs, channel count, input format, encoder configuration, and scoring pipeline. Record actual output sample rate/profile/channel count from each encoded stream, then require the expected 48 kHz stereo format. Decode each stream with both FAAD and FDK under recorded builds and require successful strict decode before scoring. Compare same-clip rate-quality curves and report the number of valid curves, exclusions, confidence interval, and per-clip rows. Use exact hashes for sources, binaries, encoded streams, decoded PCM, and scorer inputs.

Apple AUTO can change the encoded sample rate at low target rates; never pool those streams into a 48 kHz LC curve. The raw matrix has cache-creation failures for some FAAC/FDK rows and unsupported Apple 5.1 cases; retain and classify invalids rather than relabeling them as quality failures. The historical matrix's dirty benchmark checkout and missing helper/decoder binary hashes limit provenance. A clean rerun is required before treating these numbers as a current baseline.

Adaptive SBR selection is not a proposed next step: the bounded current-base screen and full ladder did not establish a resolved advantage over the simpler fixed-slot candidate, while carrying additional persistent state and analysis complexity. Do not repeat that lever absent materially new evidence. No experiment described here has been executed.
