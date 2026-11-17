# faac vs apple  (decoder faad2, cbr/he, rungs [32, 40, 48, 64], metric mos)

BD-rate mean **-8.370%** (95% clip-bootstrap CI [-11.451, -5.385], 10000 resamples, seed 0), median -7.848%; valid curves 75/77, no valid curve: 2
PCHIP sign: 19 clips worse / 56 better. Linear (log-rate linear in MOS): **-7.803%** [-10.767, -4.963], n 75, 20 worse / 55 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 32 | 77 | 3.8431 | 3.7958 | +0.0473 | 31.88 | 32.48 | 3.132 | 2.676 | 35/0/42 |
| 40 | 77 | 4.1673 | 4.0403 | +0.1270 | 39.90 | 40.46 | 3.375 | 2.871 | 51/0/26 |
| 48 | 77 | 4.3485 | 4.2146 | +0.1339 | 47.93 | 48.52 | 3.718 | 3.369 | 55/0/22 |
| 64 | 77 | 4.5695 | 4.4619 | +0.1076 | 63.96 | 64.61 | 4.143 | 3.492 | 58/0/19 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.5446 | 32 | Severance__1.31-1.51_.16b48k.wav |
| -0.5104 | 32 | Girl_In_The_Fire__Sample_.16b48k.wav |
| -0.4604 | 32 | take_your_finger_frin_my_head.16b48k.wav |
| -0.4319 | 32 | 10 Angels_Fall_First_ringing.wav |
| -0.3856 | 32 | 04 Bachpsichord.wav |
| -0.3430 | 32 | 07 applaud.wav |
| -0.3103 | 32 | TrosYGareg.16b48k.wav |
| -0.2904 | 32 | SinceAlways.16b48k.wav |
| -0.2869 | 32 | Asleep__4.11-4.31_.16b48k.wav |
| -0.2747 | 32 | 11 You look good to me.wav |
| -0.2631 | 32 | hrp.wav |
| -0.2504 | 32 | With Love (Outro).16b48k.wav |
| -0.2474 | 32 | Jupiter, the Bringer of Jolity.16b48k.wav |
| -0.2469 | 32 | Through The Fire And Flames.16b48k.wav |
| -0.2368 | 32 | Berling Drug.wav |

No valid curve: 13 fatboy_30sec.wav (no valid curve), girl.16b48k.wav (no valid curve)
