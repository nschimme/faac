# faacps vs xaacref  (decoder ffmpeg, cbr/hev2, rungs [24, 32, 48], metric mos)

BD-rate mean **+9.232%** (95% clip-bootstrap CI [+4.026, +14.711], 10000 resamples, seed 0), median +7.403%; valid curves 75/77, no valid curve: 2
PCHIP sign: 51 clips worse / 24 better. Linear (log-rate linear in MOS): **+6.040%** [+1.003, +11.353], n 75, 46 worse / 29 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 24 | 77 | 3.3087 | 3.3345 | -0.0258 | 24.07 | 24.27 | 2.220 | 2.345 | 31/0/46 |
| 32 | 77 | 3.4934 | 3.5511 | -0.0577 | 32.06 | 32.31 | 2.349 | 2.431 | 24/0/53 |
| 48 | 77 | 3.7462 | 3.6877 | +0.0585 | 48.11 | 48.37 | 2.534 | 2.475 | 59/0/18 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.5173 | 32 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.4877 | 48 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.4524 | 24 | 10 Angels_Fall_First_ringing.wav |
| -0.4250 | 32 | 10 Angels_Fall_First_ringing.wav |
| -0.4215 | 24 | 9-Have-big-expensive-car.441.16b48k.wav |
| -0.3657 | 32 | 9-Have-big-expensive-car.441.16b48k.wav |
| -0.3633 | 32 | 13 fatboy_30sec.wav |
| -0.3551 | 24 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.3408 | 24 | SinceAlways.16b48k.wav |
| -0.3325 | 24 | 03 eig.wav |
| -0.2720 | 32 | 29ru_.wav |
| -0.2598 | 24 | Hey Tonight.16b48k.wav |
| -0.2562 | 24 | 29ru_.wav |
| -0.2546 | 32 | SinceAlways.16b48k.wav |
| -0.2431 | 24 | 18vr_.wav |

No valid curve: 27-last-song-drums-and-trampets.441.16b48k.wav (no valid curve), mof.wav (no valid curve)
