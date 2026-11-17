# faac vs fdkref  (decoder faad2, abr/lc, rungs [128, 160, 192, 256, 320], metric mos)

BD-rate mean **-19.353%** (95% clip-bootstrap CI [-21.959, -16.765], 10000 resamples, seed 0), median -18.246%; valid curves 76/77, no valid curve: 1

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 77 | 4.8668 | 4.8262 | +0.0406 | 126.36 | 128.61 | 4.708 | 4.633 | 68/0/9 |
| 160 | 77 | 4.9259 | 4.8799 | +0.0460 | 157.85 | 160.67 | 4.837 | 4.720 | 74/0/3 |
| 192 | 77 | 4.9553 | 4.9266 | +0.0287 | 189.41 | 192.74 | 4.905 | 4.828 | 74/0/3 |
| 256 | 77 | 4.9815 | 4.9524 | +0.0291 | 252.38 | 256.90 | 4.963 | 4.886 | 76/0/1 |
| 320 | 77 | 4.9907 | 4.9598 | +0.0309 | 314.28 | 321.11 | 4.981 | 4.896 | 76/0/1 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0701 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0573 | 128 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0344 | 128 | fms.wav |
| -0.0197 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0184 | 128 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0163 | 128 | SinceAlways.16b48k.wav |
| -0.0148 | 128 | TrosYGareg.16b48k.wav |
| -0.0137 | 128 | 07 applaud.wav |
| -0.0126 | 128 | 10xh_.wav |
| -0.0055 | 192 | 21-classic.441.16b48k.wav |
| -0.0049 | 128 | Girl_In_The_Fire__Sample_.16b48k.wav |
| -0.0037 | 160 | fms.wav |
| -0.0030 | 256 | 21-classic.441.16b48k.wav |
| -0.0027 | 128 | 18vr_.wav |
| -0.0023 | 160 | TrosYGareg.16b48k.wav |

No valid curve: girl.16b48k.wav (no valid curve)
