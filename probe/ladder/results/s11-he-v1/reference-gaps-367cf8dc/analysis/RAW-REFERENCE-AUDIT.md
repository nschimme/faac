# Raw reference benchmark comparison (read-only audit)

> **Current interpretation:** Use only the focused, actual-output-format results in “Correction: isolate actual encoded sample rate” near the end of this file. The earlier broad 48 kHz LC summary below pooled Apple LC outputs across different encoded sample rates, so those LC curves are invalid for a like-for-like 48 kHz comparison and their apparent LC advantage is superseded. The focused HE 40–64 kb/s and LC 128–320 kb/s table is the applicable historical result; it remains conditional on the provenance limits documented here and is not a result for PR70.

## Inputs and version/provenance limits

| Input | Size | SHA-256 | Top-level rows |
|---|---:|---|---:|
| `/Users/nschimme/gitprojects/faac-benchmark/comparison_results.json` | 22,245,787 B | `18524a20abfa82d83ea766807f743e97949d6e4a69e50765ca78ddd0f1602451` | 14,971 encoder rows |
| `comparison_results.json.decoders.json` | 94,364,369 B | `bb47060c3245a3da61eae132940ea4e43dfb96bfe78c31979b9c8d0c5dca3d87` | 89,556 decoder rows + 21,780 corrupt-stream robustness rows |

The FAAC tool label is `FAAC 3.0.0 (367cf8dc)`; the local FAAC checkout is exactly `367cf8dc5e76085b02668d3b212fe6576001ee07`, branch `libfaad`. The benchmark checkout is `bdce18f7123f51c31795afe9fb4571c18f507a77`, branch `master`; its working tree is dirty in `codec_bench/decoders.py` and `utils.py` and has untracked `output/` and `scripts/vbr_consistency.py`. None was modified for this audit.

Encoder comparison rows record `decoder_name=FFmpeg AAC` and `decoder_id=ffmpeg_aac` on every valid row. They do not embed an FFmpeg binary hash, a scorer/helper hash, or a run-level benchmark commit. The decoder sidecar separately labels the FFmpeg decoder as `FFmpeg AAC N-127192-gef52e1cc38`. The current benchmark source commit therefore documents the available checkout but does not, by itself, prove which exact decoder binary/scorer produced these historical result rows. Treat MOS comparisons below as conditional on the raw matrix's common FFmpeg AAC scoring; refresh the evidence before making a release-quality claim.

## Matrix coverage and invalid rows

There are four encoder tool labels across 34 configured scenarios. The row keys are stable per tool/profile:

| Tool | Row keys / profile counts | Rows | Valid / invalid | Notes |
|---|---|---:|---:|---|
| FAAC 3.0.0 (367cf8dc) | `faac_3_0_0__367cf8dc__lc` 2,723; `...__he` 1,133 | 3,856 | 3,842 / 14 | No HEv2 reference from FAAC. All 14 invalid rows have no MOS or actual rate and report `[Errno 17] File exists` while creating `output/ref_cache/...`; these are harness/cache task failures, not decoded-candidate failures. |
| fdkaac 1.0.9 | `fdkaac_1_0_9_lc` 2,723; `..._he` 1,133; `..._hev2` 1,127 | 4,983 | 4,958 / 25 | All 25 invalid rows have no MOS/rate and the same `File exists` reference-cache failure. |
| Apple AAC 27.0.1 | `apple_aac_27_0_1_lc` 2,723; `..._he` 441; `..._hev2` 245 | 3,409 | 3,403 / 6 | All six invalid rows are 5.1 `6_Channel_ID.wav` encoding failures (`ExtAudioFileSetProperty ('cfmt') failed ('!dat')`). |
| FFmpeg AAC N-127192-gef52e1cc38 (NMR experimental) | `ffmpeg_aac_n_127192_gef52e1cc38__nmr_experimental__lc` 2,723 | 2,723 | 2,723 / 0 | LC only. |

Profiles are `lc`, `he`, `hev2`; the 34 scenario set comprises clean 16 kHz mono (2 rates), 16 kHz VoIP mono (1), clean 24 kHz mono (2), 32 kHz stereo (5), 44.1 kHz stereo (5), 48 kHz stereo (13), and 44.1 kHz 5.1 (6). Matched valid FAAC/reference rows are 2,686 (FDK-LC), 1,131 (FDK-HE), 2,705 (Apple-LC), and 441 (Apple-HE). Apple HE has only the 48 kHz 24–64 kb/s six-rung set. No FAAC HEv2 comparison is possible.

The 5.1 family has only one common usable clip and no meaningful per-clip BD-rate distribution. The clean 16 kHz and 24 kHz mono ladders have only two rates, and the 16 kHz VoIP family one rate, so they are descriptive matched-rate comparisons only. The 3+ rung comparisons below are split by corpus, sample rate, channel count, and exact profile to avoid crossing LC/HE/HEv2 transitions.

## Matched comparisons

