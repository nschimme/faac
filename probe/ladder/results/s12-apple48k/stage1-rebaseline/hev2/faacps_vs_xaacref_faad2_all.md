# faacps vs xaacref  (decoder faad2, cbr/hev2, rungs [24, 32, 48], metric mos)

BD-rate mean **+7.186%** (95% clip-bootstrap CI [+1.839, +12.764], 10000 resamples, seed 0), median +7.107%; valid curves 75/77, no valid curve: 2
PCHIP sign: 50 clips worse / 25 better. Linear (log-rate linear in MOS): **+4.070%** [-1.189, +9.456], n 75, 47 worse / 28 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 24 | 77 | 3.2889 | 3.3036 | -0.0147 | 24.07 | 24.27 | 2.164 | 2.274 | 33/0/44 |
| 32 | 77 | 3.4640 | 3.5168 | -0.0528 | 32.06 | 32.31 | 2.287 | 2.355 | 26/0/51 |
| 48 | 77 | 3.7014 | 3.6484 | +0.0529 | 48.11 | 48.37 | 2.472 | 2.403 | 58/0/19 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.5172 | 32 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.5135 | 48 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.4448 | 24 | 10 Angels_Fall_First_ringing.wav |
| -0.4121 | 32 | 10 Angels_Fall_First_ringing.wav |
| -0.3830 | 24 | 9-Have-big-expensive-car.441.16b48k.wav |
| -0.3647 | 32 | 13 fatboy_30sec.wav |
| -0.3639 | 24 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.3454 | 32 | 9-Have-big-expensive-car.441.16b48k.wav |
| -0.3420 | 24 | SinceAlways.16b48k.wav |
| -0.3179 | 24 | 03 eig.wav |
| -0.3070 | 48 | lob.wav |
| -0.2636 | 32 | 29ru_.wav |
| -0.2521 | 24 | 29ru_.wav |
| -0.2482 | 24 | 35_SQAM_glockenspiel_cut.16b48k.wav |
| -0.2431 | 32 | SinceAlways.16b48k.wav |

No valid curve: lob.wav (no valid curve), mof.wav (no valid curve)
