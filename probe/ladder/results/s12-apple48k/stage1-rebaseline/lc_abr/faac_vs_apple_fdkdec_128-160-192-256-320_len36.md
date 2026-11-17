# faac vs apple  (decoder fdkdec, abr/lc, rungs [128, 160, 192, 256, 320], metric mos)

BD-rate mean **-3.054%** (95% clip-bootstrap CI [-4.936, -0.978], 10000 resamples, seed 0), median -3.311%; valid curves 36/36, no valid curve: 0
PCHIP sign: 8 clips worse / 28 better. Linear (log-rate linear in MOS): **-2.921%** [-4.782, -0.809], n 36, 9 worse / 27 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 36 | 4.8630 | 4.8421 | +0.0209 | 126.09 | 128.58 | 4.685 | 4.596 | 27/0/9 |
| 160 | 36 | 4.9236 | 4.9173 | +0.0063 | 157.47 | 160.65 | 4.829 | 4.800 | 26/0/10 |
| 192 | 36 | 4.9536 | 4.9576 | -0.0040 | 188.94 | 192.73 | 4.900 | 4.913 | 9/0/27 |
| 256 | 36 | 4.9809 | 4.9794 | +0.0015 | 251.73 | 256.95 | 4.962 | 4.955 | 18/0/18 |
| 320 | 36 | 4.9904 | 4.9918 | -0.0014 | 313.59 | 321.19 | 4.981 | 4.983 | 14/0/22 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0739 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0613 | 128 | 10xh_.wav |
| -0.0545 | 128 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0416 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0275 | 160 | 08 velvet.wav |
| -0.0226 | 128 | 21-classic.441.16b48k.wav |
| -0.0211 | 128 | 15-Good-evening.441.16b48k.wav |
| -0.0185 | 192 | 08 velvet.wav |
| -0.0174 | 192 | 10xh_.wav |
| -0.0165 | 128 | bah.wav |
| -0.0144 | 160 | 21-classic.441.16b48k.wav |
| -0.0120 | 192 | 21-classic.441.16b48k.wav |
| -0.0116 | 128 | Severance__1.31-1.51_.16b48k.wav |
| -0.0114 | 160 | 15-Good-evening.441.16b48k.wav |
| -0.0107 | 192 | 07 applaud.wav |
