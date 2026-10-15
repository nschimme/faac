# HE-AAC v1 SBR probe results

Input: `/private/tmp/faac-he-v1-probes/probe/results-all/feature-results.json`; runner summary: `/private/tmp/faac-he-v1-probes/probe/results-all/feature-summary.json`.
Corpus: **49 clips**; bitrate rungs: 32k, 48k, 64k, 96k.

BD-rate is computed by the benchmark's PCHIP helper from actual AAC packet payload rates and Zimtohrli MOS. Per-clip BD-rate values are the independent units for bootstrap intervals; bitrate rungs are not treated as independent samples.

## Per-variant assessment

| Variant | Packet identities changed | Valid BD clips | Mean BD-rate (95% bootstrap CI) | Median BD-rate (95% bootstrap CI) | Assessment |
|---|---:|---:|---:|---:|---|
| candidate_default | 0/196 | 49/49 | +0.000% [+0.000%, +0.000%] | +0.000% [+0.000%, +0.000%] | aggregate BD-rate CI crosses zero: inconclusive |
| defaults_explicit | 196/196 | 49/49 | -0.063% [-0.256%, +0.129%] | -0.089% [-0.281%, +0.013%] | aggregate BD-rate CI crosses zero: inconclusive |
| invf_0 | 196/196 | 49/49 | +0.229% [-0.331%, +1.063%] | -0.064% [-0.261%, +0.013%] | aggregate BD-rate CI crosses zero: inconclusive |
| invf_1 | 196/196 | 49/49 | +0.145% [-0.237%, +0.794%] | -0.094% [-0.113%, -0.045%] | aggregate BD-rate CI crosses zero: inconclusive |
| limiter_gain_3 | 196/196 | 49/49 | -0.672% [-1.319%, -0.181%] | -0.391% [-0.613%, -0.019%] | aggregate BD-rate CI below zero: statistically improves this benchmark |
| noise_18 | 196/196 | 49/49 | -0.607% [-1.012%, -0.292%] | -0.116% [-0.241%, -0.020%] | aggregate BD-rate CI below zero: statistically improves this benchmark |

Mean BD-rate classification follows the 95% bootstrap CI: fully below zero is an improvement on this benchmark, crossing zero is inconclusive, and fully above zero is a regression. This is a screening result, not a shipping verdict.

## Paired per-rate results

MOS values and actual payload rates are averaged across the same clips. Confidence intervals use paired per-clip MOS deltas; the four bitrate rungs are analyzed separately.

