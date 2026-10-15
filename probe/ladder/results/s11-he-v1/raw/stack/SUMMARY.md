# Static-noise and gated-limiter stack

The isolated probe is based on upstream/master `99ce2b0980180194f9ccd1993a786208d277cf3e`. The candidate enables the SBR extra-2 header only when `numEnvFixFix > 1`; its unset noise override defaults to level 12. Levels 18 and 24 were measured independently and in combination with the limiter setting.

The gate covered four 48 kHz stereo music clips at 32, 48, 64, and 96 kbps. Full results covered 49 clips at the same rates. Every stream forced HE-AAC v1 and had its profile checked. Strict FDK decoding preceded scoring; actual bitrate uses AAC packet bytes divided by source duration, and BD-rate uses the benchmark PCHIP helper with clip-bootstrap intervals.

| Setting | Mean BD-rate vs original | 95% clip-bootstrap interval | Valid clips |
|---|---:|---:|---:|
| Limiter only | -0.540% | [-0.946%, -0.195%] | 49/49 |
| Static noise 18 | -0.607% | [-1.012%, -0.292%] | 49/49 |
| Static noise 24 | -0.625% | [-1.033%, -0.304%] | 49/49 |
| Stack at noise 18 | -1.107% | [-1.626%, -0.667%] | 49/49 |
| Stack at noise 24 | -1.117% | [-1.643%, -0.676%] | 49/49 |

Direct comparison against the corresponding static-floor setting measured -0.531% BD-rate for stack18 versus static18 (95% CI [-0.948%, -0.175%]) and -0.526% for stack24 versus static24 ([-0.946%, -0.172%]). Both intervals fall below zero in this corpus. Stack18 and stack24 are close in aggregate; no evidence here establishes a meaningful difference between those two floors.

All 196 unset-candidate rows matched the original baseline at both AAC packet and strict FDK-decoded PCM identity. The production candidate, commit `5d1bd747` on PR #64, matched the measured stack18 packets and FDK-decoded PCM for all 196 rows, permitting reuse of the stack18 MOS values. Strict FAAD3 and FFmpeg each accepted all 1,060 stack variant streams across gate and full-corpus runs.

Matched GCC 13.3.0 x86_64 release/LTO builds measured 76,184 B ROM and 20,552 B BSS for baseline and noise-only; the limiter-only and stacked builds measured 76,328 B ROM and the same BSS. The production stack was also timed natively on Apple ARM64 with the benchmark's `measure_peak_ram` helper: 20 alternating pairs per clip at 64 kbps, using two source clips repeated by direct PCM concatenation to 120 seconds.

| Clip set | Pairs | Mean duration delta | Median duration delta | 95% pair-bootstrap CI | Mean peak-RSS delta | Median peak-RSS delta |
|---|---:|---:|---:|---:|---:|---:|
| sandman | 20 | +1.334% | +1.703% | [-0.361%, +2.735%] | +0.037% | 0% |
| fms | 20 | -0.444% | -0.369% | [-1.122%, +0.212%] | +0.241% | +0.338% |
| Pooled | 40 | +0.445% | +0.418% | [-0.470%, +1.288%] | +0.139% | 0% |

Both duration intervals cross zero; this run shows no speed gain. Peak RSS is reported separately: pooled median was 2,352 KiB for both baseline and candidate. The benchmark helper's child `ru_maxrss` measurement is converted to KiB on macOS. The run does not establish amd64 performance, CI status, or listening-test results.

The run provenance is in [stack-config.json](results-all/stack-config.json), including the source-tree hash `8a2ba51ef0feda364dcd2490f93c4bf254cfee202412f2e3b8e7339e742d5d4f`. The candidate binary hash was `c0dc1e7420c70657307af24d866786c3d15e37fee919834d43cd37157d7529b9`; the full row set hash is `87a61b6147480dfc1f4cf96d8c64d03c51e57c35070366f50b5bae5440ce2530`. The isolated patch SHA-256 is `1437d7efc47a2213fb6e2ab499398f0922326740ccba164f251d50b56c515744`. The production equivalence record has its own binary, reference-row, packet, and PCM hashes.

See [full results](results-all/RESULTS.md), [gate results](results-gate/RESULTS.md), [decoder validation](interop-results.json), [production equivalence](production-equivalence/production-equivalence.json), [matched footprint](matched-gcc13/footprint.json), and [probe patch](stack-probe.patch). Timing commands, inputs, hashes, raw pairs, and summary are in [timing-serial](timing-serial/). Encoded streams, decoded PCM, runner scripts, and configurations are archived beside these summaries.
