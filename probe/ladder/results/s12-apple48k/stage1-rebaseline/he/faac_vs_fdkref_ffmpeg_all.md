# faac vs fdkref  (decoder ffmpeg, cbr/he, rungs [32, 40, 48, 64], metric mos)

BD-rate mean **-5.422%** (95% clip-bootstrap CI [-7.765, -3.089], 10000 resamples, seed 0), median -5.634%; valid curves 77/77, no valid curve: 0
PCHIP sign: 19 clips worse / 58 better. Linear (log-rate linear in MOS): **-5.569%** [-7.770, -3.324], n 77, 18 worse / 59 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 32 | 77 | 3.8428 | 3.8348 | +0.0080 | 31.88 | 32.62 | 3.101 | 2.927 | 36/0/41 |
| 40 | 77 | 4.1657 | 4.1341 | +0.0316 | 39.90 | 40.59 | 3.366 | 3.415 | 43/0/34 |
| 48 | 77 | 4.3459 | 4.2673 | +0.0786 | 47.93 | 48.57 | 3.722 | 3.837 | 60/0/17 |
| 64 | 77 | 4.5693 | 4.5260 | +0.0432 | 63.96 | 64.62 | 4.148 | 4.154 | 59/0/18 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.4982 | 40 | 13 fatboy_30sec.wav |
| -0.4532 | 32 | 18vr_.wav |
| -0.4383 | 32 | mof.wav |
| -0.4181 | 32 | take_your_finger_frin_my_head.16b48k.wav |
| -0.4086 | 32 | Severance__1.31-1.51_.16b48k.wav |
| -0.3922 | 32 | 10 Angels_Fall_First_ringing.wav |
| -0.3554 | 32 | 13 fatboy_30sec.wav |
| -0.3179 | 40 | 18vr_.wav |
| -0.2968 | 32 | 24td_.wav |
| -0.2932 | 32 | Girl_In_The_Fire__Sample_.16b48k.wav |
| -0.2697 | 32 | SinceAlways.16b48k.wav |
| -0.2588 | 48 | 13 fatboy_30sec.wav |
| -0.2576 | 32 | 29ru_.wav |
| -0.2311 | 32 | TrosYGareg.16b48k.wav |
| -0.2287 | 48 | 18vr_.wav |
