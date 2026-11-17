# faac vs xaacref  (decoder fdkdec, cbr/he, rungs [32, 40, 48, 64], metric mos)

BD-rate mean **-9.372%** (95% clip-bootstrap CI [-12.587, -6.142], 10000 resamples, seed 0), median -9.233%; valid curves 36/36, no valid curve: 0
PCHIP sign: 5 clips worse / 31 better. Linear (log-rate linear in MOS): **-8.696%** [-11.771, -5.589], n 36, 6 worse / 30 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 32 | 36 | 3.8328 | 3.7278 | +0.1050 | 31.88 | 32.50 | 3.259 | 2.693 | 21/0/15 |
| 40 | 36 | 4.1264 | 4.0377 | +0.0887 | 39.90 | 40.46 | 3.493 | 3.484 | 26/0/10 |
| 48 | 36 | 4.3040 | 4.1709 | +0.1331 | 47.93 | 48.43 | 3.830 | 3.817 | 32/0/4 |
| 64 | 36 | 4.5234 | 4.4896 | +0.0338 | 63.97 | 64.47 | 4.085 | 4.171 | 27/0/9 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.2830 | 32 | 01 castanets.wav |
| -0.2797 | 32 | Severance__1.31-1.51_.16b48k.wav |
| -0.2558 | 64 | 03 eig.wav |
| -0.2256 | 32 | 35_SQAM_glockenspiel_cut.16b48k.wav |
| -0.2208 | 32 | Changes.16b48k.wav |
| -0.1856 | 48 | 01 castanets.wav |
| -0.1745 | 32 | 29ru_.wav |
| -0.1720 | 40 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.1677 | 32 | 24td_.wav |
| -0.1673 | 64 | 01 castanets.wav |
| -0.1599 | 64 | Severance__1.31-1.51_.16b48k.wav |
| -0.1432 | 40 | 01 castanets.wav |
| -0.1283 | 32 | With Love (Outro).16b48k.wav |
| -0.1273 | 32 | Asleep__4.11-4.31_.16b48k.wav |
| -0.1232 | 48 | 07 applaud.wav |
