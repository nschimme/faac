# cbr100 vs apple  (decoder fdkdec, cbr/lc, rungs [128, 160, 192, 256, 320], metric mos)

BD-rate mean **-1.850%** (95% clip-bootstrap CI [-3.510, -0.207], 10000 resamples, seed 0), median -2.264%; valid curves 77/77, no valid curve: 0
PCHIP sign: 24 clips worse / 53 better. Linear (log-rate linear in MOS): **-1.655%** [-3.314, -0.021], n 77, 25 worse / 52 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 77 | 4.8690 | 4.8466 | +0.0224 | 128.17 | 128.56 | 4.671 | 4.488 | 55/0/22 |
| 160 | 77 | 4.9269 | 4.9192 | +0.0077 | 160.23 | 160.64 | 4.820 | 4.766 | 52/0/25 |
| 192 | 77 | 4.9557 | 4.9580 | -0.0023 | 192.29 | 192.74 | 4.891 | 4.898 | 20/0/57 |
| 256 | 77 | 4.9819 | 4.9795 | +0.0024 | 256.39 | 256.98 | 4.958 | 4.952 | 49/0/28 |
| 320 | 77 | 4.9909 | 4.9919 | -0.0010 | 320.50 | 321.19 | 4.981 | 4.982 | 34/0/43 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0859 | 128 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0845 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0771 | 160 | 08 velvet.wav |
| -0.0638 | 128 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0597 | 192 | 08 velvet.wav |
| -0.0546 | 128 | SinceAlways.16b48k.wav |
| -0.0463 | 128 | 18vr_.wav |
| -0.0456 | 160 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0336 | 128 | 14fe_.wav |
| -0.0313 | 160 | SinceAlways.16b48k.wav |
| -0.0312 | 128 | 10 Angels_Fall_First_ringing.wav |
| -0.0303 | 128 | Girl_In_The_Fire__Sample_.16b48k.wav |
| -0.0295 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0289 | 128 | 08 velvet.wav |
| -0.0279 | 192 | take_your_finger_frin_my_head.16b48k.wav |
