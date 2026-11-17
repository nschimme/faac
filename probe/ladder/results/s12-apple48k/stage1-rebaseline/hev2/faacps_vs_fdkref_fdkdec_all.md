# faacps vs fdkref  (decoder fdkdec, cbr/hev2, rungs [24, 32, 48], metric mos)

BD-rate mean **+4.302%** (95% clip-bootstrap CI [-0.482, +9.060], 10000 resamples, seed 0), median +3.418%; valid curves 75/77, no valid curve: 2
PCHIP sign: 44 clips worse / 31 better. Linear (log-rate linear in MOS): **+1.546%** [-2.816, +5.895], n 75, 38 worse / 37 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 24 | 77 | 3.3133 | 3.3276 | -0.0143 | 24.07 | 24.38 | 2.208 | 2.242 | 40/0/37 |
| 32 | 77 | 3.4965 | 3.5499 | -0.0534 | 32.06 | 32.42 | 2.348 | 2.383 | 25/0/52 |
| 48 | 77 | 3.7496 | 3.7019 | +0.0477 | 48.11 | 48.55 | 2.539 | 2.496 | 57/0/20 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.5697 | 32 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.5656 | 48 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.5444 | 24 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.5113 | 32 | 10 Angels_Fall_First_ringing.wav |
| -0.5056 | 24 | 10 Angels_Fall_First_ringing.wav |
| -0.4643 | 32 | 13 fatboy_30sec.wav |
| -0.3931 | 24 | SinceAlways.16b48k.wav |
| -0.3519 | 24 | 24td_.wav |
| -0.3109 | 24 | 01 castanets.wav |
| -0.3011 | 32 | 9-Have-big-expensive-car.441.16b48k.wav |
| -0.2968 | 32 | 24td_.wav |
| -0.2770 | 24 | 03 eig.wav |
| -0.2748 | 32 | SinceAlways.16b48k.wav |
| -0.2604 | 48 | Changes.16b48k.wav |
| -0.2506 | 24 | 06 trumpet.wav |

No valid curve: Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav (no valid curve), mof.wav (no valid curve)
