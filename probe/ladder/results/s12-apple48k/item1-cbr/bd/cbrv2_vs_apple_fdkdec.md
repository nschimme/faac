# cbrv2 vs apple  (decoder fdkdec, cbr/lc, rungs [128, 160, 192, 256, 320], metric mos)

BD-rate mean **-1.785%** (95% clip-bootstrap CI [-3.380, -0.226], 10000 resamples, seed 0), median -2.044%; valid curves 77/77, no valid curve: 0
PCHIP sign: 23 clips worse / 54 better. Linear (log-rate linear in MOS): **-1.596%** [-3.197, -0.037], n 77, 25 worse / 52 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 77 | 4.8688 | 4.8466 | +0.0222 | 128.10 | 128.56 | 4.694 | 4.488 | 55/0/22 |
| 160 | 77 | 4.9273 | 4.9192 | +0.0081 | 160.15 | 160.64 | 4.843 | 4.766 | 50/0/27 |
| 192 | 77 | 4.9559 | 4.9580 | -0.0021 | 192.22 | 192.74 | 4.896 | 4.898 | 20/0/57 |
| 256 | 77 | 4.9819 | 4.9795 | +0.0024 | 256.33 | 256.98 | 4.960 | 4.952 | 50/0/27 |
| 320 | 77 | 4.9909 | 4.9919 | -0.0010 | 320.47 | 321.19 | 4.981 | 4.982 | 31/0/46 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0878 | 128 | 10xh_.wav |
| -0.0750 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0549 | 128 | SinceAlways.16b48k.wav |
| -0.0527 | 128 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0505 | 128 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0451 | 128 | 18vr_.wav |
| -0.0389 | 128 | fms.wav |
| -0.0375 | 128 | 14fe_.wav |
| -0.0361 | 192 | 08 velvet.wav |
| -0.0347 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0320 | 160 | SinceAlways.16b48k.wav |
| -0.0307 | 160 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0301 | 128 | Girl_In_The_Fire__Sample_.16b48k.wav |
| -0.0267 | 128 | 10 Angels_Fall_First_ringing.wav |
| -0.0226 | 192 | SinceAlways.16b48k.wav |
