# PR70 benchmark CI terminal result

Run [38659018069](https://github.com/nschimme/faac/actions/runs/38659018069) on candidate `c50b66aab79f754fc17e2550159a861d26403b65` is completed with overall conclusion **failure**. The separate build run 38659018090 passed all ten jobs. This record separates GitHub job conclusions from report-level quality warnings and gate results.

| Job | Final job result | Report/gate detail |
|---|---|---|
| Footprint | Success | `.text + .rodata + .data` 76,040→75,992 bytes (−48 B, −0.06%). |
| arm64 CBR | Success | The report lists three MOS warning rows. |
| amd64 ABR | Success | Mean MOS Δ +0.001; HE-AAC v1 48 kHz BD-rate −0.108%; throughput −0.0%; four MOS warning rows below −0.05. |
| arm64 VBR | Failure | The report's gapless gate lists three nonzero-lag HE-v1 streams. Baseline and candidate report identical lags; this does not prove decoded PCM alignment or the underlying cause. |
| Consolidated benchmark | **Failure** | The final artifact headline is “Gapless Regression Detected.” |

The ABR rows that the per-rate report calls MOS regressions (strictly below −0.05 based on full-precision paired values) are:

| Case | Baseline MOS | Candidate MOS | Δ MOS |
|---|---:|---:|---:|
| `48k_stereo_24k_clapton_44k.16b48k.wav` | 3.532845 | 3.436158 | −0.096686 |
| `32k_stereo_16k_clapton_44k.16b48k.wav` | 2.952197 | 2.885615 | −0.066582 |
| `48k_stereo_16k_Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav` | 2.556375 | 2.489820 | −0.066555 |
| `48k_stereo_24k_bas.wav` | 2.769587 | 2.716029 | −0.053557 |

The ABR artifact marks the MOS metric with a warning for four clips past −0.05, while the ABR job and executive MOS pillar are reported as passing. The full report's consolidated failure is retained and is not reclassified as a passing run.

For VBR, the paired report rows remain in [`vbr-gapless-paired-cases.json`](vbr-gapless-paired-cases.json); the three lag values (−964, −391, −89) match baseline and candidate. Both reports record `frame_error: 0`, `elst: true`, and `itunsmpb: true`. The artifacts contain JSON/Markdown, not AAC or decoded PCM. This supports only equality of reported lag values, not a sample-exactness or root-cause conclusion.

The quality suite source resolved to `faac-benchmark` commit `bdce18f7123f51c31795afe9fb4571c18f507a77`. The native workflow footprint result (−48 B) is distinct from the local emulated GCC13/LTO footprint (−16 B); do not merge the measurements. Complete job/artifact API snapshots, artifact IDs/digests, reports, and authenticated ABR/VBR logs are preserved in this directory. This is a terminal CI record, not an all-green or merge-readiness statement. PR #70 remains a draft pending root review.
