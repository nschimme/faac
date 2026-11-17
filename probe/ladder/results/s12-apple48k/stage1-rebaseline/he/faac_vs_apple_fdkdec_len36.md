# faac vs apple  (decoder fdkdec, cbr/he, rungs [32, 40, 48, 64], metric mos)

BD-rate mean **-9.217%** (95% clip-bootstrap CI [-13.917, -4.513], 10000 resamples, seed 0), median -7.009%; valid curves 36/36, no valid curve: 0
PCHIP sign: 9 clips worse / 27 better. Linear (log-rate linear in MOS): **-8.970%** [-13.295, -4.616], n 36, 8 worse / 28 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 32 | 36 | 3.8328 | 3.7214 | +0.1114 | 31.88 | 32.46 | 3.259 | 2.611 | 20/0/16 |
| 40 | 36 | 4.1264 | 3.9861 | +0.1403 | 39.90 | 40.42 | 3.493 | 3.031 | 25/0/11 |
| 48 | 36 | 4.3040 | 4.1856 | +0.1184 | 47.93 | 48.49 | 3.830 | 3.574 | 26/0/10 |
| 64 | 36 | 4.5234 | 4.4570 | +0.0665 | 63.97 | 64.61 | 4.085 | 3.995 | 23/0/13 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.4798 | 32 | Severance__1.31-1.51_.16b48k.wav |
| -0.3363 | 32 | 07 applaud.wav |
| -0.2772 | 32 | Asleep__4.11-4.31_.16b48k.wav |
| -0.2480 | 32 | 29ru_.wav |
| -0.2451 | 32 | 35_SQAM_glockenspiel_cut.16b48k.wav |
| -0.2313 | 64 | 03 eig.wav |
| -0.2268 | 40 | 06 trumpet.wav |
| -0.2163 | 32 | With Love (Outro).16b48k.wav |
| -0.1898 | 64 | Severance__1.31-1.51_.16b48k.wav |
| -0.1651 | 40 | 21-classic.441.16b48k.wav |
| -0.1545 | 32 | Hotel California.16b48k.wav |
| -0.1513 | 48 | 07 applaud.wav |
| -0.1462 | 32 | 24td_.wav |
| -0.1340 | 48 | Asleep__4.11-4.31_.16b48k.wav |
| -0.1251 | 48 | 06 trumpet.wav |
