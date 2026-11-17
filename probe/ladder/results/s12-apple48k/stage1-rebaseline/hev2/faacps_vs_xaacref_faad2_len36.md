# faacps vs xaacref  (decoder faad2, cbr/hev2, rungs [24, 32, 48], metric mos)

BD-rate mean **+4.339%** (95% clip-bootstrap CI [-3.455, +12.425], 10000 resamples, seed 0), median +6.713%; valid curves 36/36, no valid curve: 0
PCHIP sign: 22 clips worse / 14 better. Linear (log-rate linear in MOS): **+1.448%** [-6.643, +9.721], n 36, 20 worse / 16 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 24 | 36 | 3.2689 | 3.2731 | -0.0041 | 24.06 | 24.26 | 2.484 | 2.471 | 17/0/19 |
| 32 | 36 | 3.4523 | 3.4931 | -0.0408 | 32.05 | 32.30 | 2.643 | 2.615 | 14/0/22 |
| 48 | 36 | 3.6693 | 3.6235 | +0.0458 | 48.10 | 48.36 | 2.734 | 2.678 | 26/0/10 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.5172 | 32 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.5135 | 48 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.3639 | 24 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.3179 | 24 | 03 eig.wav |
| -0.2636 | 32 | 29ru_.wav |
| -0.2521 | 24 | 29ru_.wav |
| -0.2482 | 24 | 35_SQAM_glockenspiel_cut.16b48k.wav |
| -0.2239 | 24 | Changes.16b48k.wav |
| -0.2160 | 32 | 25rb_.wav |
| -0.2087 | 32 | 01 castanets.wav |
| -0.2072 | 32 | 03 eig.wav |
| -0.1983 | 32 | 24td_.wav |
| -0.1854 | 48 | Hey Tonight.16b48k.wav |
| -0.1828 | 24 | 06 trumpet.wav |
| -0.1791 | 32 | Hey Tonight.16b48k.wav |
