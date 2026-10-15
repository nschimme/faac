# Adaptive transient-border mode1 on merged upstream 098

Status: quality and strict interop complete. This is a full-corpus confirmation of the two frozen four-clip settings, not a held-out evaluation.

## Comparison

Encoded 49 clips at 32, 48, 64, and 96 kb/s with λ=.001 and λ=.003 (392 adaptive streams total). Scoring used the original source WAV anchors, actual AAC payload bitrate, and benchmark Zimtohrli MOS. Each candidate is compared with the exact merged-098 baseline and the fresh merged-098 fixed8 full49 rows.

| λ | vs merged baseline BD-rate (95% clip-bootstrap CI) | vs merged fixed8 BD-rate (95% clip-bootstrap CI) |
|---|---:|---:|
| .001 | −0.6448767% [−0.981606%, −0.349516%] | −0.1325196% [−0.333368%, +0.063008%] |
| .003 | −0.6637911% [−0.989343%, −0.391152%] | −0.1519271% [−0.323619%, +0.014367%] |

Both adaptive settings improve versus the merged baseline. Neither resolves a gain over fixed8 because both fixed8 comparison intervals include zero. There is no held-out corpus. This supports the simpler fixed8 choice on current evidence; it does not establish adaptive superiority.

## Decoder and provenance gates

All 392 streams are HE-AAC. The frozen scoring run used the new FDK decoder; strict FAAD and FFmpeg each passed 392/392 rows with zero errors. New/old FDK equivalence is separately established for the 32-row adaptive gate, not this 392-row matrix. This quality result is separate from production speed/footprint evidence.

- [Raw rows](rows.json), SHA-256 `2a2de9485eb7ecc2dc9fff5b70c1afa39120feadccf5df5a8e483ee15e855c85`
- [Scoring summary](summary.json)
- [Strict interop results](strict-interop.json)
- [Run config](config.json)
- [Provenance](provenance.json)
- [Scoring runner](run_full.py)
- [Strict interop runner](run_strict_interop.py)

No tuning followed the four-clip screen. The follow-up uses only its frozen λ values. Remaining candidate gates are fixed8 production footprint/timing, smoke/workflow CI, and publication review.
