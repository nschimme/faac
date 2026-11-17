# faac vs fdkref  (decoder fdkdec, cbr/lc, rungs [128, 160, 192, 256, 320], metric mos)

BD-rate mean **-19.058%** (95% clip-bootstrap CI [-22.866, -15.430], 10000 resamples, seed 0), median -19.444%; valid curves 36/36, no valid curve: 0
PCHIP sign: 1 clips worse / 35 better. Linear (log-rate linear in MOS): **-19.098%** [-22.999, -15.380], n 36, 1 worse / 35 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 36 | 4.8619 | 4.8197 | +0.0423 | 127.95 | 128.60 | 4.680 | 4.626 | 33/0/3 |
| 160 | 36 | 4.9230 | 4.8741 | +0.0488 | 160.05 | 160.65 | 4.828 | 4.717 | 35/0/1 |
| 192 | 36 | 4.9531 | 4.9231 | +0.0300 | 192.14 | 192.72 | 4.891 | 4.829 | 34/0/2 |
| 256 | 36 | 4.9806 | 4.9498 | +0.0309 | 256.30 | 256.87 | 4.959 | 4.890 | 35/0/1 |
| 320 | 36 | 4.9903 | 4.9581 | +0.0322 | 320.44 | 321.07 | 4.981 | 4.902 | 35/0/1 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0716 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0614 | 128 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0184 | 128 | 07 applaud.wav |
| -0.0183 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0140 | 128 | 10xh_.wav |
| -0.0056 | 192 | 21-classic.441.16b48k.wav |
| -0.0030 | 256 | 21-classic.441.16b48k.wav |
| -0.0004 | 320 | 21-classic.441.16b48k.wav |
| +0.0020 | 160 | glk.wav |
| +0.0023 | 192 | glk.wav |
| +0.0035 | 192 | Severance__1.31-1.51_.16b48k.wav |
| +0.0035 | 256 | glk.wav |
| +0.0036 | 192 | bas.wav |
| +0.0037 | 256 | Severance__1.31-1.51_.16b48k.wav |
| +0.0039 | 192 | bah.wav |
