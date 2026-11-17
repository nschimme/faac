# faac vs fdkref  (decoder ffmpeg, cbr/he, rungs [32, 40, 48, 64], metric mos)

BD-rate mean **-6.809%** (95% clip-bootstrap CI [-10.036, -3.643], 10000 resamples, seed 0), median -6.347%; valid curves 36/36, no valid curve: 0
PCHIP sign: 7 clips worse / 29 better. Linear (log-rate linear in MOS): **-6.742%** [-9.827, -3.667], n 36, 7 worse / 29 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 32 | 36 | 3.8520 | 3.7839 | +0.0681 | 31.88 | 32.60 | 3.250 | 2.668 | 19/0/17 |
| 40 | 36 | 4.1575 | 4.0933 | +0.0642 | 39.90 | 40.57 | 3.508 | 3.208 | 23/0/13 |
| 48 | 36 | 4.3388 | 4.2519 | +0.0870 | 47.93 | 48.56 | 3.843 | 3.863 | 28/0/8 |
| 64 | 36 | 4.5554 | 4.5176 | +0.0377 | 63.97 | 64.61 | 4.110 | 4.038 | 28/0/8 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.4086 | 32 | Severance__1.31-1.51_.16b48k.wav |
| -0.2968 | 32 | 24td_.wav |
| -0.2576 | 32 | 29ru_.wav |
| -0.2140 | 32 | Asleep__4.11-4.31_.16b48k.wav |
| -0.2099 | 40 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.2075 | 48 | 01 castanets.wav |
| -0.2065 | 32 | Changes.16b48k.wav |
| -0.1983 | 40 | 01 castanets.wav |
| -0.1882 | 32 | 35_SQAM_glockenspiel_cut.16b48k.wav |
| -0.1785 | 64 | 03 eig.wav |
| -0.1777 | 40 | Asleep__4.11-4.31_.16b48k.wav |
| -0.1744 | 48 | 07 applaud.wav |
| -0.1630 | 32 | 22ex_.wav |
| -0.1601 | 64 | 01 castanets.wav |
| -0.1506 | 32 | With Love (Outro).16b48k.wav |