| Variant | Target | Clips | Baseline MOS | Candidate MOS | MOS Δ (95% paired bootstrap CI) | Actual payload Δ |
|---|---:|---:|---:|---:|---:|---:|
| candidate_default | 32 kbps | 49 | 3.84259 | 3.84259 | +0.00000 [+0.00000, +0.00000] | +0.000 kbps (+0.000%) |
| candidate_default | 48 kbps | 49 | 4.20630 | 4.20630 | +0.00000 [+0.00000, +0.00000] | +0.000 kbps (+0.000%) |
| candidate_default | 64 kbps | 49 | 4.44092 | 4.44092 | +0.00000 [+0.00000, +0.00000] | +0.000 kbps (+0.000%) |
| candidate_default | 96 kbps | 49 | 4.68014 | 4.68014 | +0.00000 [+0.00000, +0.00000] | +0.000 kbps (+0.000%) |
| defaults_explicit | 32 kbps | 49 | 3.84259 | 3.83849 | -0.00410 [-0.01035, +0.00068] | -0.011 kbps (-0.035%) |
| defaults_explicit | 48 kbps | 49 | 4.20630 | 4.20804 | +0.00174 [-0.00049, +0.00421] | -0.009 kbps (-0.018%) |
| defaults_explicit | 64 kbps | 49 | 4.44092 | 4.44119 | +0.00027 [-0.00253, +0.00260] | -0.011 kbps (-0.018%) |
| defaults_explicit | 96 kbps | 49 | 4.68014 | 4.68014 | +0.00001 [-0.00071, +0.00071] | -0.012 kbps (-0.013%) |
| invf_0 | 32 kbps | 49 | 3.84259 | 3.84393 | +0.00134 [+0.00039, +0.00234] | +0.000 kbps (+0.000%) |
| invf_0 | 48 kbps | 49 | 4.20630 | 4.20594 | -0.00036 [-0.00404, +0.00254] | +0.000 kbps (+0.000%) |
| invf_0 | 64 kbps | 49 | 4.44092 | 4.44061 | -0.00031 [-0.00394, +0.00273] | +0.000 kbps (+0.000%) |
| invf_0 | 96 kbps | 49 | 4.68014 | 4.67761 | -0.00253 [-0.00710, +0.00127] | +0.000 kbps (+0.000%) |
| invf_1 | 32 kbps | 49 | 3.84259 | 3.84324 | +0.00065 [+0.00025, +0.00112] | +0.000 kbps (+0.000%) |
| invf_1 | 48 kbps | 49 | 4.20630 | 4.20613 | -0.00017 [-0.00247, +0.00130] | +0.000 kbps (+0.000%) |
| invf_1 | 64 kbps | 49 | 4.44092 | 4.44129 | +0.00037 [-0.00199, +0.00209] | +0.000 kbps (+0.000%) |
| invf_1 | 96 kbps | 49 | 4.68014 | 4.67999 | -0.00015 [-0.00254, +0.00154] | +0.000 kbps (+0.000%) |
| limiter_gain_3 | 32 kbps | 49 | 3.84259 | 3.84377 | +0.00118 [-0.00653, +0.00852] | -0.011 kbps (-0.035%) |
| limiter_gain_3 | 48 kbps | 49 | 4.20630 | 4.21198 | +0.00569 [+0.00164, +0.01096] | -0.009 kbps (-0.018%) |
| limiter_gain_3 | 64 kbps | 49 | 4.44092 | 4.44525 | +0.00433 [+0.00020, +0.00856] | -0.011 kbps (-0.018%) |
| limiter_gain_3 | 96 kbps | 49 | 4.68014 | 4.68587 | +0.00573 [+0.00205, +0.01045] | -0.012 kbps (-0.013%) |
| noise_18 | 32 kbps | 49 | 3.84259 | 3.84775 | +0.00517 [+0.00301, +0.00782] | +0.000 kbps (+0.000%) |
| noise_18 | 48 kbps | 49 | 4.20630 | 4.20965 | +0.00335 [+0.00171, +0.00538] | +0.000 kbps (+0.000%) |
| noise_18 | 64 kbps | 49 | 4.44092 | 4.44459 | +0.00366 [+0.00190, +0.00574] | +0.000 kbps (+0.000%) |
| noise_18 | 96 kbps | 49 | 4.68014 | 4.68425 | +0.00412 [+0.00230, +0.00635] | +0.000 kbps (+0.000%) |

## Best and worst clips

For MOS, the table shows the largest and smallest per-clip paired MOS delta at each target rung. BD-rate uses the complete ladder per clip.

| Variant | Best BD clip | Best BD-rate | Worst BD clip | Worst BD-rate | Best MOS clip | MOS Δ | Worst MOS clip | MOS Δ |
|---|---|---:|---|---:|---|---:|---|---:|
| candidate_default | 12-German-male-speech.441.16b48k.wav | +0.000% | 12-German-male-speech.441.16b48k.wav | +0.000% | velvet.16b48k.wav @ 96k | +0.00000 | 12-German-male-speech.441.16b48k.wav @ 32k | +0.00000 |
| defaults_explicit | Shinsho_pool_3min45_4min4.16b48k.wav | -1.709% | lob.wav | +2.556% | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav @ 64k | +0.02824 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav @ 32k | -0.12041 |
| invf_0 | girl.16b48k.wav | -3.951% | glk.wav | +13.990% | cst.wav @ 64k | +0.02269 | Severance__1.31-1.51_.16b48k.wav @ 96k | -0.06206 |
| invf_1 | girl.16b48k.wav | -2.307% | glk.wav | +12.280% | girl.16b48k.wav @ 96k | +0.01114 | glk.wav @ 64k | -0.03989 |
| limiter_gain_3 | glk.wav | -10.888% | lob.wav | +2.625% | Severance__1.31-1.51_.16b48k.wav @ 48k | +0.10329 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav @ 32k | -0.11793 |
| noise_18 | girl.16b48k.wav | -5.702% | ExitMusic.16b48k.wav | +0.167% | Last_Of_The_Mohicanz__Sample_.16b48k.wav @ 32k | +0.04064 | 35_SQAM_glockenspiel_cut.16b48k.wav @ 48k | -0.00494 |

## Provenance and scope

- Results contain 49 clips and 4 target rates; gate is expected to be n=4, full corpus n=49 when the selected corpus has those counts.
- Probe source diff SHA-256: `0478d4aa6c5e9175088188fc19e55a7023ce55b54bc1d1713257a32d9938ac2e`.
- Size and speed were not tested; both remain pending.
- No result here establishes shipping readiness. Review per-clip outcomes, quality/rate curves, and follow-up size/speed gates.
