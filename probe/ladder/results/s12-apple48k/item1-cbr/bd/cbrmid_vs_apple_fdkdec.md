# cbrmid vs apple  (decoder fdkdec, cbr/lc, rungs [128, 160, 192, 256, 320], metric mos)

BD-rate mean **-1.780%** (95% clip-bootstrap CI [-3.385, -0.228], 10000 resamples, seed 0), median -1.969%; valid curves 77/77, no valid curve: 0
PCHIP sign: 24 clips worse / 53 better. Linear (log-rate linear in MOS): **-1.598%** [-3.210, -0.036], n 77, 26 worse / 51 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 77 | 4.8695 | 4.8466 | +0.0229 | 128.10 | 128.56 | 4.718 | 4.488 | 56/0/21 |
| 160 | 77 | 4.9271 | 4.9192 | +0.0079 | 160.16 | 160.64 | 4.841 | 4.766 | 50/0/27 |
| 192 | 77 | 4.9560 | 4.9580 | -0.0020 | 192.22 | 192.74 | 4.909 | 4.898 | 19/0/58 |
| 256 | 77 | 4.9820 | 4.9795 | +0.0024 | 256.34 | 256.98 | 4.963 | 4.952 | 51/0/26 |
| 320 | 77 | 4.9909 | 4.9919 | -0.0010 | 320.47 | 321.19 | 4.981 | 4.982 | 32/0/45 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0834 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0580 | 128 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0547 | 128 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0535 | 128 | SinceAlways.16b48k.wav |
| -0.0439 | 128 | 18vr_.wav |
| -0.0427 | 128 | fms.wav |
| -0.0400 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0360 | 128 | 14fe_.wav |
| -0.0320 | 160 | SinceAlways.16b48k.wav |
| -0.0304 | 128 | Girl_In_The_Fire__Sample_.16b48k.wav |
| -0.0278 | 160 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0255 | 128 | 10 Angels_Fall_First_ringing.wav |
| -0.0223 | 192 | SinceAlways.16b48k.wav |
| -0.0217 | 160 | 08 velvet.wav |
| -0.0214 | 160 | fms.wav |
