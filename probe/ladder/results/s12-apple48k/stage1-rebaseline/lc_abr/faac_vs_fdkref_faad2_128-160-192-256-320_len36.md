# faac vs fdkref  (decoder faad2, abr/lc, rungs [128, 160, 192, 256, 320], metric mos)

BD-rate mean **-20.608%** (95% clip-bootstrap CI [-24.547, -16.857], 10000 resamples, seed 0), median -20.455%; valid curves 36/36, no valid curve: 0
PCHIP sign: 1 clips worse / 35 better. Linear (log-rate linear in MOS): **-20.662%** [-24.670, -16.860], n 36, 1 worse / 35 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 36 | 4.8632 | 4.8197 | +0.0435 | 126.09 | 128.60 | 4.683 | 4.626 | 33/0/3 |
| 160 | 36 | 4.9235 | 4.8741 | +0.0494 | 157.47 | 160.65 | 4.830 | 4.717 | 35/0/1 |
| 192 | 36 | 4.9538 | 4.9231 | +0.0307 | 188.94 | 192.72 | 4.900 | 4.829 | 34/0/2 |
| 256 | 36 | 4.9809 | 4.9498 | +0.0311 | 251.73 | 256.87 | 4.962 | 4.890 | 35/0/1 |
| 320 | 36 | 4.9904 | 4.9581 | +0.0323 | 313.59 | 321.07 | 4.981 | 4.902 | 35/0/1 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0701 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0573 | 128 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0197 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0137 | 128 | 07 applaud.wav |
| -0.0126 | 128 | 10xh_.wav |
| -0.0055 | 192 | 21-classic.441.16b48k.wav |
| -0.0030 | 256 | 21-classic.441.16b48k.wav |
| -0.0003 | 320 | 21-classic.441.16b48k.wav |
| +0.0019 | 160 | glk.wav |
| +0.0023 | 192 | glk.wav |
| +0.0034 | 192 | Severance__1.31-1.51_.16b48k.wav |
| +0.0035 | 256 | glk.wav |
| +0.0037 | 256 | Severance__1.31-1.51_.16b48k.wav |
| +0.0038 | 192 | bah.wav |
| +0.0039 | 192 | bas.wav |
