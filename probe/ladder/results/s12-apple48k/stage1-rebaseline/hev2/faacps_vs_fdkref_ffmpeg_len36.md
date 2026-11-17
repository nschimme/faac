# faacps vs fdkref  (decoder ffmpeg, cbr/hev2, rungs [24, 32, 48], metric mos)

BD-rate mean **+2.561%** (95% clip-bootstrap CI [-4.212, +9.237], 10000 resamples, seed 0), median +1.416%; valid curves 35/36, no valid curve: 1
PCHIP sign: 19 clips worse / 16 better. Linear (log-rate linear in MOS): **+0.719%** [-5.823, +7.167], n 35, 18 worse / 17 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 24 | 36 | 3.2990 | 3.3230 | -0.0241 | 24.06 | 24.37 | 2.511 | 2.471 | 17/0/19 |
| 32 | 36 | 3.4905 | 3.5420 | -0.0516 | 32.05 | 32.41 | 2.710 | 2.726 | 13/0/23 |
| 48 | 36 | 3.7231 | 3.6955 | +0.0276 | 48.10 | 48.54 | 2.835 | 2.864 | 25/0/11 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.5851 | 32 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.5668 | 48 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.5311 | 24 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.3918 | 24 | 01 castanets.wav |
| -0.3476 | 24 | 24td_.wav |
| -0.2992 | 32 | 24td_.wav |
| -0.2783 | 24 | 03 eig.wav |
| -0.2732 | 24 | Hey Tonight.16b48k.wav |
| -0.2592 | 48 | Changes.16b48k.wav |
| -0.2518 | 32 | 01 castanets.wav |
| -0.2505 | 24 | 06 trumpet.wav |
| -0.2278 | 24 | 29ru_.wav |
| -0.2257 | 32 | 29ru_.wav |
| -0.2175 | 32 | Changes.16b48k.wav |
| -0.1919 | 48 | Hey Tonight.16b48k.wav |

No valid curve: Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav (no valid curve)
