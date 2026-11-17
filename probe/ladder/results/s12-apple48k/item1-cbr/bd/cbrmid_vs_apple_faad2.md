# cbrmid vs apple  (decoder faad2, cbr/lc, rungs [128, 160, 192, 256, 320], metric mos)

BD-rate mean **-1.815%** (95% clip-bootstrap CI [-3.417, -0.268], 10000 resamples, seed 0), median -2.036%; valid curves 77/77, no valid curve: 0
PCHIP sign: 23 clips worse / 54 better. Linear (log-rate linear in MOS): **-1.635%** [-3.248, -0.078], n 77, 27 worse / 50 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 77 | 4.8698 | 4.8466 | +0.0232 | 128.10 | 128.56 | 4.718 | 4.488 | 56/0/21 |
| 160 | 77 | 4.9271 | 4.9192 | +0.0079 | 160.16 | 160.64 | 4.843 | 4.766 | 50/0/27 |
| 192 | 77 | 4.9560 | 4.9580 | -0.0019 | 192.22 | 192.74 | 4.909 | 4.898 | 19/0/58 |
| 256 | 77 | 4.9820 | 4.9795 | +0.0024 | 256.34 | 256.98 | 4.963 | 4.952 | 51/0/26 |
| 320 | 77 | 4.9909 | 4.9919 | -0.0010 | 320.47 | 321.19 | 4.981 | 4.982 | 32/0/45 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0853 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0535 | 128 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0530 | 128 | SinceAlways.16b48k.wav |
| -0.0506 | 128 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0467 | 128 | 18vr_.wav |
| -0.0412 | 128 | fms.wav |
| -0.0392 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0361 | 128 | 14fe_.wav |
| -0.0319 | 160 | SinceAlways.16b48k.wav |
| -0.0296 | 128 | Girl_In_The_Fire__Sample_.16b48k.wav |
| -0.0279 | 160 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0264 | 128 | 10 Angels_Fall_First_ringing.wav |
| -0.0223 | 192 | SinceAlways.16b48k.wav |
| -0.0221 | 160 | fms.wav |
| -0.0216 | 160 | 12at_.wav |