Method: pair the same `scenario` and `filename`; retain only rows with `decode_valid=true`, finite MOS, and positive finite `actual_bitrate`. In `compare_codecs.py`, `actual_bitrate` is calculated from extracted AAC elementary-stream bytes divided by source audio duration, not total M4A/MP4 size. For every profile/sample-rate/channel corpus with at least three common rungs, compute per-clip PCHIP BD-rate over the overlapping MOS range using actual ES bitrate; positive means FAAC needs more bits than the reference for equal MOS. Reported intervals are 5,000-resample percentile bootstrap CIs over the paired clips (seed `20261015`). Raw MOS and ES-rate deltas are descriptive means/medians across the matched same-target rows; they are not a bitrate-adjusted score.

### FAAC vs FDK AAC

| Profile / subset | Common rungs | Paired clips with BD | FAAC BD-rate vs FDK mean (median), 95% bootstrap CI | Same-target FAAC−FDK MOS mean (median) | FAAC ES-rate delta mean (median) |
|---|---:|---:|---:|---:|---:|
| LC, 32 kHz stereo | 5 | 49 | −37.52% (−37.48%), [−40.01, −35.01] | +0.2616 (+0.0595) | −1.75% (−2.05%) |
| LC, 44.1 kHz stereo | 5 | 48 | −8.82% (−9.30%), [−11.90, −5.94] | +0.0479 (+0.0239) | −1.56% (−1.45%) |
| LC, 48 kHz stereo | 13 | 48 | −25.14% (−26.80%), [−28.47, −21.65] | +0.3439 (+0.0677) | +0.06% (−1.63%) |
| HE, 32 kHz stereo | 5 | 47 | −6.90% (−7.99%), [−11.10, −2.59] | +0.1006 (+0.0248) | −1.96% (−2.10%) |
| HE, 44.1 kHz stereo | 5 | 48 | −4.58% (−2.62%), [−9.51, −0.04] | +0.0610 (+0.0594) | −3.45% (−1.85%) |
| HE, 48 kHz stereo | 13 | 49 | −2.82% (−2.32%), [−6.44, +0.70] | +0.0639 (+0.0344) | −3.38% (−2.04%) |

### FAAC vs Apple AAC

| Profile / subset | Common rungs | Paired clips with BD | FAAC BD-rate vs Apple mean (median), 95% bootstrap CI | Same-target FAAC−Apple MOS mean (median) | FAAC ES-rate delta mean (median) |
|---|---:|---:|---:|---:|---:|
| LC, 32 kHz stereo | 5 | 49 | −42.53% (−44.36%), [−45.22, −39.97] | +0.4393 (+0.2541) | −0.59% (−1.02%) |
| LC, 44.1 kHz stereo | 5 | 48 | −7.71% (−7.43%), [−10.71, −4.97] | +0.0740 (−0.0051) | −1.10% (−1.92%) |
| LC, 48 kHz stereo | 13 | 48 | −28.94% (−29.67%), [−32.27, −25.73] | +0.4690 (+0.1685) | +0.91% (−2.04%) |
| HE, 48 kHz stereo | 6 | 48 | +2.67% (+3.49%), [−1.47, +7.09] | +0.1020 (−0.0529) | −2.44% (−1.95%) |

These values do not show an LC quality deficit for the recorded FAAC build: all LC BD means are negative, with the recorded confidence intervals below zero. HE is much closer; FAAC's advantage over FDK at 48 kHz is inconclusive, and the Apple-HE interval also crosses zero. That is the measured result for these files and this scorer, not a general encoder ranking. The current matrix gives no statistical basis for prioritizing a broad LC-quality fix; investigate exact run provenance and verify the raw MOS matrix before turning this into a feature choice. The apparent larger LC deltas at 32/48 kHz should be checked against freshly reproduced runs because there is no run-level decoder/helper hash in the JSON.

## Decoder and corrupt-stream sidecars

`decoder_results` has 14,926 rows each for FAAD 2.11.3, FAAD3, FFmpeg AAC N-127192-gef52e1cc38, Apple AAC 27.0.1, Helix AAC 1.0, and FDK AAC 2.0.0. There are 89,535 decode-valid rows and 21 invalid: FAAD 2.11.3 timed out once on an FDK HE stream; Helix had 20 “Error opening input” failures (6 FDK HE, 5 FDK LC, 4 FAAC LC, 3 FAAC HE, 2 FFmpeg experimental LC). FAAD3, FFmpeg AAC, Apple AAC and FDK AAC have no invalid rows in this table. Forty-four sidecar decoder-result MOS fields are null. Decoder-side MOS is mixed: 49,802 records are labelled `inherited`, 39,733 `scored`, and 21 have no source. Do not combine these values as if they came from one decoder.

The 21,780 robustness rows are deliberate corrupted-bitstream trials (3,630 per decoder); `passed` means the decoder returned success on corrupted input, while `crash_free` is the safety property. There are 340 crashes and 150 timeouts, all for Helix AAC 1.0; no runaway outputs were recorded. The other five decoder implementations are crash-free across their recorded corrupted-stream trials. A non-passing corrupted-stream test can be a clean error exit and must not be described as an interop failure on valid streams.

