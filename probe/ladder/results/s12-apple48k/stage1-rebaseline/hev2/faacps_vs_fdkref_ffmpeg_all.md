# faacps vs fdkref  (decoder ffmpeg, cbr/hev2, rungs [24, 32, 48], metric mos)

BD-rate mean **+5.013%** (95% clip-bootstrap CI [+0.304, +9.731], 10000 resamples, seed 0), median +3.176%; valid curves 75/77, no valid curve: 2
PCHIP sign: 44 clips worse / 31 better. Linear (log-rate linear in MOS): **+2.225%** [-2.111, +6.569], n 75, 39 worse / 36 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 24 | 77 | 3.3087 | 3.3266 | -0.0179 | 24.07 | 24.38 | 2.220 | 2.240 | 38/0/39 |
| 32 | 77 | 3.4934 | 3.5482 | -0.0548 | 32.06 | 32.42 | 2.349 | 2.384 | 25/0/52 |
| 48 | 77 | 3.7462 | 3.7011 | +0.0451 | 48.11 | 48.55 | 2.534 | 2.497 | 56/0/21 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.5851 | 32 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.5668 | 48 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.5311 | 24 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.5102 | 32 | 10 Angels_Fall_First_ringing.wav |
| -0.5066 | 24 | 10 Angels_Fall_First_ringing.wav |
| -0.3955 | 24 | SinceAlways.16b48k.wav |
| -0.3918 | 24 | 01 castanets.wav |
| -0.3787 | 32 | 13 fatboy_30sec.wav |
| -0.3476 | 24 | 24td_.wav |
| -0.3090 | 32 | 9-Have-big-expensive-car.441.16b48k.wav |
| -0.2992 | 32 | 24td_.wav |
| -0.2851 | 32 | SinceAlways.16b48k.wav |
| -0.2783 | 24 | 03 eig.wav |
| -0.2762 | 24 | Berling Drug.wav |
| -0.2732 | 24 | Hey Tonight.16b48k.wav |

No valid curve: Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav (no valid curve), mof.wav (no valid curve)
