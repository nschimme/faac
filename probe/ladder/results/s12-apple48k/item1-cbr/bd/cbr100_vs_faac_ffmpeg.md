# cbr100 vs faac  (decoder ffmpeg, cbr/lc, rungs [128, 160, 192, 256, 320], metric mos)

BD-rate mean **-0.810%** (95% clip-bootstrap CI [-1.136, -0.417], 10000 resamples, seed 0), median -0.934%; valid curves 77/77, no valid curve: 0
PCHIP sign: 5 clips worse / 72 better. Linear (log-rate linear in MOS): **-0.826%** [-1.146, -0.441], n 77, 5 worse / 72 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 77 | 4.8694 | 4.8660 | +0.0035 | 128.17 | 127.95 | 4.668 | 4.704 | 68/0/9 |
| 160 | 77 | 4.9270 | 4.9257 | +0.0013 | 160.23 | 160.05 | 4.820 | 4.835 | 71/0/6 |
| 192 | 77 | 4.9557 | 4.9549 | +0.0007 | 192.29 | 192.15 | 4.891 | 4.894 | 69/0/8 |
| 256 | 77 | 4.9819 | 4.9814 | +0.0005 | 256.39 | 256.31 | 4.958 | 4.959 | 69/0/8 |
| 320 | 77 | 4.9909 | 4.9907 | +0.0002 | 320.50 | 320.47 | 4.981 | 4.981 | 66/0/11 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0688 | 128 | 08 velvet.wav |
| -0.0481 | 160 | 08 velvet.wav |
| -0.0260 | 128 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0231 | 192 | 08 velvet.wav |
| -0.0126 | 160 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0099 | 256 | 08 velvet.wav |
| -0.0077 | 128 | 24td_.wav |
| -0.0072 | 192 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0059 | 160 | 10 Angels_Fall_First_ringing.wav |
| -0.0031 | 192 | mof.wav |
| -0.0026 | 320 | 08 velvet.wav |
| -0.0022 | 192 | 10 Angels_Fall_First_ringing.wav |
| -0.0020 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0016 | 128 | mof.wav |
| -0.0011 | 192 | 18vr_.wav |
