# faacps vs xaacref  (decoder fdkdec, cbr/hev2, rungs [24, 32, 48], metric mos)

BD-rate mean **+8.558%** (95% clip-bootstrap CI [+3.321, +14.034], 10000 resamples, seed 0), median +8.045%; valid curves 75/77, no valid curve: 2
PCHIP sign: 50 clips worse / 25 better. Linear (log-rate linear in MOS): **+5.630%** [+0.596, +10.933], n 75, 49 worse / 26 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 24 | 77 | 3.3133 | 3.3349 | -0.0216 | 24.07 | 24.27 | 2.208 | 2.346 | 33/0/44 |
| 32 | 77 | 3.4965 | 3.5501 | -0.0536 | 32.06 | 32.31 | 2.348 | 2.432 | 27/0/50 |
| 48 | 77 | 3.7496 | 3.6877 | +0.0618 | 48.11 | 48.37 | 2.539 | 2.476 | 59/0/18 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.5021 | 32 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.4860 | 48 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.4516 | 24 | 10 Angels_Fall_First_ringing.wav |
| -0.4493 | 32 | 13 fatboy_30sec.wav |
| -0.4286 | 24 | 9-Have-big-expensive-car.441.16b48k.wav |
| -0.4261 | 32 | 10 Angels_Fall_First_ringing.wav |
| -0.3651 | 24 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.3637 | 32 | 9-Have-big-expensive-car.441.16b48k.wav |
| -0.3473 | 24 | 03 eig.wav |
| -0.3391 | 24 | SinceAlways.16b48k.wav |
| -0.2633 | 32 | 29ru_.wav |
| -0.2578 | 24 | 18vr_.wav |
| -0.2554 | 24 | 29ru_.wav |
| -0.2454 | 32 | SinceAlways.16b48k.wav |
| -0.2328 | 24 | Coral.16b48k.wav |

No valid curve: 27-last-song-drums-and-trampets.441.16b48k.wav (no valid curve), mof.wav (no valid curve)
