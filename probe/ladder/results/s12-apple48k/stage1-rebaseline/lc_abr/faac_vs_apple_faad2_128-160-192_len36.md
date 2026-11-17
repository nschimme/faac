# faac vs apple  (decoder faad2, abr/lc, rungs [128, 160, 192], metric mos)

BD-rate mean **-3.766%** (95% clip-bootstrap CI [-6.325, -0.799], 10000 resamples, seed 0), median -4.906%; valid curves 36/36, no valid curve: 0
PCHIP sign: 8 clips worse / 28 better. Linear (log-rate linear in MOS): **-3.686%** [-6.031, -1.033], n 36, 8 worse / 28 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 36 | 4.8632 | 4.8421 | +0.0211 | 126.09 | 128.58 | 4.683 | 4.596 | 27/0/9 |
| 160 | 36 | 4.9235 | 4.9173 | +0.0062 | 157.47 | 160.65 | 4.830 | 4.800 | 26/0/10 |
| 192 | 36 | 4.9538 | 4.9576 | -0.0038 | 188.94 | 192.73 | 4.900 | 4.913 | 9/0/27 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0797 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0686 | 128 | 10xh_.wav |
| -0.0534 | 128 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0379 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0260 | 160 | 08 velvet.wav |
| -0.0214 | 128 | 21-classic.441.16b48k.wav |
| -0.0201 | 128 | 15-Good-evening.441.16b48k.wav |
| -0.0182 | 192 | 08 velvet.wav |
| -0.0174 | 192 | 10xh_.wav |
| -0.0156 | 128 | bah.wav |
| -0.0144 | 160 | 21-classic.441.16b48k.wav |
| -0.0120 | 192 | 21-classic.441.16b48k.wav |
| -0.0118 | 128 | Severance__1.31-1.51_.16b48k.wav |
| -0.0110 | 160 | 15-Good-evening.441.16b48k.wav |
| -0.0109 | 192 | 07 applaud.wav |
