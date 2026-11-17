# faacps vs fdkref  (decoder faad2, cbr/hev2, rungs [24, 32, 48], metric mos)

BD-rate mean **+1.063%** (95% clip-bootstrap CI [-5.898, +8.043], 10000 resamples, seed 0), median +1.772%; valid curves 35/36, no valid curve: 1
PCHIP sign: 19 clips worse / 16 better. Linear (log-rate linear in MOS): **-0.360%** [-7.021, +6.281], n 35, 17 worse / 18 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 24 | 36 | 3.2689 | 3.2819 | -0.0130 | 24.06 | 24.37 | 2.484 | 2.429 | 20/0/16 |
| 32 | 36 | 3.4523 | 3.4961 | -0.0438 | 32.05 | 32.41 | 2.643 | 2.620 | 14/0/22 |
| 48 | 36 | 3.6693 | 3.6512 | +0.0180 | 48.10 | 48.54 | 2.734 | 2.762 | 22/0/14 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.5988 | 48 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.5868 | 32 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.5477 | 24 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.3457 | 24 | 01 castanets.wav |
| -0.3370 | 24 | 24td_.wav |
| -0.2918 | 32 | 24td_.wav |
| -0.2790 | 24 | 03 eig.wav |
| -0.2479 | 48 | Changes.16b48k.wav |
| -0.2283 | 24 | 29ru_.wav |
| -0.2279 | 32 | 01 castanets.wav |
| -0.2207 | 32 | 29ru_.wav |
| -0.2031 | 24 | 25rb_.wav |
| -0.2007 | 32 | Changes.16b48k.wav |
| -0.1991 | 24 | Changes.16b48k.wav |
| -0.1987 | 24 | 06 trumpet.wav |

No valid curve: Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav (no valid curve)
