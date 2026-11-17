# faac vs fdkref  (decoder faad2, cbr/lc, rungs [128, 160, 192], metric mos)

BD-rate mean **-15.377%** (95% clip-bootstrap CI [-18.699, -11.274], 10000 resamples, seed 0), median -16.548%; valid curves 34/36, no valid curve: 2
PCHIP sign: 1 clips worse / 33 better. Linear (log-rate linear in MOS): **-14.376%** [-17.420, -10.739], n 34, 1 worse / 33 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 36 | 4.8624 | 4.8197 | +0.0428 | 127.95 | 128.60 | 4.682 | 4.626 | 33/0/3 |
| 160 | 36 | 4.9231 | 4.8741 | +0.0490 | 160.05 | 160.65 | 4.828 | 4.717 | 35/0/1 |
| 192 | 36 | 4.9532 | 4.9231 | +0.0301 | 192.14 | 192.72 | 4.892 | 4.829 | 34/0/2 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0696 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0611 | 128 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0180 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0153 | 128 | 07 applaud.wav |
| -0.0128 | 128 | 10xh_.wav |
| -0.0056 | 192 | 21-classic.441.16b48k.wav |
| +0.0020 | 160 | glk.wav |
| +0.0022 | 192 | glk.wav |
| +0.0035 | 192 | Severance__1.31-1.51_.16b48k.wav |
| +0.0039 | 192 | bas.wav |
| +0.0039 | 192 | bah.wav |
| +0.0043 | 160 | 21-classic.441.16b48k.wav |
| +0.0058 | 128 | glk.wav |
| +0.0059 | 160 | bas.wav |
| +0.0075 | 128 | 35_SQAM_glockenspiel_cut.16b48k.wav |

No valid curve: 24td_.wav (no valid curve), Robots_old.16b48k.wav (no valid curve)
