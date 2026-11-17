# faacps vs fdkref  (decoder fdkdec, cbr/hev2, rungs [24, 32, 48], metric mos)

BD-rate mean **+1.179%** (95% clip-bootstrap CI [-5.475, +7.807], 10000 resamples, seed 0), median +1.281%; valid curves 35/36, no valid curve: 1
PCHIP sign: 18 clips worse / 17 better. Linear (log-rate linear in MOS): **-0.273%** [-6.675, +6.078], n 35, 18 worse / 17 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 24 | 36 | 3.2996 | 3.3236 | -0.0241 | 24.06 | 24.37 | 2.507 | 2.475 | 18/0/18 |
| 32 | 36 | 3.4941 | 3.5429 | -0.0487 | 32.05 | 32.41 | 2.704 | 2.724 | 13/0/23 |
| 48 | 36 | 3.7270 | 3.6963 | +0.0307 | 48.10 | 48.54 | 2.836 | 2.862 | 25/0/11 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.5697 | 32 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.5656 | 48 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.5444 | 24 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.3519 | 24 | 24td_.wav |
| -0.3109 | 24 | 01 castanets.wav |
| -0.2968 | 32 | 24td_.wav |
| -0.2770 | 24 | 03 eig.wav |
| -0.2604 | 48 | Changes.16b48k.wav |
| -0.2506 | 24 | 06 trumpet.wav |
| -0.2370 | 32 | 01 castanets.wav |
| -0.2272 | 24 | 29ru_.wav |
| -0.2169 | 32 | 29ru_.wav |
| -0.2077 | 32 | Changes.16b48k.wav |
| -0.1955 | 24 | Hey Tonight.16b48k.wav |
| -0.1946 | 24 | Changes.16b48k.wav |

No valid curve: Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav (no valid curve)
