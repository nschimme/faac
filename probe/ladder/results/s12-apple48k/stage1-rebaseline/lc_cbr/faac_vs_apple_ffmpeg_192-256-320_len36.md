# faac vs apple  (decoder ffmpeg, cbr/lc, rungs [192, 256, 320], metric mos)

BD-rate mean **-2.331%** (95% clip-bootstrap CI [-5.379, +0.189], 10000 resamples, seed 0), median +0.060%; valid curves 35/36, no valid curve: 1
PCHIP sign: 18 clips worse / 17 better. Linear (log-rate linear in MOS): **-0.684%** [-3.645, +1.718], n 35, 18 worse / 17 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 192 | 36 | 4.9532 | 4.9576 | -0.0044 | 192.14 | 192.73 | 4.891 | 4.913 | 9/0/27 |
| 256 | 36 | 4.9806 | 4.9794 | +0.0013 | 256.30 | 256.95 | 4.959 | 4.955 | 19/0/17 |
| 320 | 36 | 4.9903 | 4.9918 | -0.0014 | 320.44 | 321.19 | 4.981 | 4.983 | 13/0/23 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0375 | 192 | 08 velvet.wav |
| -0.0354 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0176 | 192 | 10xh_.wav |
| -0.0121 | 192 | 21-classic.441.16b48k.wav |
| -0.0111 | 192 | 07 applaud.wav |
| -0.0084 | 320 | 10xh_.wav |
| -0.0083 | 192 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.0081 | 320 | 03 eig.wav |
| -0.0078 | 192 | 15-Good-evening.441.16b48k.wav |
| -0.0077 | 192 | 15ma_.wav |
| -0.0074 | 192 | 27-last-song-drums-and-trampets.441.16b48k.wav |
| -0.0068 | 192 | 35_SQAM_glockenspiel_cut.16b48k.wav |
| -0.0066 | 256 | 35_SQAM_glockenspiel_cut.16b48k.wav |
| -0.0063 | 256 | 10xh_.wav |
| -0.0059 | 320 | 35_SQAM_glockenspiel_cut.16b48k.wav |

No valid curve: 35_SQAM_glockenspiel_cut.16b48k.wav (no valid curve)
