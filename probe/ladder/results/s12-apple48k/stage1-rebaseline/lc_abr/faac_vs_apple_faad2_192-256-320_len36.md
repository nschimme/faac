# faac vs apple  (decoder faad2, abr/lc, rungs [192, 256, 320], metric mos)

BD-rate mean **-3.645%** (95% clip-bootstrap CI [-6.812, -1.077], 10000 resamples, seed 0), median -3.621%; valid curves 35/36, no valid curve: 1
PCHIP sign: 12 clips worse / 23 better. Linear (log-rate linear in MOS): **-2.055%** [-5.248, +0.439], n 35, 16 worse / 19 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 192 | 36 | 4.9538 | 4.9576 | -0.0038 | 188.94 | 192.73 | 4.900 | 4.913 | 9/0/27 |
| 256 | 36 | 4.9809 | 4.9794 | +0.0015 | 251.73 | 256.95 | 4.962 | 4.955 | 18/0/18 |
| 320 | 36 | 4.9904 | 4.9918 | -0.0014 | 313.59 | 321.19 | 4.981 | 4.983 | 14/0/22 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0379 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0182 | 192 | 08 velvet.wav |
| -0.0174 | 192 | 10xh_.wav |
| -0.0120 | 192 | 21-classic.441.16b48k.wav |
| -0.0109 | 192 | 07 applaud.wav |
| -0.0085 | 320 | 10xh_.wav |
| -0.0084 | 320 | 03 eig.wav |
| -0.0078 | 192 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.0077 | 192 | 15-Good-evening.441.16b48k.wav |
| -0.0074 | 192 | 27-last-song-drums-and-trampets.441.16b48k.wav |
| -0.0073 | 192 | 15ma_.wav |
| -0.0060 | 256 | 10xh_.wav |
| -0.0058 | 320 | 35_SQAM_glockenspiel_cut.16b48k.wav |
| -0.0057 | 320 | Robots_old.16b48k.wav |
| -0.0057 | 256 | 35_SQAM_glockenspiel_cut.16b48k.wav |

No valid curve: 35_SQAM_glockenspiel_cut.16b48k.wav (no valid curve)
