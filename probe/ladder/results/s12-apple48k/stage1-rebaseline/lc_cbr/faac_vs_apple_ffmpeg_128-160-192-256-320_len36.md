# faac vs apple  (decoder ffmpeg, cbr/lc, rungs [128, 160, 192, 256, 320], metric mos)

BD-rate mean **-1.214%** (95% clip-bootstrap CI [-2.973, +0.743], 10000 resamples, seed 0), median -2.093%; valid curves 36/36, no valid curve: 0
PCHIP sign: 11 clips worse / 25 better. Linear (log-rate linear in MOS): **-1.060%** [-2.862, +0.933], n 36, 11 worse / 25 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 36 | 4.8620 | 4.8421 | +0.0199 | 127.95 | 128.58 | 4.681 | 4.596 | 26/0/10 |
| 160 | 36 | 4.9231 | 4.9173 | +0.0058 | 160.05 | 160.65 | 4.828 | 4.800 | 25/0/11 |
| 192 | 36 | 4.9532 | 4.9576 | -0.0044 | 192.14 | 192.73 | 4.891 | 4.913 | 9/0/27 |
| 256 | 36 | 4.9806 | 4.9794 | +0.0013 | 256.30 | 256.95 | 4.959 | 4.955 | 19/0/17 |
| 320 | 36 | 4.9903 | 4.9918 | -0.0014 | 320.44 | 321.19 | 4.981 | 4.983 | 13/0/23 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0785 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0727 | 128 | 10xh_.wav |
| -0.0573 | 128 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0375 | 192 | 08 velvet.wav |
| -0.0354 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0287 | 160 | 08 velvet.wav |
| -0.0226 | 128 | 15-Good-evening.441.16b48k.wav |
| -0.0219 | 128 | 21-classic.441.16b48k.wav |
| -0.0176 | 192 | 10xh_.wav |
| -0.0175 | 128 | bah.wav |
| -0.0141 | 160 | 21-classic.441.16b48k.wav |
| -0.0121 | 128 | Severance__1.31-1.51_.16b48k.wav |
| -0.0121 | 192 | 21-classic.441.16b48k.wav |
| -0.0113 | 160 | 15-Good-evening.441.16b48k.wav |
| -0.0111 | 192 | 07 applaud.wav |
