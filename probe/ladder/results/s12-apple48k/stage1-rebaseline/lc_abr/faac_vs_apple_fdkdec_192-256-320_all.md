# faac vs apple  (decoder fdkdec, abr/lc, rungs [192, 256, 320], metric mos)

BD-rate mean **-2.769%** (95% clip-bootstrap CI [-4.866, -0.757], 10000 resamples, seed 0), median -3.206%; valid curves 76/77, no valid curve: 1

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 192 | 77 | 4.9552 | 4.9580 | -0.0028 | 189.41 | 192.74 | 4.905 | 4.898 | 17/0/60 |
| 256 | 77 | 4.9815 | 4.9795 | +0.0020 | 252.38 | 256.98 | 4.963 | 4.952 | 43/0/34 |
| 320 | 77 | 4.9907 | 4.9919 | -0.0012 | 314.28 | 321.19 | 4.981 | 4.982 | 33/0/44 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0416 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0236 | 192 | SinceAlways.16b48k.wav |
| -0.0185 | 192 | 08 velvet.wav |
| -0.0174 | 192 | 10xh_.wav |
| -0.0171 | 192 | 14fe_.wav |
| -0.0170 | 192 | 11 You look good to me.wav |
| -0.0153 | 192 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0135 | 192 | 12at_.wav |
| -0.0120 | 192 | 21-classic.441.16b48k.wav |
| -0.0113 | 192 | 04 Bachpsichord.wav |
| -0.0107 | 192 | 07 applaud.wav |
| -0.0096 | 192 | fms.wav |
| -0.0088 | 192 | Last_Of_The_Mohicanz__Sample_.16b48k.wav |
| -0.0085 | 320 | 10xh_.wav |
| -0.0084 | 320 | 03 eig.wav |

No valid curve: 35_SQAM_glockenspiel_cut.16b48k.wav (no valid curve)
