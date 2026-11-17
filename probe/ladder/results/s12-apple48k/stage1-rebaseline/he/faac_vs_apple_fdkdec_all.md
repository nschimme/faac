# faac vs apple  (decoder fdkdec, cbr/he, rungs [32, 40, 48, 64], metric mos)

BD-rate mean **-6.983%** (95% clip-bootstrap CI [-10.261, -3.835], 10000 resamples, seed 0), median -5.012%; valid curves 76/77, no valid curve: 1
PCHIP sign: 26 clips worse / 50 better. Linear (log-rate linear in MOS): **-6.713%** [-9.937, -3.658], n 76, 25 worse / 51 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 32 | 77 | 3.8275 | 3.7665 | +0.0610 | 31.88 | 32.48 | 3.075 | 2.601 | 35/0/42 |
| 40 | 77 | 4.1323 | 4.0196 | +0.1127 | 39.90 | 40.46 | 3.390 | 2.889 | 46/0/31 |
| 48 | 77 | 4.3100 | 4.1933 | +0.1166 | 47.93 | 48.52 | 3.708 | 3.363 | 49/0/28 |
| 64 | 77 | 4.5328 | 4.4497 | +0.0831 | 63.96 | 64.61 | 4.115 | 3.490 | 48/0/29 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.4798 | 32 | Severance__1.31-1.51_.16b48k.wav |
| -0.4284 | 32 | take_your_finger_frin_my_head.16b48k.wav |
| -0.4257 | 32 | 10 Angels_Fall_First_ringing.wav |
| -0.4086 | 32 | Girl_In_The_Fire__Sample_.16b48k.wav |
| -0.3460 | 32 | 04 Bachpsichord.wav |
| -0.3363 | 32 | 07 applaud.wav |
| -0.2898 | 32 | SinceAlways.16b48k.wav |
| -0.2772 | 32 | Asleep__4.11-4.31_.16b48k.wav |
| -0.2682 | 32 | hrp.wav |
| -0.2629 | 32 | TrosYGareg.16b48k.wav |
| -0.2587 | 32 | Berling Drug.wav |
| -0.2583 | 32 | 11ff_.wav |
| -0.2567 | 32 | Through The Fire And Flames.16b48k.wav |
| -0.2480 | 32 | 29ru_.wav |
| -0.2472 | 32 | Jupiter, the Bringer of Jolity.16b48k.wav |

No valid curve: girl.16b48k.wav (no valid curve)
