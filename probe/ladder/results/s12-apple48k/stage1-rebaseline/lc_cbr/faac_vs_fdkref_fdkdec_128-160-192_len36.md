# faac vs fdkref  (decoder fdkdec, cbr/lc, rungs [128, 160, 192], metric mos)

BD-rate mean **-15.309%** (95% clip-bootstrap CI [-18.640, -11.185], 10000 resamples, seed 0), median -16.539%; valid curves 34/36, no valid curve: 2
PCHIP sign: 1 clips worse / 33 better. Linear (log-rate linear in MOS): **-14.298%** [-17.347, -10.645], n 34, 1 worse / 33 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 36 | 4.8619 | 4.8197 | +0.0423 | 127.95 | 128.60 | 4.680 | 4.626 | 33/0/3 |
| 160 | 36 | 4.9230 | 4.8741 | +0.0488 | 160.05 | 160.65 | 4.828 | 4.717 | 35/0/1 |
| 192 | 36 | 4.9531 | 4.9231 | +0.0300 | 192.14 | 192.72 | 4.891 | 4.829 | 34/0/2 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0716 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0614 | 128 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0184 | 128 | 07 applaud.wav |
| -0.0183 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0140 | 128 | 10xh_.wav |
| -0.0056 | 192 | 21-classic.441.16b48k.wav |
| +0.0020 | 160 | glk.wav |
| +0.0023 | 192 | glk.wav |
| +0.0035 | 192 | Severance__1.31-1.51_.16b48k.wav |
| +0.0036 | 192 | bas.wav |
| +0.0039 | 192 | bah.wav |
| +0.0044 | 160 | 21-classic.441.16b48k.wav |
| +0.0054 | 160 | bas.wav |
| +0.0060 | 128 | glk.wav |
| +0.0073 | 128 | bas.wav |

No valid curve: 24td_.wav (no valid curve), Robots_old.16b48k.wav (no valid curve)
