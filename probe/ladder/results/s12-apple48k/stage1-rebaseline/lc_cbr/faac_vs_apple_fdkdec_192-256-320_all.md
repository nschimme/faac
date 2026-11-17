# faac vs apple  (decoder fdkdec, cbr/lc, rungs [192, 256, 320], metric mos)

BD-rate mean **-1.537%** (95% clip-bootstrap CI [-3.594, +0.432], 10000 resamples, seed 0), median -0.931%; valid curves 76/77, no valid curve: 1
PCHIP sign: 36 clips worse / 40 better. Linear (log-rate linear in MOS): **+0.155%** [-1.790, +2.018], n 76, 40 worse / 36 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 192 | 77 | 4.9549 | 4.9580 | -0.0031 | 192.15 | 192.74 | 4.894 | 4.898 | 15/0/62 |
| 256 | 77 | 4.9814 | 4.9795 | +0.0019 | 256.31 | 256.98 | 4.959 | 4.952 | 44/0/33 |
| 320 | 77 | 4.9907 | 4.9919 | -0.0012 | 320.47 | 321.19 | 4.981 | 4.982 | 31/0/46 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0384 | 192 | 08 velvet.wav |
| -0.0365 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0240 | 192 | SinceAlways.16b48k.wav |
| -0.0207 | 192 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0175 | 192 | 10xh_.wav |
| -0.0171 | 192 | 14fe_.wav |
| -0.0170 | 192 | 11 You look good to me.wav |
| -0.0135 | 192 | 12at_.wav |
| -0.0121 | 192 | 21-classic.441.16b48k.wav |
| -0.0112 | 192 | 04 Bachpsichord.wav |
| -0.0109 | 192 | 07 applaud.wav |
| -0.0086 | 192 | Last_Of_The_Mohicanz__Sample_.16b48k.wav |
| -0.0084 | 320 | 10xh_.wav |
| -0.0082 | 192 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.0081 | 320 | 03 eig.wav |

No valid curve: 35_SQAM_glockenspiel_cut.16b48k.wav (no valid curve)
