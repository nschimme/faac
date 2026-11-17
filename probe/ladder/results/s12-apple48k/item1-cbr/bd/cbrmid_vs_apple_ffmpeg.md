# cbrmid vs apple  (decoder ffmpeg, cbr/lc, rungs [128, 160, 192, 256, 320], metric mos)

BD-rate mean **-1.808%** (95% clip-bootstrap CI [-3.403, -0.265], 10000 resamples, seed 0), median -2.001%; valid curves 77/77, no valid curve: 0
PCHIP sign: 24 clips worse / 53 better. Linear (log-rate linear in MOS): **-1.628%** [-3.237, -0.072], n 77, 25 worse / 52 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 77 | 4.8696 | 4.8466 | +0.0230 | 128.10 | 128.56 | 4.717 | 4.488 | 55/0/22 |
| 160 | 77 | 4.9271 | 4.9192 | +0.0079 | 160.16 | 160.64 | 4.842 | 4.766 | 50/0/27 |
| 192 | 77 | 4.9560 | 4.9580 | -0.0020 | 192.22 | 192.74 | 4.909 | 4.898 | 19/0/58 |
| 256 | 77 | 4.9820 | 4.9795 | +0.0024 | 256.34 | 256.98 | 4.963 | 4.952 | 51/0/26 |
| 320 | 77 | 4.9909 | 4.9919 | -0.0010 | 320.47 | 321.19 | 4.981 | 4.982 | 32/0/45 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0843 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0563 | 128 | SinceAlways.16b48k.wav |
| -0.0531 | 128 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0519 | 128 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0469 | 128 | fms.wav |
| -0.0451 | 128 | 18vr_.wav |
| -0.0402 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0392 | 128 | 14fe_.wav |
| -0.0317 | 128 | Girl_In_The_Fire__Sample_.16b48k.wav |
| -0.0310 | 160 | SinceAlways.16b48k.wav |
| -0.0278 | 160 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0261 | 128 | 10 Angels_Fall_First_ringing.wav |
| -0.0214 | 192 | SinceAlways.16b48k.wav |
| -0.0213 | 160 | 12at_.wav |
| -0.0212 | 160 | fms.wav |
