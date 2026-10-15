# Final production tier17 stack serial timing

The final native ARM64 production build was measured against the retained noise18+gated-limiter stack. Each clip was repeated by direct PCM concatenation to 120 seconds; 20 alternating AB/BA pairs per clip were encoded at 64 kbps. The benchmark helper measured each child process separately with `utils.measure_peak_ram`; duration and peak RSS are reported as separate metrics.

The tier17 candidate’s mean paired duration delta was **+3.680%** (median +1.063%), with 95% paired-sample bootstrap CI [+0.984%, +7.390%]. Positive values mean the candidate took longer. Both clip-level mean CIs are above zero. These measurements show a duration increase on this ARM64 host; they do not establish amd64 behavior.

| Clip | Pairs | Mean duration delta | Median duration delta | 95% bootstrap CI | Mean peak-RSS delta | Median peak-RSS delta |
|---|---:|---:|---:|---:|---:|---:|
| fms.wav | 20 | +4.339% | +0.978% | [+0.814%, +10.027%] | +0.715% | +0.678% |
| sandman.16b48k.wav | 20 | +3.021% | +1.128% | [+0.305%, +7.656%] | +0.884% | +0.676% |

Across 40 paired measurements, mean peak-RSS delta was +0.800% and median was +0.676%. Three pairs showed large positive duration deltas (>10%): sandman pair 4 +44.281%, fms pair 5 +47.608%, and fms pair 7 +19.136%. They remain included in the summaries.

The candidate frontend SHA-256 is `7741458a69d12c7ceabd7e0b2b3e60f85f74620f29a28c7be49fb2f1c6b32d5d` and its loaded libfaac SHA-256 is `32609f1838ef268fcbcb2c9f4fa9e72b7ee5ad5a98e5416ca66dadf1cead0ca6`. The source patch SHA-256 is `d109b48eedec1990d3dd76148be1fa8b224192ccc8fac869c17bd39d35d86f34`; the candidate matched all 196 production packet and strict-FDK-PCM equivalence rows before timing. The quality curve is post-hoc: retained stack at 32 kbps and `all_stack` at 48/64/96; it has no held-out claim.

Machine-readable provenance and observations: [`timing-config.json`](timing-config.json), [`timing-results.json`](timing-results.json), [`timing-start.json`](timing-start.json).
