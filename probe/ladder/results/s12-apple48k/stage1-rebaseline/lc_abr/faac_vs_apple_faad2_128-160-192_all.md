# faac vs apple  (decoder faad2, abr/lc, rungs [128, 160, 192], metric mos)

BD-rate mean **-2.789%** (95% clip-bootstrap CI [-4.594, -0.855], 10000 resamples, seed 0), median -3.299%; valid curves 76/77, no valid curve: 1
PCHIP sign: 23 clips worse / 53 better. Linear (log-rate linear in MOS): **-2.614%** [-4.303, -0.819], n 76, 23 worse / 53 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 77 | 4.8668 | 4.8466 | +0.0202 | 126.36 | 128.56 | 4.708 | 4.488 | 51/0/26 |
| 160 | 77 | 4.9259 | 4.9192 | +0.0067 | 157.85 | 160.64 | 4.837 | 4.766 | 48/0/29 |
| 192 | 77 | 4.9553 | 4.9580 | -0.0027 | 189.41 | 192.74 | 4.905 | 4.898 | 17/0/60 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0797 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0686 | 128 | 10xh_.wav |
| -0.0582 | 128 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0566 | 128 | SinceAlways.16b48k.wav |
| -0.0534 | 128 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0492 | 128 | fms.wav |
| -0.0447 | 128 | 18vr_.wav |
| -0.0394 | 128 | 14fe_.wav |
| -0.0379 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0345 | 128 | Girl_In_The_Fire__Sample_.16b48k.wav |
| -0.0331 | 160 | SinceAlways.16b48k.wav |
| -0.0301 | 160 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0291 | 128 | 10 Angels_Fall_First_ringing.wav |
| -0.0260 | 160 | 08 velvet.wav |
| -0.0234 | 192 | SinceAlways.16b48k.wav |

No valid curve: girl.16b48k.wav (no valid curve)
