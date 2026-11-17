# faac vs xaacref  (decoder fdkdec, cbr/he, rungs [32, 40, 48, 64], metric mos)

BD-rate mean **-9.345%** (95% clip-bootstrap CI [-11.705, -7.054], 10000 resamples, seed 0), median -9.860%; valid curves 77/77, no valid curve: 0
PCHIP sign: 12 clips worse / 65 better. Linear (log-rate linear in MOS): **-8.669%** [-10.914, -6.477], n 77, 13 worse / 64 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 32 | 77 | 3.8275 | 3.7646 | +0.0629 | 31.88 | 32.51 | 3.075 | 2.813 | 41/0/36 |
| 40 | 77 | 4.1323 | 4.0575 | +0.0748 | 39.90 | 40.46 | 3.390 | 3.485 | 56/0/21 |
| 48 | 77 | 4.3100 | 4.1794 | +0.1306 | 47.93 | 48.43 | 3.708 | 3.779 | 66/0/11 |
| 64 | 77 | 4.5328 | 4.4839 | +0.0489 | 63.96 | 64.47 | 4.115 | 4.131 | 56/0/21 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.3675 | 32 | 18vr_.wav |
| -0.3335 | 40 | 13 fatboy_30sec.wav |
| -0.3296 | 32 | take_your_finger_frin_my_head.16b48k.wav |
| -0.2830 | 32 | 01 castanets.wav |
| -0.2797 | 32 | Severance__1.31-1.51_.16b48k.wav |
| -0.2629 | 32 | mof.wav |
| -0.2558 | 64 | 03 eig.wav |
| -0.2549 | 40 | 18vr_.wav |
| -0.2488 | 32 | 13 fatboy_30sec.wav |
| -0.2375 | 32 | 10 Angels_Fall_First_ringing.wav |
| -0.2256 | 32 | 35_SQAM_glockenspiel_cut.16b48k.wav |
| -0.2228 | 32 | SinceAlways.16b48k.wav |
| -0.2208 | 32 | Changes.16b48k.wav |
| -0.2161 | 32 | 9-Have-big-expensive-car.441.16b48k.wav |
| -0.1856 | 48 | 01 castanets.wav |
