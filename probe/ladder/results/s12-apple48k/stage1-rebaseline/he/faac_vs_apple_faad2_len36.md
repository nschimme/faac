# faac vs apple  (decoder faad2, cbr/he, rungs [32, 40, 48, 64], metric mos)

BD-rate mean **-10.226%** (95% clip-bootstrap CI [-14.788, -5.528], 10000 resamples, seed 0), median -8.383%; valid curves 36/36, no valid curve: 0
PCHIP sign: 6 clips worse / 30 better. Linear (log-rate linear in MOS): **-10.080%** [-14.267, -5.859], n 36, 6 worse / 30 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 32 | 36 | 3.8471 | 3.7572 | +0.0899 | 31.88 | 32.46 | 3.243 | 2.681 | 20/0/16 |
| 40 | 36 | 4.1585 | 4.0039 | +0.1547 | 39.90 | 40.42 | 3.511 | 3.005 | 27/0/9 |
| 48 | 36 | 4.3395 | 4.2089 | +0.1305 | 47.93 | 48.49 | 3.843 | 3.575 | 28/0/8 |
| 64 | 36 | 4.5557 | 4.4675 | +0.0882 | 63.97 | 64.61 | 4.108 | 4.002 | 28/0/8 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.5446 | 32 | Severance__1.31-1.51_.16b48k.wav |
| -0.3430 | 32 | 07 applaud.wav |
| -0.2869 | 32 | Asleep__4.11-4.31_.16b48k.wav |
| -0.2504 | 32 | With Love (Outro).16b48k.wav |
| -0.2365 | 40 | 06 trumpet.wav |
| -0.2185 | 32 | 24td_.wav |
| -0.1996 | 32 | 29ru_.wav |
| -0.1934 | 32 | 35_SQAM_glockenspiel_cut.16b48k.wav |
| -0.1820 | 64 | 03 eig.wav |
| -0.1675 | 48 | 07 applaud.wav |
| -0.1571 | 40 | 21-classic.441.16b48k.wav |
| -0.1427 | 48 | 06 trumpet.wav |
| -0.1371 | 32 | 15ma_.wav |
| -0.1309 | 32 | 06 trumpet.wav |
| -0.1154 | 40 | 07 applaud.wav |
