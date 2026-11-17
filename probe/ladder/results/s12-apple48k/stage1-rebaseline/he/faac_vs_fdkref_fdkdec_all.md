# faac vs fdkref  (decoder fdkdec, cbr/he, rungs [32, 40, 48, 64], metric mos)

BD-rate mean **-4.549%** (95% clip-bootstrap CI [-7.042, -2.093], 10000 resamples, seed 0), median -5.633%; valid curves 77/77, no valid curve: 0
PCHIP sign: 24 clips worse / 53 better. Linear (log-rate linear in MOS): **-4.700%** [-7.121, -2.293], n 77, 23 worse / 54 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 32 | 77 | 3.8275 | 3.8050 | +0.0225 | 31.88 | 32.62 | 3.075 | 2.724 | 39/0/38 |
| 40 | 77 | 4.1323 | 4.1066 | +0.0257 | 39.90 | 40.59 | 3.390 | 3.397 | 42/0/35 |
| 48 | 77 | 4.3100 | 4.2485 | +0.0615 | 47.93 | 48.57 | 3.708 | 3.854 | 53/0/24 |
| 64 | 77 | 4.5328 | 4.5141 | +0.0187 | 63.96 | 64.62 | 4.115 | 4.136 | 47/0/30 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.5049 | 40 | 13 fatboy_30sec.wav |
| -0.4855 | 32 | 18vr_.wav |
| -0.4289 | 32 | mof.wav |
| -0.3880 | 32 | 10 Angels_Fall_First_ringing.wav |
| -0.3841 | 32 | take_your_finger_frin_my_head.16b48k.wav |
| -0.3631 | 40 | 18vr_.wav |
| -0.3162 | 32 | Severance__1.31-1.51_.16b48k.wav |
| -0.3030 | 32 | 29ru_.wav |
| -0.2970 | 32 | 13 fatboy_30sec.wav |
| -0.2784 | 48 | 13 fatboy_30sec.wav |
| -0.2666 | 32 | Girl_In_The_Fire__Sample_.16b48k.wav |
| -0.2578 | 40 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.2475 | 48 | 18vr_.wav |
| -0.2460 | 32 | SinceAlways.16b48k.wav |
| -0.2444 | 40 | mof.wav |
