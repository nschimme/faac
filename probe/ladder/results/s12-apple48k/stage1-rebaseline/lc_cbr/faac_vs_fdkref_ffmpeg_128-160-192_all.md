# faac vs fdkref  (decoder ffmpeg, cbr/lc, rungs [128, 160, 192], metric mos)

BD-rate mean **-14.986%** (95% clip-bootstrap CI [-17.313, -12.503], 10000 resamples, seed 0), median -15.382%; valid curves 73/77, no valid curve: 4
PCHIP sign: 2 clips worse / 71 better. Linear (log-rate linear in MOS): **-13.989%** [-16.205, -11.651], n 73, 3 worse / 70 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 77 | 4.8660 | 4.8262 | +0.0397 | 127.95 | 128.61 | 4.704 | 4.633 | 68/0/9 |
| 160 | 77 | 4.9257 | 4.8799 | +0.0458 | 160.05 | 160.67 | 4.835 | 4.720 | 75/0/2 |
| 192 | 77 | 4.9549 | 4.9266 | +0.0283 | 192.15 | 192.74 | 4.894 | 4.828 | 73/0/4 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0688 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0613 | 128 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0290 | 128 | fms.wav |
| -0.0172 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0170 | 128 | SinceAlways.16b48k.wav |
| -0.0166 | 128 | 10xh_.wav |
| -0.0161 | 128 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0158 | 128 | 07 applaud.wav |
| -0.0145 | 128 | TrosYGareg.16b48k.wav |
| -0.0056 | 192 | 21-classic.441.16b48k.wav |
| -0.0051 | 128 | Girl_In_The_Fire__Sample_.16b48k.wav |
| -0.0045 | 128 | 18vr_.wav |
| -0.0022 | 160 | TrosYGareg.16b48k.wav |
| -0.0021 | 192 | TrosYGareg.16b48k.wav |
| -0.0003 | 192 | take_your_finger_frin_my_head.16b48k.wav |

No valid curve: 24td_.wav (no valid curve), Robots_old.16b48k.wav (no valid curve), girl.16b48k.wav (no valid curve), lob.wav (no valid curve)
