# faacps vs apple  (decoder ffmpeg, cbr/hev2, rungs [24, 32, 48], metric mos)

BD-rate mean **-0.476%** (95% clip-bootstrap CI [-6.598, +6.034], 10000 resamples, seed 0), median -2.254%; valid curves 36/36, no valid curve: 0
PCHIP sign: 17 clips worse / 19 better. Linear (log-rate linear in MOS): **-0.745%** [-6.769, +5.725], n 36, 17 worse / 19 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 24 | 36 | 3.2990 | 3.2456 | +0.0534 | 24.06 | 24.27 | 2.511 | 2.389 | 20/0/16 |
| 32 | 36 | 3.4905 | 3.4714 | +0.0190 | 32.05 | 32.33 | 2.710 | 2.718 | 19/0/17 |
| 48 | 36 | 3.7231 | 3.7151 | +0.0080 | 48.10 | 48.43 | 2.835 | 2.864 | 16/0/20 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.5293 | 48 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.3727 | 32 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.3262 | 24 | Hey Tonight.16b48k.wav |
| -0.3059 | 24 | 06 trumpet.wav |
| -0.2935 | 24 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.2689 | 32 | 29ru_.wav |
| -0.2226 | 48 | Hey Tonight.16b48k.wav |
| -0.2177 | 48 | 03 eig.wav |
| -0.1859 | 24 | 07 applaud.wav |
| -0.1657 | 24 | 29ru_.wav |
| -0.1495 | 32 | Hey Tonight.16b48k.wav |
| -0.1308 | 24 | 24td_.wav |
| -0.1166 | 32 | 15-Good-evening.441.16b48k.wav |
| -0.1147 | 32 | 35_SQAM_glockenspiel_cut.16b48k.wav |
| -0.1102 | 32 | 24td_.wav |
