# faac vs fdkref  (decoder ffmpeg, abr/lc, rungs [128, 160, 192], metric mos)

BD-rate mean **-16.159%** (95% clip-bootstrap CI [-18.559, -13.610], 10000 resamples, seed 0), median -16.293%; valid curves 73/77, no valid curve: 4
PCHIP sign: 3 clips worse / 70 better. Linear (log-rate linear in MOS): **-15.214%** [-17.490, -12.805], n 73, 3 worse / 70 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 77 | 4.8666 | 4.8262 | +0.0404 | 126.36 | 128.61 | 4.707 | 4.633 | 68/0/9 |
| 160 | 77 | 4.9258 | 4.8799 | +0.0459 | 157.85 | 160.67 | 4.838 | 4.720 | 74/0/3 |
| 192 | 77 | 4.9552 | 4.9266 | +0.0286 | 189.41 | 192.74 | 4.905 | 4.828 | 74/0/3 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0668 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0578 | 128 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0298 | 128 | fms.wav |
| -0.0221 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0201 | 128 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0163 | 128 | SinceAlways.16b48k.wav |
| -0.0155 | 128 | TrosYGareg.16b48k.wav |
| -0.0149 | 128 | 07 applaud.wav |
| -0.0129 | 128 | 10xh_.wav |
| -0.0055 | 192 | 21-classic.441.16b48k.wav |
| -0.0051 | 160 | fms.wav |
| -0.0041 | 128 | Girl_In_The_Fire__Sample_.16b48k.wav |
| -0.0023 | 160 | TrosYGareg.16b48k.wav |
| -0.0021 | 192 | TrosYGareg.16b48k.wav |
| -0.0019 | 128 | 18vr_.wav |

No valid curve: 24td_.wav (no valid curve), Robots_old.16b48k.wav (no valid curve), girl.16b48k.wav (no valid curve), lob.wav (no valid curve)
