# faac vs apple  (decoder ffmpeg, cbr/he, rungs [32, 40, 48, 64], metric mos)

BD-rate mean **-10.167%** (95% clip-bootstrap CI [-14.721, -5.440], 10000 resamples, seed 0), median -8.298%; valid curves 36/36, no valid curve: 0
PCHIP sign: 7 clips worse / 29 better. Linear (log-rate linear in MOS): **-10.040%** [-14.255, -5.809], n 36, 6 worse / 30 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 32 | 36 | 3.8520 | 3.7578 | +0.0942 | 31.88 | 32.46 | 3.250 | 2.688 | 20/0/16 |
| 40 | 36 | 4.1575 | 4.0044 | +0.1531 | 39.90 | 40.42 | 3.508 | 3.006 | 26/0/10 |
| 48 | 36 | 4.3388 | 4.2093 | +0.1296 | 47.93 | 48.49 | 3.843 | 3.578 | 28/0/8 |
| 64 | 36 | 4.5554 | 4.4677 | +0.0877 | 63.97 | 64.61 | 4.110 | 4.002 | 28/0/8 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.5372 | 32 | Severance__1.31-1.51_.16b48k.wav |
| -0.3446 | 32 | 07 applaud.wav |
| -0.2829 | 32 | Asleep__4.11-4.31_.16b48k.wav |
| -0.2525 | 32 | With Love (Outro).16b48k.wav |
| -0.2342 | 40 | 06 trumpet.wav |
| -0.2157 | 32 | 24td_.wav |
| -0.1914 | 32 | 29ru_.wav |
| -0.1896 | 32 | 35_SQAM_glockenspiel_cut.16b48k.wav |
| -0.1764 | 64 | 03 eig.wav |
| -0.1662 | 48 | 07 applaud.wav |
| -0.1451 | 48 | 06 trumpet.wav |
| -0.1397 | 40 | 21-classic.441.16b48k.wav |
| -0.1367 | 32 | 15ma_.wav |
| -0.1305 | 32 | 06 trumpet.wav |
| -0.1226 | 40 | 07 applaud.wav |
