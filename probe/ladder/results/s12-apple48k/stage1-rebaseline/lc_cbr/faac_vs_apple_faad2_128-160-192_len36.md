# faac vs apple  (decoder faad2, cbr/lc, rungs [128, 160, 192], metric mos)

BD-rate mean **-2.063%** (95% clip-bootstrap CI [-4.456, +0.844], 10000 resamples, seed 0), median -2.917%; valid curves 36/36, no valid curve: 0
PCHIP sign: 12 clips worse / 24 better. Linear (log-rate linear in MOS): **-1.958%** [-4.202, +0.615], n 36, 11 worse / 25 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 36 | 4.8624 | 4.8421 | +0.0203 | 127.95 | 128.58 | 4.682 | 4.596 | 26/0/10 |
| 160 | 36 | 4.9231 | 4.9173 | +0.0059 | 160.05 | 160.65 | 4.828 | 4.800 | 25/0/11 |
| 192 | 36 | 4.9532 | 4.9576 | -0.0044 | 192.14 | 192.73 | 4.892 | 4.913 | 9/0/27 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0792 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0689 | 128 | 10xh_.wav |
| -0.0572 | 128 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0371 | 192 | 08 velvet.wav |
| -0.0362 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0287 | 160 | 08 velvet.wav |
| -0.0217 | 128 | 21-classic.441.16b48k.wav |
| -0.0212 | 128 | 15-Good-evening.441.16b48k.wav |
| -0.0175 | 192 | 10xh_.wav |
| -0.0153 | 128 | bah.wav |
| -0.0142 | 160 | 21-classic.441.16b48k.wav |
| -0.0121 | 192 | 21-classic.441.16b48k.wav |
| -0.0118 | 128 | Severance__1.31-1.51_.16b48k.wav |
| -0.0109 | 192 | 07 applaud.wav |
| -0.0108 | 160 | 15-Good-evening.441.16b48k.wav |
