# HE-v1 NPER by frequency band

Events fixed from the benchmark pipeline (2 kHz HP, 4th order); pre-echo (-6..-1 ms vs ref pre, floor ref_post*1e-8) recomputed per band on
zero-phase 8th-order Butterworth copies of ref and decoded (same filter). Excess = mean of max(0, dB) pooled over all events (all clips, channels).
Dominance = share of events whose band excess > full-band excess - 3 dB (bands overlap in share; not exclusive).

| enc | rung | events | full (bench) | full (recomp) | 2-6k excess / dom | 6-12k excess / dom | 12-24k excess / dom |
|---|---|---|---|---|---|---|---|
| faac | 32k | 965 | 21.14 | 21.14 | 21.20 / 83% | 21.35 / 59% | 13.75 / 40% |
| faac | 40k | 965 | 18.36 | 18.36 | 18.27 / 83% | 19.79 / 70% | 12.17 / 41% |
| faac | 48k | 965 | 16.48 | 16.48 | 16.29 / 83% | 18.96 / 78% | 11.66 / 44% |
| fdk | 32k | 965 | 15.44 | 15.44 | 16.03 / 89% | 14.87 / 60% | 6.14 / 32% |
| fdk | 40k | 965 | 14.81 | 14.81 | 15.05 / 86% | 15.10 / 66% | 7.50 / 39% |
| fdk | 48k | 965 | 14.41 | 14.41 | 14.73 / 87% | 14.40 / 62% | 7.20 / 41% |
| apple | 32k | 965 | 17.00 | 17.00 | 17.27 / 85% | 17.36 / 64% | 6.97 / 28% |
| apple | 40k | 965 | 15.45 | 15.45 | 15.77 / 84% | 15.51 / 64% | 9.26 / 40% |
| apple | 48k | 965 | 14.91 | 14.91 | 15.52 / 87% | 14.12 / 61% | 9.17 / 41% |

Clip-mean full-band NPER: faac/32=10.11, faac/40=8.92, faac/48=8.04, fdk/32=8.71, fdk/40=7.80, fdk/48=7.28, apple/32=10.05, apple/40=8.33, apple/48=8.00

## SBR start frequency (crossover) from decoder dumps

| enc | rung | out rate | start_freq idx (mode, hist) | k0 | crossover Hz |
|---|---|---|---|---|---|
| faac | 32k | 48000 | 15 ({15: 8458}) | 31 | 11625 |
| faac | 40k | 48000 | 12 ({12: 8458}) | 22 | 8250 |
| faac | 48k | 48000 | 12 ({12: 8458}) | 22 | 8250 |
| fdk | 32k | 48000 | 7 ({7: 8466}) | 15 | 5625 |
| fdk | 40k | 48000 | 10 ({10: 8466}) | 18 | 6750 |
| fdk | 48k | 48000 | 12 ({12: 8466}) | 22 | 8250 |
| apple | 32k | 32000 | 11 ({11: 5666}) | 23 | 5750 |
| apple | 40k | 48000 | 11 ({11: 8470}) | 20 | 7500 |
| apple | 48k | 48000 | 13 ({13: 8470}) | 24 | 9000 |

