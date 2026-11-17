# faac vs xaacref  (decoder ffmpeg, cbr/he, rungs [32, 40, 48, 64], metric mos)

BD-rate mean **-9.815%** (95% clip-bootstrap CI [-13.090, -6.602], 10000 resamples, seed 0), median -9.152%; valid curves 36/36, no valid curve: 0
PCHIP sign: 6 clips worse / 30 better. Linear (log-rate linear in MOS): **-9.530%** [-12.544, -6.541], n 36, 5 worse / 31 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 32 | 36 | 3.8520 | 3.7678 | +0.0842 | 31.88 | 32.50 | 3.250 | 2.710 | 19/0/17 |
| 40 | 36 | 4.1575 | 4.0697 | +0.0878 | 39.90 | 40.46 | 3.508 | 3.541 | 27/0/9 |
| 48 | 36 | 4.3388 | 4.1895 | +0.1493 | 47.93 | 48.43 | 3.843 | 3.840 | 31/0/5 |
| 64 | 36 | 4.5554 | 4.5011 | +0.0543 | 63.97 | 64.47 | 4.110 | 4.177 | 30/0/6 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.3720 | 32 | Severance__1.31-1.51_.16b48k.wav |
| -0.2788 | 32 | 01 castanets.wav |
| -0.2743 | 32 | 24td_.wav |
| -0.2463 | 32 | 29ru_.wav |
| -0.2029 | 64 | 03 eig.wav |
| -0.1915 | 32 | Changes.16b48k.wav |
| -0.1899 | 32 | 35_SQAM_glockenspiel_cut.16b48k.wav |
| -0.1722 | 32 | With Love (Outro).16b48k.wav |
| -0.1463 | 40 | 01 castanets.wav |
| -0.1370 | 40 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.1337 | 48 | 01 castanets.wav |
| -0.1318 | 40 | 07 applaud.wav |
| -0.1261 | 32 | 22ex_.wav |
| -0.1258 | 64 | 01 castanets.wav |
| -0.1219 | 48 | 07 applaud.wav |
