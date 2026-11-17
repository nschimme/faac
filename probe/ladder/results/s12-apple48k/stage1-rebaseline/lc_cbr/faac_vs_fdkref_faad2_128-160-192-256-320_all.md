# faac vs fdkref  (decoder faad2, cbr/lc, rungs [128, 160, 192, 256, 320], metric mos)

BD-rate mean **-18.675%** (95% clip-bootstrap CI [-21.534, -15.916], 10000 resamples, seed 0), median -17.435%; valid curves 77/77, no valid curve: 0
PCHIP sign: 2 clips worse / 75 better. Linear (log-rate linear in MOS): **-18.740%** [-21.660, -15.909], n 77, 2 worse / 75 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 77 | 4.8663 | 4.8262 | +0.0401 | 127.95 | 128.61 | 4.707 | 4.633 | 68/0/9 |
| 160 | 77 | 4.9258 | 4.8799 | +0.0459 | 160.05 | 160.67 | 4.835 | 4.720 | 75/0/2 |
| 192 | 77 | 4.9550 | 4.9266 | +0.0284 | 192.15 | 192.74 | 4.895 | 4.828 | 73/0/4 |
| 256 | 77 | 4.9814 | 4.9524 | +0.0290 | 256.31 | 256.90 | 4.959 | 4.886 | 76/0/1 |
| 320 | 77 | 4.9907 | 4.9598 | +0.0309 | 320.47 | 321.11 | 4.981 | 4.896 | 76/0/1 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0696 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0611 | 128 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0272 | 128 | fms.wav |
| -0.0180 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0178 | 128 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0171 | 128 | SinceAlways.16b48k.wav |
| -0.0153 | 128 | 07 applaud.wav |
| -0.0145 | 128 | TrosYGareg.16b48k.wav |
| -0.0128 | 128 | 10xh_.wav |
| -0.0057 | 128 | Girl_In_The_Fire__Sample_.16b48k.wav |
| -0.0056 | 192 | 21-classic.441.16b48k.wav |
| -0.0044 | 128 | 18vr_.wav |
| -0.0030 | 256 | 21-classic.441.16b48k.wav |
| -0.0023 | 160 | TrosYGareg.16b48k.wav |
| -0.0021 | 192 | TrosYGareg.16b48k.wav |
