# HE-AAC v1 SBR probe results

Input: `/private/tmp/faac-he-v1-probes/probe/results-gate/feature-results.json`; runner summary: `/private/tmp/faac-he-v1-probes/probe/results-gate/feature-summary.json`.
Corpus: **4 clips**; bitrate rungs: 32k, 48k, 64k, 96k.

BD-rate is computed by the benchmark's PCHIP helper from actual AAC packet payload rates and Zimtohrli MOS. Per-clip BD-rate values are the independent units for bootstrap intervals; bitrate rungs are not treated as independent samples.

## Per-variant assessment

| Variant | Packet identities changed | Valid BD clips | Mean BD-rate (95% bootstrap CI) | Median BD-rate (95% bootstrap CI) | Assessment |
|---|---:|---:|---:|---:|---|
| candidate_default | 0/16 | 4/4 | +0.000% [+0.000%, +0.000%] | +0.000% [+0.000%, +0.000%] | aggregate BD-rate CI crosses zero: inconclusive |
| defaults_explicit | 16/16 | 4/4 | +0.386% [-0.020%, +0.791%] | +0.343% [-0.052%, +0.909%] | aggregate BD-rate CI crosses zero: inconclusive |
| harmonic_60 | 13/16 | 4/4 | +0.005% [-0.163%, +0.173%] | -0.014% [-0.179%, +0.227%] | aggregate BD-rate CI crosses zero: inconclusive |
| harmonic_75 | 9/16 | 4/4 | +0.462% [-0.096%, +1.111%] | +0.279% [-0.192%, +1.481%] | aggregate BD-rate CI crosses zero: inconclusive |
| harmonic_90 | 4/16 | 4/4 | +0.128% [+0.000%, +0.385%] | +0.000% [+0.000%, +0.513%] | aggregate BD-rate CI crosses zero: inconclusive |
| interpol_0 | 16/16 | 4/4 | +0.645% [+0.173%, +1.117%] | +0.593% [+0.142%, +1.250%] | aggregate BD-rate CI above zero: regresses on this benchmark |
| invf_0 | 16/16 | 4/4 | -0.157% [-0.618%, +0.298%] | -0.153% [-0.808%, +0.484%] | aggregate BD-rate CI crosses zero: inconclusive |
| invf_1 | 16/16 | 4/4 | -0.167% [-0.362%, -0.041%] | -0.099% [-0.458%, -0.013%] | aggregate BD-rate CI below zero: statistically improves this benchmark |
| invf_2 | 16/16 | 4/4 | -0.037% [-0.083%, -0.007%] | -0.022% [-0.105%, +0.001%] | aggregate BD-rate CI below zero: statistically improves this benchmark |
| limiter_bands_0 | 16/16 | 4/4 | +0.823% [+0.342%, +1.116%] | +1.042% [+0.086%, +1.124%] | aggregate BD-rate CI above zero: regresses on this benchmark |
| limiter_bands_1 | 16/16 | 4/4 | +0.772% [+0.228%, +1.312%] | +0.776% [-0.018%, +1.554%] | aggregate BD-rate CI above zero: regresses on this benchmark |
| limiter_bands_3 | 16/16 | 4/4 | +0.386% [-0.020%, +0.791%] | +0.343% [-0.052%, +0.909%] | aggregate BD-rate CI crosses zero: inconclusive |
| limiter_gain_0 | 16/16 | 4/4 | +1.649% [+0.385%, +3.228%] | +1.138% [+0.205%, +4.116%] | aggregate BD-rate CI above zero: regresses on this benchmark |
| limiter_gain_1 | 16/16 | 4/4 | +0.809% [+0.001%, +1.617%] | +0.634% [-0.071%, +2.039%] | aggregate BD-rate CI above zero: regresses on this benchmark |
| limiter_gain_3 | 16/16 | 4/4 | -0.033% [-0.494%, +0.515%] | -0.171% [-0.603%, +0.815%] | aggregate BD-rate CI crosses zero: inconclusive |
| noise_15 | 16/16 | 4/4 | -0.643% [-1.119%, -0.194%] | -0.615% [-1.337%, -0.002%] | aggregate BD-rate CI below zero: statistically improves this benchmark |
| noise_18 | 16/16 | 4/4 | -0.702% [-1.236%, -0.213%] | -0.658% [-1.490%, -0.003%] | aggregate BD-rate CI below zero: statistically improves this benchmark |
| noise_6 | 16/16 | 4/4 | +8.708% [+2.319%, +14.495%] | +9.310% [-0.644%, +16.856%] | aggregate BD-rate CI above zero: regresses on this benchmark |
| noise_9 | 16/16 | 4/4 | +2.828% [+0.852%, +4.805%] | +2.908% [-0.014%, +5.511%] | aggregate BD-rate CI above zero: regresses on this benchmark |
| smooth_0 | 16/16 | 4/4 | +0.386% [-0.020%, +0.791%] | +0.343% [-0.052%, +0.909%] | aggregate BD-rate CI crosses zero: inconclusive |

