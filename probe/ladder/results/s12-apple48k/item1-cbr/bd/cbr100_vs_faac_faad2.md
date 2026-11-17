# cbr100 vs faac  (decoder faad2, cbr/lc, rungs [128, 160, 192, 256, 320], metric mos)

BD-rate mean **-0.800%** (95% clip-bootstrap CI [-1.121, -0.415], 10000 resamples, seed 0), median -0.901%; valid curves 77/77, no valid curve: 0
PCHIP sign: 5 clips worse / 72 better. Linear (log-rate linear in MOS): **-0.811%** [-1.126, -0.433], n 77, 6 worse / 71 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 77 | 4.8696 | 4.8663 | +0.0032 | 128.17 | 127.95 | 4.669 | 4.707 | 68/0/9 |
| 160 | 77 | 4.9271 | 4.9258 | +0.0013 | 160.23 | 160.05 | 4.820 | 4.835 | 70/0/7 |
| 192 | 77 | 4.9557 | 4.9550 | +0.0008 | 192.29 | 192.15 | 4.891 | 4.895 | 70/0/7 |
| 256 | 77 | 4.9819 | 4.9814 | +0.0005 | 256.39 | 256.31 | 4.958 | 4.959 | 69/0/8 |
| 320 | 77 | 4.9909 | 4.9907 | +0.0002 | 320.50 | 320.47 | 4.981 | 4.981 | 66/0/11 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0696 | 128 | 08 velvet.wav |
| -0.0475 | 160 | 08 velvet.wav |
| -0.0228 | 128 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0223 | 192 | 08 velvet.wav |
| -0.0121 | 160 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0099 | 256 | 08 velvet.wav |
| -0.0077 | 128 | 24td_.wav |
| -0.0073 | 192 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0057 | 160 | 10 Angels_Fall_First_ringing.wav |
| -0.0038 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0028 | 192 | mof.wav |
| -0.0026 | 320 | 08 velvet.wav |
| -0.0025 | 128 | 18vr_.wav |
| -0.0023 | 128 | mof.wav |
| -0.0022 | 192 | 10 Angels_Fall_First_ringing.wav |
