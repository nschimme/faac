# faac vs apple  (decoder ffmpeg, cbr/lc, rungs [128, 160, 192, 256, 320], metric mos)

BD-rate mean **-1.143%** (95% clip-bootstrap CI [-2.765, +0.425], 10000 resamples, seed 0), median -1.482%; valid curves 77/77, no valid curve: 0
PCHIP sign: 29 clips worse / 48 better. Linear (log-rate linear in MOS): **-0.955%** [-2.569, +0.623], n 77, 30 worse / 47 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 77 | 4.8660 | 4.8466 | +0.0194 | 127.95 | 128.56 | 4.704 | 4.488 | 50/0/27 |
| 160 | 77 | 4.9257 | 4.9192 | +0.0065 | 160.05 | 160.64 | 4.835 | 4.766 | 47/0/30 |
| 192 | 77 | 4.9549 | 4.9580 | -0.0030 | 192.15 | 192.74 | 4.894 | 4.898 | 16/0/61 |
| 256 | 77 | 4.9814 | 4.9795 | +0.0019 | 256.31 | 256.98 | 4.959 | 4.952 | 44/0/33 |
| 320 | 77 | 4.9907 | 4.9919 | -0.0012 | 320.47 | 321.19 | 4.981 | 4.982 | 31/0/46 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0785 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0727 | 128 | 10xh_.wav |
| -0.0573 | 128 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0573 | 128 | SinceAlways.16b48k.wav |
| -0.0558 | 128 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0465 | 128 | 18vr_.wav |
| -0.0438 | 128 | fms.wav |
| -0.0397 | 128 | 14fe_.wav |
| -0.0375 | 192 | 08 velvet.wav |
| -0.0354 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0348 | 128 | Girl_In_The_Fire__Sample_.16b48k.wav |
| -0.0333 | 160 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0332 | 160 | SinceAlways.16b48k.wav |
| -0.0302 | 128 | 10 Angels_Fall_First_ringing.wav |
| -0.0287 | 160 | 08 velvet.wav |