Mean BD-rate classification follows the 95% bootstrap CI: fully below zero is an improvement on this benchmark, crossing zero is inconclusive, and fully above zero is a regression. This is a screening result, not a shipping verdict.

## Paired per-rate results

MOS values and actual payload rates are averaged across the same clips. Confidence intervals use paired per-clip MOS deltas; the four bitrate rungs are analyzed separately.

| Variant | Target | Clips | Baseline MOS | Candidate MOS | MOS Δ (95% paired bootstrap CI) | Actual payload Δ |
|---|---:|---:|---:|---:|---:|---:|
| candidate_default | 32 kbps | 4 | 3.82641 | 3.82641 | +0.00000 [+0.00000, +0.00000] | +0.000 kbps (+0.000%) |
| candidate_default | 48 kbps | 4 | 4.17969 | 4.17969 | +0.00000 [+0.00000, +0.00000] | +0.000 kbps (+0.000%) |
| candidate_default | 64 kbps | 4 | 4.43086 | 4.43086 | +0.00000 [+0.00000, +0.00000] | +0.000 kbps (+0.000%) |
| candidate_default | 96 kbps | 4 | 4.67168 | 4.67168 | +0.00000 [+0.00000, +0.00000] | +0.000 kbps (+0.000%) |
| defaults_explicit | 32 kbps | 4 | 3.82641 | 3.81291 | -0.01349 [-0.02637, -0.00093] | -0.016 kbps (-0.049%) |
| defaults_explicit | 48 kbps | 4 | 4.17969 | 4.17658 | -0.00311 [-0.00920, +0.00298] | -0.015 kbps (-0.032%) |
| defaults_explicit | 64 kbps | 4 | 4.43086 | 4.43153 | +0.00067 [-0.00197, +0.00331] | -0.017 kbps (-0.026%) |
| defaults_explicit | 96 kbps | 4 | 4.67168 | 4.67004 | -0.00164 [-0.00344, +0.00016] | -0.011 kbps (-0.011%) |
| harmonic_60 | 32 kbps | 4 | 3.82641 | 3.82361 | -0.00280 [-0.00813, +0.00125] | +0.003 kbps (+0.011%) |
| harmonic_60 | 48 kbps | 4 | 4.17969 | 4.18002 | +0.00034 [-0.00042, +0.00109] | -0.000 kbps (-0.001%) |
| harmonic_60 | 64 kbps | 4 | 4.43086 | 4.43250 | +0.00164 [+0.00007, +0.00321] | +0.000 kbps (+0.001%) |
| harmonic_60 | 96 kbps | 4 | 4.67168 | 4.67076 | -0.00092 [-0.00292, +0.00018] | +0.001 kbps (+0.001%) |
| harmonic_75 | 32 kbps | 4 | 3.82641 | 3.81539 | -0.01102 [-0.02522, +0.00133] | +0.002 kbps (+0.005%) |
| harmonic_75 | 48 kbps | 4 | 4.17969 | 4.17470 | -0.00499 [-0.01523, +0.00026] | +0.001 kbps (+0.002%) |
| harmonic_75 | 64 kbps | 4 | 4.43086 | 4.43191 | +0.00105 [+0.00000, +0.00284] | +0.001 kbps (+0.001%) |
| harmonic_75 | 96 kbps | 4 | 4.67168 | 4.67079 | -0.00089 [-0.00274, +0.00008] | +0.000 kbps (+0.000%) |
| harmonic_90 | 32 kbps | 4 | 3.82641 | 3.82209 | -0.00432 [-0.01296, +0.00000] | +0.000 kbps (+0.001%) |
| harmonic_90 | 48 kbps | 4 | 4.17969 | 4.17912 | -0.00057 [-0.00170, +0.00000] | +0.000 kbps (+0.001%) |
| harmonic_90 | 64 kbps | 4 | 4.43086 | 4.43072 | -0.00014 [-0.00041, +0.00000] | +0.000 kbps (+0.000%) |
| harmonic_90 | 96 kbps | 4 | 4.67168 | 4.67106 | -0.00062 [-0.00186, +0.00000] | +0.000 kbps (+0.000%) |
| interpol_0 | 32 kbps | 4 | 3.82641 | 3.81240 | -0.01401 [-0.02623, -0.00034] | -0.016 kbps (-0.049%) |
| interpol_0 | 48 kbps | 4 | 4.17969 | 4.17520 | -0.00449 [-0.01134, +0.00236] | -0.015 kbps (-0.032%) |
| interpol_0 | 64 kbps | 4 | 4.43086 | 4.42819 | -0.00267 [-0.00720, +0.00186] | -0.017 kbps (-0.026%) |
| interpol_0 | 96 kbps | 4 | 4.67168 | 4.66807 | -0.00361 [-0.00539, -0.00183] | -0.011 kbps (-0.011%) |
| invf_0 | 32 kbps | 4 | 3.82641 | 3.82832 | +0.00191 [-0.00107, +0.00529] | +0.000 kbps (+0.000%) |
| invf_0 | 48 kbps | 4 | 4.17969 | 4.18184 | +0.00215 [-0.00163, +0.00600] | +0.000 kbps (+0.000%) |
| invf_0 | 64 kbps | 4 | 4.43086 | 4.43234 | +0.00147 [-0.00129, +0.00424] | +0.000 kbps (+0.000%) |
| invf_0 | 96 kbps | 4 | 4.67168 | 4.67080 | -0.00087 [-0.00548, +0.00407] | +0.000 kbps (+0.000%) |
| invf_1 | 32 kbps | 4 | 3.82641 | 3.82748 | +0.00107 [-0.00045, +0.00260] | +0.000 kbps (+0.000%) |
| invf_1 | 48 kbps | 4 | 4.17969 | 4.18103 | +0.00135 [-0.00021, +0.00293] | +0.000 kbps (+0.000%) |
| invf_1 | 64 kbps | 4 | 4.43086 | 4.43274 | +0.00188 [+0.00071, +0.00372] | +0.000 kbps (+0.000%) |
| invf_1 | 96 kbps | 4 | 4.67168 | 4.67222 | +0.00054 [-0.00015, +0.00158] | +0.000 kbps (+0.000%) |
| invf_2 | 32 kbps | 4 | 3.82641 | 3.82648 | +0.00007 [-0.00083, +0.00098] | +0.000 kbps (+0.000%) |
| invf_2 | 48 kbps | 4 | 4.17969 | 4.17990 | +0.00022 [-0.00020, +0.00063] | +0.000 kbps (+0.000%) |
| invf_2 | 64 kbps | 4 | 4.43086 | 4.43139 | +0.00053 [+0.00031, +0.00075] | +0.000 kbps (+0.000%) |
| invf_2 | 96 kbps | 4 | 4.67168 | 4.67191 | +0.00023 [-0.00016, +0.00062] | +0.000 kbps (+0.000%) |
| limiter_bands_0 | 32 kbps | 4 | 3.82641 | 3.81263 | -0.01378 [-0.02576, -0.00150] | -0.016 kbps (-0.049%) |
| limiter_bands_0 | 48 kbps | 4 | 4.17969 | 4.17350 | -0.00618 [-0.01180, -0.00007] | -0.015 kbps (-0.032%) |
| limiter_bands_0 | 64 kbps | 4 | 4.43086 | 4.42678 | -0.00408 [-0.00945, +0.00104] | -0.017 kbps (-0.026%) |
| limiter_bands_0 | 96 kbps | 4 | 4.67168 | 4.66556 | -0.00612 [-0.00992, -0.00330] | -0.011 kbps (-0.011%) |
| limiter_bands_1 | 32 kbps | 4 | 3.82641 | 3.81179 | -0.01462 [-0.02621, -0.00185] | -0.016 kbps (-0.049%) |
| limiter_bands_1 | 48 kbps | 4 | 4.17969 | 4.17401 | -0.00567 [-0.01130, +0.00053] | -0.015 kbps (-0.032%) |
| limiter_bands_1 | 64 kbps | 4 | 4.43086 | 4.42752 | -0.00334 [-0.01120, +0.00265] | -0.017 kbps (-0.026%) |
| limiter_bands_1 | 96 kbps | 4 | 4.67168 | 4.66591 | -0.00577 [-0.01240, -0.00166] | -0.011 kbps (-0.011%) |
| limiter_bands_3 | 32 kbps | 4 | 3.82641 | 3.81291 | -0.01349 [-0.02637, -0.00093] | -0.016 kbps (-0.049%) |
| limiter_bands_3 | 48 kbps | 4 | 4.17969 | 4.17658 | -0.00311 [-0.00920, +0.00298] | -0.015 kbps (-0.032%) |
| limiter_bands_3 | 64 kbps | 4 | 4.43086 | 4.43153 | +0.00067 [-0.00197, +0.00331] | -0.017 kbps (-0.026%) |
| limiter_bands_3 | 96 kbps | 4 | 4.67168 | 4.67004 | -0.00164 [-0.00344, +0.00016] | -0.011 kbps (-0.011%) |
| limiter_gain_0 | 32 kbps | 4 | 3.82641 | 3.80265 | -0.02375 [-0.03859, -0.00838] | -0.016 kbps (-0.049%) |
| limiter_gain_0 | 48 kbps | 4 | 4.17969 | 4.16811 | -0.01157 [-0.02434, +0.00001] | -0.015 kbps (-0.032%) |
| limiter_gain_0 | 64 kbps | 4 | 4.43086 | 4.42247 | -0.00839 [-0.01778, -0.00235] | -0.017 kbps (-0.026%) |
| limiter_gain_0 | 96 kbps | 4 | 4.67168 | 4.65992 | -0.01175 [-0.02148, -0.00377] | -0.011 kbps (-0.011%) |
| limiter_gain_1 | 32 kbps | 4 | 3.82641 | 3.80937 | -0.01703 [-0.03003, -0.00270] | -0.016 kbps (-0.049%) |
| limiter_gain_1 | 48 kbps | 4 | 4.17969 | 4.17372 | -0.00597 [-0.01503, +0.00309] | -0.015 kbps (-0.032%) |
| limiter_gain_1 | 64 kbps | 4 | 4.43086 | 4.42878 | -0.00208 [-0.00656, +0.00130] | -0.017 kbps (-0.026%) |
| limiter_gain_1 | 96 kbps | 4 | 4.67168 | 4.66645 | -0.00523 [-0.00945, -0.00101] | -0.011 kbps (-0.011%) |
| limiter_gain_3 | 32 kbps | 4 | 3.82641 | 3.81542 | -0.01099 [-0.02475, -0.00031] | -0.016 kbps (-0.049%) |
| limiter_gain_3 | 48 kbps | 4 | 4.17969 | 4.17935 | -0.00034 [-0.00979, +0.00580] | -0.015 kbps (-0.032%) |
| limiter_gain_3 | 64 kbps | 4 | 4.43086 | 4.43518 | +0.00432 [-0.00198, +0.00907] | -0.017 kbps (-0.026%) |
| limiter_gain_3 | 96 kbps | 4 | 4.67168 | 4.67310 | +0.00142 [-0.00280, +0.00404] | -0.011 kbps (-0.011%) |
| noise_15 | 32 kbps | 4 | 3.82641 | 3.83196 | +0.00555 [+0.00085, +0.01026] | +0.000 kbps (+0.000%) |
| noise_15 | 48 kbps | 4 | 4.17969 | 4.18333 | +0.00365 [+0.00120, +0.00625] | +0.000 kbps (+0.000%) |
| noise_15 | 64 kbps | 4 | 4.43086 | 4.43701 | +0.00614 [+0.00152, +0.01165] | +0.000 kbps (+0.000%) |
| noise_15 | 96 kbps | 4 | 4.67168 | 4.67835 | +0.00667 [+0.00206, +0.01098] | +0.000 kbps (+0.000%) |
| noise_18 | 32 kbps | 4 | 3.82641 | 3.83236 | +0.00596 [+0.00064, +0.01127] | +0.000 kbps (+0.000%) |
| noise_18 | 48 kbps | 4 | 4.17969 | 4.18386 | +0.00418 [+0.00124, +0.00766] | +0.000 kbps (+0.000%) |
| noise_18 | 64 kbps | 4 | 4.43086 | 4.43744 | +0.00658 [+0.00165, +0.01258] | +0.000 kbps (+0.000%) |
| noise_18 | 96 kbps | 4 | 4.67168 | 4.67909 | +0.00741 [+0.00218, +0.01240] | +0.000 kbps (+0.000%) |
| noise_6 | 32 kbps | 4 | 3.82641 | 3.72632 | -0.10009 [-0.18030, -0.02141] | +0.000 kbps (+0.000%) |
| noise_6 | 48 kbps | 4 | 4.17969 | 4.11443 | -0.06526 [-0.10760, -0.02099] | +0.000 kbps (+0.000%) |
| noise_6 | 64 kbps | 4 | 4.43086 | 4.36559 | -0.06527 [-0.10997, -0.01714] | +0.000 kbps (+0.000%) |
| noise_6 | 96 kbps | 4 | 4.67168 | 4.59661 | -0.07507 [-0.12405, -0.02052] | +0.000 kbps (+0.000%) |
| noise_9 | 32 kbps | 4 | 3.82641 | 3.79516 | -0.03125 [-0.05648, -0.00786] | +0.000 kbps (+0.000%) |
| noise_9 | 48 kbps | 4 | 4.17969 | 4.15868 | -0.02100 [-0.03677, -0.00524] | +0.000 kbps (+0.000%) |
| noise_9 | 64 kbps | 4 | 4.43086 | 4.40891 | -0.02195 [-0.03800, -0.00590] | +0.000 kbps (+0.000%) |
| noise_9 | 96 kbps | 4 | 4.67168 | 4.64962 | -0.02206 [-0.03532, -0.00795] | +0.000 kbps (+0.000%) |
| smooth_0 | 32 kbps | 4 | 3.82641 | 3.81291 | -0.01349 [-0.02637, -0.00093] | -0.016 kbps (-0.049%) |
| smooth_0 | 48 kbps | 4 | 4.17969 | 4.17658 | -0.00311 [-0.00920, +0.00298] | -0.015 kbps (-0.032%) |
| smooth_0 | 64 kbps | 4 | 4.43086 | 4.43153 | +0.00067 [-0.00197, +0.00331] | -0.017 kbps (-0.026%) |
| smooth_0 | 96 kbps | 4 | 4.67168 | 4.67004 | -0.00164 [-0.00344, +0.00016] | -0.011 kbps (-0.011%) |

