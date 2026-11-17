# faac vs apple  (decoder ffmpeg, abr/lc, rungs [128, 160, 192], metric mos)

BD-rate mean **-3.739%** (95% clip-bootstrap CI [-6.306, -0.753], 10000 resamples, seed 0), median -4.837%; valid curves 36/36, no valid curve: 0

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 36 | 4.8629 | 4.8421 | +0.0208 | 126.09 | 128.58 | 4.682 | 4.596 | 27/0/9 |
| 160 | 36 | 4.9236 | 4.9173 | +0.0063 | 157.47 | 160.65 | 4.831 | 4.800 | 26/0/10 |
| 192 | 36 | 4.9537 | 4.9576 | -0.0039 | 188.94 | 192.73 | 4.900 | 4.913 | 9/0/27 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0765 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0690 | 128 | 10xh_.wav |
| -0.0539 | 128 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0403 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0244 | 160 | 08 velvet.wav |
| -0.0223 | 128 | 21-classic.441.16b48k.wav |
| -0.0220 | 128 | 15-Good-evening.441.16b48k.wav |
| -0.0186 | 192 | 08 velvet.wav |
| -0.0174 | 192 | 10xh_.wav |
| -0.0171 | 128 | bah.wav |
| -0.0145 | 160 | 21-classic.441.16b48k.wav |
| -0.0119 | 192 | 21-classic.441.16b48k.wav |
| -0.0118 | 128 | Severance__1.31-1.51_.16b48k.wav |
| -0.0115 | 160 | 15-Good-evening.441.16b48k.wav |
| -0.0106 | 192 | 07 applaud.wav |
