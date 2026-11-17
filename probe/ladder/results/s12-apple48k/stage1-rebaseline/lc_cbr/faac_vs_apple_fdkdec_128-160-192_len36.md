# faac vs apple  (decoder fdkdec, cbr/lc, rungs [128, 160, 192], metric mos)

BD-rate mean **-1.994%** (95% clip-bootstrap CI [-4.398, +0.923], 10000 resamples, seed 0), median -2.965%; valid curves 36/36, no valid curve: 0
PCHIP sign: 12 clips worse / 24 better. Linear (log-rate linear in MOS): **-1.892%** [-4.133, +0.689], n 36, 11 worse / 25 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 36 | 4.8619 | 4.8421 | +0.0198 | 127.95 | 128.58 | 4.680 | 4.596 | 26/0/10 |
| 160 | 36 | 4.9230 | 4.9173 | +0.0057 | 160.05 | 160.65 | 4.828 | 4.800 | 25/0/11 |
| 192 | 36 | 4.9531 | 4.9576 | -0.0045 | 192.14 | 192.73 | 4.891 | 4.913 | 9/0/27 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0812 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0700 | 128 | 10xh_.wav |
| -0.0575 | 128 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0384 | 192 | 08 velvet.wav |
| -0.0365 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0292 | 160 | 08 velvet.wav |
| -0.0233 | 128 | 21-classic.441.16b48k.wav |
| -0.0222 | 128 | 15-Good-evening.441.16b48k.wav |
| -0.0175 | 192 | 10xh_.wav |
| -0.0158 | 128 | bah.wav |
| -0.0142 | 160 | 21-classic.441.16b48k.wav |
| -0.0121 | 192 | 21-classic.441.16b48k.wav |
| -0.0117 | 128 | Severance__1.31-1.51_.16b48k.wav |
| -0.0113 | 160 | 15-Good-evening.441.16b48k.wav |
| -0.0109 | 192 | 07 applaud.wav |