## Best and worst clips

For MOS, the table shows the largest and smallest per-clip paired MOS delta at each target rung. BD-rate uses the complete ladder per clip.

| Variant | Best BD clip | Best BD-rate | Worst BD clip | Worst BD-rate | Best MOS clip | MOS Δ | Worst MOS clip | MOS Δ |
|---|---|---:|---|---:|---|---:|---|---:|
| candidate_default | sandman.16b48k.wav | +0.000% | sandman.16b48k.wav | +0.000% | velvet.16b48k.wav @ 96k | +0.00000 | 21-classic.441.16b48k.wav @ 32k | +0.00000 |
| defaults_explicit | velvet.16b48k.wav | -0.052% | fms.wav | +0.909% | fms.wav @ 64k | +0.00444 | fms.wav @ 32k | -0.03101 |
| harmonic_60 | sandman.16b48k.wav | -0.179% | velvet.16b48k.wav | +0.227% | sandman.16b48k.wav @ 64k | +0.00362 | velvet.16b48k.wav @ 32k | -0.01120 |
| harmonic_75 | sandman.16b48k.wav | -0.192% | 21-classic.441.16b48k.wav | +1.481% | sandman.16b48k.wav @ 64k | +0.00379 | fms.wav @ 32k | -0.03363 |
| harmonic_90 | sandman.16b48k.wav | +0.000% | 21-classic.441.16b48k.wav | +0.513% | velvet.16b48k.wav @ 96k | +0.00000 | 21-classic.441.16b48k.wav @ 32k | -0.01728 |
| interpol_0 | velvet.16b48k.wav | +0.142% | 21-classic.441.16b48k.wav | +1.250% | sandman.16b48k.wav @ 32k | +0.00574 | fms.wav @ 32k | -0.03085 |
| invf_0 | velvet.16b48k.wav | -0.808% | 21-classic.441.16b48k.wav | +0.484% | velvet.16b48k.wav @ 48k | +0.00738 | 21-classic.441.16b48k.wav @ 96k | -0.00730 |
| invf_1 | velvet.16b48k.wav | -0.458% | 21-classic.441.16b48k.wav | -0.013% | velvet.16b48k.wav @ 64k | +0.00466 | 21-classic.441.16b48k.wav @ 48k | -0.00074 |
| invf_2 | velvet.16b48k.wav | -0.105% | fms.wav | +0.001% | velvet.16b48k.wav @ 32k | +0.00146 | fms.wav @ 32k | -0.00089 |
| limiter_bands_0 | sandman.16b48k.wav | +0.086% | velvet.16b48k.wav | +1.124% | sandman.16b48k.wav @ 32k | +0.00379 | fms.wav @ 32k | -0.03075 |
| limiter_bands_1 | sandman.16b48k.wav | -0.018% | velvet.16b48k.wav | +1.554% | fms.wav @ 64k | +0.00450 | fms.wav @ 32k | -0.03033 |
| limiter_bands_3 | velvet.16b48k.wav | -0.052% | fms.wav | +0.909% | fms.wav @ 64k | +0.00444 | fms.wav @ 32k | -0.03101 |
| limiter_gain_0 | sandman.16b48k.wav | +0.205% | 21-classic.441.16b48k.wav | +4.116% | sandman.16b48k.wav @ 32k | +0.00149 | 21-classic.441.16b48k.wav @ 32k | -0.03916 |
| limiter_gain_1 | sandman.16b48k.wav | -0.071% | 21-classic.441.16b48k.wav | +2.039% | sandman.16b48k.wav @ 32k | +0.00455 | fms.wav @ 32k | -0.03560 |
| limiter_gain_3 | 21-classic.441.16b48k.wav | -0.603% | fms.wav | +0.815% | 21-classic.441.16b48k.wav @ 64k | +0.01057 | fms.wav @ 32k | -0.03100 |
| noise_15 | velvet.16b48k.wav | -1.337% | 21-classic.441.16b48k.wav | -0.002% | velvet.16b48k.wav @ 64k | +0.01407 | 21-classic.441.16b48k.wav @ 96k | -0.00033 |
| noise_18 | velvet.16b48k.wav | -1.490% | 21-classic.441.16b48k.wav | -0.003% | velvet.16b48k.wav @ 64k | +0.01525 | 21-classic.441.16b48k.wav @ 96k | -0.00038 |
| noise_6 | 21-classic.441.16b48k.wav | -0.644% | velvet.16b48k.wav | +16.856% | 21-classic.441.16b48k.wav @ 32k | +0.00986 | velvet.16b48k.wav @ 32k | -0.21312 |
| noise_9 | 21-classic.441.16b48k.wav | -0.014% | velvet.16b48k.wav | +5.511% | 21-classic.441.16b48k.wav @ 32k | +0.00204 | velvet.16b48k.wav @ 32k | -0.06822 |
| smooth_0 | velvet.16b48k.wav | -0.052% | fms.wav | +0.909% | fms.wav @ 64k | +0.00444 | fms.wav @ 32k | -0.03101 |

## Provenance and scope

- Results contain 4 clips and 4 target rates; gate is expected to be n=4, full corpus n=49 when the selected corpus has those counts.
- Probe source diff SHA-256: `0478d4aa6c5e9175088188fc19e55a7023ce55b54bc1d1713257a32d9938ac2e`.
- Size and speed were not tested; both remain pending.
- No result here establishes shipping readiness. Review per-clip outcomes, quality/rate curves, and follow-up size/speed gates.
