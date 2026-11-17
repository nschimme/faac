# faacps vs fdkref  (decoder faad2, cbr/hev2, rungs [24, 32, 48], metric mos)

BD-rate mean **+4.626%** (95% clip-bootstrap CI [-0.587, +10.095], 10000 resamples, seed 0), median +3.444%; valid curves 75/77, no valid curve: 2
PCHIP sign: 43 clips worse / 32 better. Linear (log-rate linear in MOS): **+2.117%** [-2.723, +7.225], n 75, 38 worse / 37 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 24 | 77 | 3.2889 | 3.2969 | -0.0079 | 24.07 | 24.38 | 2.164 | 2.160 | 42/0/35 |
| 32 | 77 | 3.4640 | 3.5133 | -0.0493 | 32.06 | 32.42 | 2.287 | 2.286 | 27/0/50 |
| 48 | 77 | 3.7014 | 3.6639 | +0.0374 | 48.11 | 48.55 | 2.472 | 2.362 | 53/0/24 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.5988 | 48 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.5868 | 32 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.5477 | 24 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.4971 | 24 | 10 Angels_Fall_First_ringing.wav |
| -0.4940 | 32 | 10 Angels_Fall_First_ringing.wav |
| -0.3878 | 24 | SinceAlways.16b48k.wav |
| -0.3854 | 32 | 13 fatboy_30sec.wav |
| -0.3457 | 24 | 01 castanets.wav |
| -0.3370 | 24 | 24td_.wav |
| -0.3225 | 32 | 9-Have-big-expensive-car.441.16b48k.wav |
| -0.2918 | 32 | 24td_.wav |
| -0.2790 | 24 | 03 eig.wav |
| -0.2677 | 32 | SinceAlways.16b48k.wav |
| -0.2530 | 48 | 9-Have-big-expensive-car.441.16b48k.wav |
| -0.2505 | 48 | lob.wav |

No valid curve: Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav (no valid curve), mof.wav (no valid curve)
