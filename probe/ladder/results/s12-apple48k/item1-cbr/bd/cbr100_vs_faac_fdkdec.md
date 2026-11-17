# cbr100 vs faac  (decoder fdkdec, cbr/lc, rungs [128, 160, 192, 256, 320], metric mos)

BD-rate mean **-0.808%** (95% clip-bootstrap CI [-1.131, -0.426], 10000 resamples, seed 0), median -0.912%; valid curves 77/77, no valid curve: 0
PCHIP sign: 7 clips worse / 70 better. Linear (log-rate linear in MOS): **-0.811%** [-1.131, -0.430], n 77, 5 worse / 72 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 77 | 4.8690 | 4.8658 | +0.0032 | 128.17 | 127.95 | 4.671 | 4.706 | 65/0/12 |
| 160 | 77 | 4.9269 | 4.9255 | +0.0014 | 160.23 | 160.05 | 4.820 | 4.834 | 71/0/6 |
| 192 | 77 | 4.9557 | 4.9549 | +0.0008 | 192.29 | 192.15 | 4.891 | 4.894 | 70/0/7 |
| 256 | 77 | 4.9819 | 4.9814 | +0.0005 | 256.39 | 256.31 | 4.958 | 4.959 | 69/0/8 |
| 320 | 77 | 4.9909 | 4.9907 | +0.0002 | 320.50 | 320.47 | 4.981 | 4.981 | 66/0/11 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0697 | 128 | 08 velvet.wav |
| -0.0479 | 160 | 08 velvet.wav |
| -0.0283 | 128 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0213 | 192 | 08 velvet.wav |
| -0.0119 | 160 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0098 | 256 | 08 velvet.wav |
| -0.0078 | 128 | 24td_.wav |
| -0.0072 | 192 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0063 | 128 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0061 | 160 | 10 Angels_Fall_First_ringing.wav |
| -0.0033 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0030 | 192 | mof.wav |
| -0.0026 | 320 | 08 velvet.wav |
| -0.0021 | 192 | 10 Angels_Fall_First_ringing.wav |
| -0.0018 | 128 | mof.wav |
