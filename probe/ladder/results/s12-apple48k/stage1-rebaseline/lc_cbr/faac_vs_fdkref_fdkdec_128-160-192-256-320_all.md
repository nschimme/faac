# faac vs fdkref  (decoder fdkdec, cbr/lc, rungs [128, 160, 192, 256, 320], metric mos)

BD-rate mean **-18.583%** (95% clip-bootstrap CI [-21.438, -15.820], 10000 resamples, seed 0), median -17.568%; valid curves 77/77, no valid curve: 0
PCHIP sign: 2 clips worse / 75 better. Linear (log-rate linear in MOS): **-18.647%** [-21.555, -15.816], n 77, 2 worse / 75 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 77 | 4.8658 | 4.8262 | +0.0396 | 127.95 | 128.61 | 4.706 | 4.633 | 68/0/9 |
| 160 | 77 | 4.9255 | 4.8799 | +0.0456 | 160.05 | 160.67 | 4.834 | 4.720 | 75/0/2 |
| 192 | 77 | 4.9549 | 4.9266 | +0.0283 | 192.15 | 192.74 | 4.894 | 4.828 | 73/0/4 |
| 256 | 77 | 4.9814 | 4.9524 | +0.0290 | 256.31 | 256.90 | 4.959 | 4.886 | 76/0/1 |
| 320 | 77 | 4.9907 | 4.9598 | +0.0309 | 320.47 | 321.11 | 4.981 | 4.896 | 76/0/1 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0716 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0614 | 128 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0313 | 128 | fms.wav |
| -0.0184 | 128 | 07 applaud.wav |
| -0.0183 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0179 | 128 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0174 | 128 | SinceAlways.16b48k.wav |
| -0.0152 | 128 | TrosYGareg.16b48k.wav |
| -0.0140 | 128 | 10xh_.wav |
| -0.0071 | 128 | Girl_In_The_Fire__Sample_.16b48k.wav |
| -0.0071 | 128 | 18vr_.wav |
| -0.0056 | 192 | 21-classic.441.16b48k.wav |
| -0.0030 | 256 | 21-classic.441.16b48k.wav |
| -0.0023 | 160 | TrosYGareg.16b48k.wav |
| -0.0021 | 192 | TrosYGareg.16b48k.wav |
