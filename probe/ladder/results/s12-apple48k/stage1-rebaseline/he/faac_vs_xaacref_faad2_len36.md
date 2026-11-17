# faac vs xaacref  (decoder faad2, cbr/he, rungs [32, 40, 48, 64], metric mos)

BD-rate mean **-9.827%** (95% clip-bootstrap CI [-13.067, -6.617], 10000 resamples, seed 0), median -8.698%; valid curves 36/36, no valid curve: 0
PCHIP sign: 6 clips worse / 30 better. Linear (log-rate linear in MOS): **-9.550%** [-12.532, -6.580], n 36, 5 worse / 31 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 32 | 36 | 3.8471 | 3.7691 | +0.0780 | 31.88 | 32.50 | 3.243 | 2.711 | 22/0/14 |
| 40 | 36 | 4.1585 | 4.0697 | +0.0889 | 39.90 | 40.46 | 3.511 | 3.542 | 27/0/9 |
| 48 | 36 | 4.3395 | 4.1897 | +0.1498 | 47.93 | 48.43 | 3.843 | 3.840 | 31/0/5 |
| 64 | 36 | 4.5557 | 4.5012 | +0.0546 | 63.97 | 64.47 | 4.108 | 4.177 | 30/0/6 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.3792 | 32 | Severance__1.31-1.51_.16b48k.wav |
| -0.2733 | 32 | 24td_.wav |
| -0.2682 | 32 | 01 castanets.wav |
| -0.2606 | 32 | 29ru_.wav |
| -0.2113 | 64 | 03 eig.wav |
| -0.1990 | 32 | Changes.16b48k.wav |
| -0.1872 | 32 | 35_SQAM_glockenspiel_cut.16b48k.wav |
| -0.1733 | 32 | With Love (Outro).16b48k.wav |
| -0.1350 | 40 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.1331 | 48 | 01 castanets.wav |
| -0.1327 | 40 | 01 castanets.wav |
| -0.1267 | 32 | 22ex_.wav |
| -0.1266 | 64 | 01 castanets.wav |
| -0.1258 | 48 | 07 applaud.wav |
| -0.1253 | 40 | 07 applaud.wav |
