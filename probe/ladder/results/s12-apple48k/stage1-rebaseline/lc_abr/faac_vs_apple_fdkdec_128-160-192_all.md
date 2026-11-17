# faac vs apple  (decoder fdkdec, abr/lc, rungs [128, 160, 192], metric mos)

BD-rate mean **-2.723%** (95% clip-bootstrap CI [-4.533, -0.798], 10000 resamples, seed 0), median -3.313%; valid curves 76/77, no valid curve: 1
PCHIP sign: 24 clips worse / 52 better. Linear (log-rate linear in MOS): **-2.542%** [-4.241, -0.740], n 76, 22 worse / 54 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 77 | 4.8667 | 4.8466 | +0.0201 | 126.36 | 128.56 | 4.713 | 4.488 | 52/0/25 |
| 160 | 77 | 4.9259 | 4.9192 | +0.0066 | 157.85 | 160.64 | 4.836 | 4.766 | 48/0/29 |
| 192 | 77 | 4.9552 | 4.9580 | -0.0028 | 189.41 | 192.74 | 4.905 | 4.898 | 17/0/60 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0739 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0613 | 128 | 10xh_.wav |
| -0.0579 | 128 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0563 | 128 | SinceAlways.16b48k.wav |
| -0.0545 | 128 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0458 | 128 | 18vr_.wav |
| -0.0416 | 128 | fms.wav |
| -0.0416 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0377 | 128 | Girl_In_The_Fire__Sample_.16b48k.wav |
| -0.0370 | 128 | 14fe_.wav |
| -0.0333 | 160 | SinceAlways.16b48k.wav |
| -0.0308 | 160 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0288 | 128 | 10 Angels_Fall_First_ringing.wav |
| -0.0275 | 160 | 08 velvet.wav |
| -0.0236 | 192 | SinceAlways.16b48k.wav |

No valid curve: girl.16b48k.wav (no valid curve)