## Conclusions for S11

This read-only reference comparison does not identify a consistent measured MOS/BD-rate deficit for FAAC 3.0.0 (367cf8dc) on the paired LC ladders; it shows a small/inconclusive HE gap at 48 kHz and no FAAC HEv2 row family. It is not directly comparable to S11’s prior fixed8 feature experiments unless the source commit, corpus version, scorer helper, and decoder binaries are verified identical. Next useful step is provenance-checked reproduction of the LC and HE paired ladders (including the invalid task rows) with immutable decoder/scorer hashes, then inspect where per-clip results diverge. This report is exploratory evidence only; it does not alter the S11 ledger or recommend a codec change.

## Correction: isolate actual encoded sample rate before interpreting the gaps

The broad LC ladder above pooled Apple AAC rows whose encoded output sample rate changed with target rate. The requested profile in the encoder row is not sufficient to establish like-for-like LC comparison. I verified the 588 Apple 48 kHz stereo stream-header records in `/private/tmp/faac-reference-gap-analysis/apple48-stream-validation.json`: all 49 clips at 48 kHz targets 24/32 kb/s were HE-AAC with output rates 22.05/32 kHz; all 49 clips at HE targets 40/48/56/64 kb/s and LC targets 96/128/160/192/256/320 kb/s were 48 kHz stereo. An additional direct `ffprobe` check of existing read-only Apple files confirmed the LC 48 kHz-scenario outputs at 32/48/96 kb/s were LC at 12/24/48 kHz respectively. Therefore the prior broad 48 kHz LC curve included sample-rate changes and must not be used to claim a broad LC advantage. It also does not answer the user's narrower high-rate LC / 40–64 kb/s HE question.

I read the focused analyzer at `/private/tmp/faac-reference-gap-analysis/analyze.py` and independently recomputed its four BD-rate curves from the raw encoder rows, Apple stream-validation filter, current `bd_rate_curve` PCHIP helper, and actual AAC elementary-stream bitrate. The independently calculated means and bootstrap intervals exactly match its `equal-rate-gaps.json` results:

| Matched 48 kHz stereo profile/rungs | Reference | Valid per-clip curves | FAAC BD-rate vs reference, mean (95% bootstrap CI) | Interpretation |
|---|---|---:|---:|---|
| HE, 40/48/56/64 kb/s | FDK AAC 1.0.9 | 47 | +6.933% [ +3.119%, +11.181% ] | FAAC needs more ES bits than FDK for equal MOS on this matched HE range. |
| HE, 40/48/56/64 kb/s | Apple AAC 27.0.1 | 45 | +5.443% [ +1.098%, +9.907% ] | FAAC needs more ES bits than Apple for equal MOS on this matched HE range. |
| LC, 128/160/192/256/320 kb/s | FDK AAC 1.0.9 | 48 | −11.672% [ −14.651%, −8.603% ] | FAAC uses fewer ES bits than FDK for equal MOS in this high-rate LC range. |
| LC, 128/160/192/256/320 kb/s | Apple AAC 27.0.1 | 49 | +6.949% [ +3.638%, +10.072% ] | FAAC needs more ES bits than Apple for equal MOS in this high-rate LC range. |

The mean sign convention remains positive = FAAC requires more bits for equal MOS. The HE analysis uses four common 48 kHz output-rate rungs; LC uses five. The focused Apple filtering excludes the 12/22.05/24/32 kHz encoded streams. The weak broad-LC conclusion above is superseded by this corrected, range-specific result. The strongest measured reference gap in the validated 48 kHz domains is Apple-vs-FAAC high-rate LC and both references-vs-FAAC HE at 40–64 kb/s. This identifies a useful target for matched reproduction, not a proven source lever. S11 history already records that quantizer execution remains a candidate quality limiter and prior new SBR/transient adaptive arms were rejected; any follow-up should first verify the encoder/scorer/decoder provenance and compare high-rate LC / HE against the exact same validated output-rate bands. Do not reopen the rejected adaptive SBR experiment from these results.

Per-rate equal-ES-rate gaps in `equal-rate-gaps.json` give additional resolution: HE vs FDK has reference-minus-FAAC means +0.060/+0.023/+0.064/+0.048 MOS at 40/48/56/64 kb/s (the 48 kb/s interval crosses zero); HE vs Apple means are +0.065/+0.024/+0.023/+0.030 (each interval crosses zero at the per-rate level). High-rate LC vs FDK means are −0.022/−0.034/−0.021/−0.024/−0.028 at 128/160/192/256/320 kb/s; LC vs Apple means are +0.007/+0.009/+0.012/+0.003/+0.002. These are per-rate descriptive interpolation checks; the multi-rung BD-rate rows above are the primary bitrate-adjusted comparisons.

The separately recorded decoder-sidecar comparison preserves the HE deficit direction; using the FDK decoder reduces its magnitude by about 0.02 MOS. Treat that as a sensitivity check only: sidecar MOS provenance is mixed (`inherited` versus `scored`), and the matrix does not bind the rows to a decoder executable hash. It is not a second independently validated quality result.
