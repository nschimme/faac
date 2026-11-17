# faac vs fdkref  (decoder faad2, cbr/he, rungs [32, 40, 48, 64], metric mos)

BD-rate mean **-5.518%** (95% clip-bootstrap CI [-7.863, -3.210], 10000 resamples, seed 0), median -5.890%; valid curves 77/77, no valid curve: 0
PCHIP sign: 19 clips worse / 58 better. Linear (log-rate linear in MOS): **-5.719%** [-7.932, -3.487], n 77, 18 worse / 59 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 32 | 77 | 3.8431 | 3.8356 | +0.0075 | 31.88 | 32.62 | 3.132 | 2.932 | 37/0/40 |
| 40 | 77 | 4.1673 | 4.1342 | +0.0331 | 39.90 | 40.59 | 3.375 | 3.422 | 43/0/34 |
| 48 | 77 | 4.3485 | 4.2673 | +0.0811 | 47.93 | 48.57 | 3.718 | 3.836 | 62/0/15 |
| 64 | 77 | 4.5695 | 4.5247 | +0.0448 | 63.96 | 64.62 | 4.143 | 4.152 | 59/0/18 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.4744 | 40 | 13 fatboy_30sec.wav |
| -0.4472 | 32 | 18vr_.wav |
| -0.4358 | 32 | mof.wav |
| -0.4180 | 32 | take_your_finger_frin_my_head.16b48k.wav |
| -0.4155 | 32 | Severance__1.31-1.51_.16b48k.wav |
| -0.3767 | 32 | 10 Angels_Fall_First_ringing.wav |
| -0.3142 | 40 | 18vr_.wav |
| -0.3080 | 32 | Girl_In_The_Fire__Sample_.16b48k.wav |
| -0.3000 | 32 | 24td_.wav |
| -0.2695 | 32 | 29ru_.wav |
| -0.2667 | 32 | SinceAlways.16b48k.wav |
| -0.2594 | 48 | 13 fatboy_30sec.wav |
| -0.2436 | 32 | 13 fatboy_30sec.wav |
| -0.2297 | 32 | TrosYGareg.16b48k.wav |
| -0.2238 | 48 | 18vr_.wav |
