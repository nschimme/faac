# faacps vs apple  (decoder fdkdec, cbr/hev2, rungs [24, 32, 48], metric mos)

BD-rate mean **-1.243%** (95% clip-bootstrap CI [-7.577, +5.333], 10000 resamples, seed 0), median -1.997%; valid curves 36/36, no valid curve: 0
PCHIP sign: 17 clips worse / 19 better. Linear (log-rate linear in MOS): **-1.255%** [-7.472, +5.271], n 36, 17 worse / 19 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 24 | 36 | 3.2996 | 3.2474 | +0.0522 | 24.06 | 24.27 | 2.507 | 2.389 | 20/0/16 |
| 32 | 36 | 3.4941 | 3.4724 | +0.0217 | 32.05 | 32.33 | 2.704 | 2.717 | 19/0/17 |
| 48 | 36 | 3.7270 | 3.7161 | +0.0110 | 48.10 | 48.43 | 2.836 | 2.862 | 18/0/18 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.5268 | 48 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.3579 | 32 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.3075 | 24 | 06 trumpet.wav |
| -0.3033 | 24 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.2608 | 32 | 29ru_.wav |
| -0.2499 | 24 | Hey Tonight.16b48k.wav |
| -0.2228 | 48 | Hey Tonight.16b48k.wav |
| -0.2064 | 48 | 03 eig.wav |
| -0.1877 | 24 | 07 applaud.wav |
| -0.1659 | 24 | 29ru_.wav |
| -0.1503 | 32 | Hey Tonight.16b48k.wav |
| -0.1351 | 24 | 24td_.wav |
| -0.1315 | 24 | 25rb_.wav |
| -0.1193 | 32 | 35_SQAM_glockenspiel_cut.16b48k.wav |
| -0.1091 | 32 | 24td_.wav |
