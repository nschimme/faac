# faacps vs apple  (decoder fdkdec, cbr/hev2, rungs [24, 32, 48], metric mos)

BD-rate mean **+0.462%** (95% clip-bootstrap CI [-4.374, +5.311], 10000 resamples, seed 0), median +1.209%; valid curves 76/77, no valid curve: 1
PCHIP sign: 41 clips worse / 35 better. Linear (log-rate linear in MOS): **+0.626%** [-4.033, +5.299], n 76, 43 worse / 33 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 24 | 77 | 3.3133 | 3.2847 | +0.0285 | 24.07 | 24.30 | 2.208 | 2.056 | 36/0/41 |
| 32 | 77 | 3.4965 | 3.4736 | +0.0229 | 32.06 | 32.36 | 2.348 | 2.148 | 33/0/44 |
| 48 | 77 | 3.7496 | 3.7273 | +0.0223 | 48.11 | 48.43 | 2.539 | 2.512 | 44/0/33 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.5268 | 48 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.5031 | 32 | 10 Angels_Fall_First_ringing.wav |
| -0.4899 | 24 | 10 Angels_Fall_First_ringing.wav |
| -0.3579 | 32 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.3075 | 24 | 06 trumpet.wav |
| -0.3033 | 24 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.2608 | 32 | 29ru_.wav |
| -0.2571 | 24 | Coral.16b48k.wav |
| -0.2510 | 24 | Berling Drug.wav |
| -0.2499 | 24 | Hey Tonight.16b48k.wav |
| -0.2408 | 32 | Coral.16b48k.wav |
| -0.2325 | 24 | SinceAlways.16b48k.wav |
| -0.2228 | 48 | Hey Tonight.16b48k.wav |
| -0.2220 | 48 | lob.wav |
| -0.2195 | 24 | TrosYGareg.16b48k.wav |

No valid curve: mof.wav (no valid curve)
