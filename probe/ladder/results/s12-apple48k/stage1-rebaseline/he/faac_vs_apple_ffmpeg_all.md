# faac vs apple  (decoder ffmpeg, cbr/he, rungs [32, 40, 48, 64], metric mos)

BD-rate mean **-8.398%** (95% clip-bootstrap CI [-11.497, -5.328], 10000 resamples, seed 0), median -7.858%; valid curves 76/77, no valid curve: 1
PCHIP sign: 21 clips worse / 55 better. Linear (log-rate linear in MOS): **-8.031%** [-11.075, -5.113], n 76, 21 worse / 55 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 32 | 77 | 3.8428 | 3.7967 | +0.0461 | 31.88 | 32.48 | 3.101 | 2.677 | 35/0/42 |
| 40 | 77 | 4.1657 | 4.0406 | +0.1251 | 39.90 | 40.46 | 3.366 | 2.873 | 49/0/28 |
| 48 | 77 | 4.3459 | 4.2147 | +0.1312 | 47.93 | 48.52 | 3.722 | 3.367 | 55/0/22 |
| 64 | 77 | 4.5693 | 4.4617 | +0.1076 | 63.96 | 64.61 | 4.148 | 3.489 | 58/0/19 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.5372 | 32 | Severance__1.31-1.51_.16b48k.wav |
| -0.4948 | 32 | Girl_In_The_Fire__Sample_.16b48k.wav |
| -0.4580 | 32 | take_your_finger_frin_my_head.16b48k.wav |
| -0.4481 | 32 | 10 Angels_Fall_First_ringing.wav |
| -0.3911 | 32 | 04 Bachpsichord.wav |
| -0.3446 | 32 | 07 applaud.wav |
| -0.3128 | 32 | TrosYGareg.16b48k.wav |
| -0.2928 | 32 | SinceAlways.16b48k.wav |
| -0.2829 | 32 | Asleep__4.11-4.31_.16b48k.wav |
| -0.2779 | 32 | hrp.wav |
| -0.2558 | 32 | Through The Fire And Flames.16b48k.wav |
| -0.2525 | 32 | With Love (Outro).16b48k.wav |
| -0.2520 | 32 | 11 You look good to me.wav |
| -0.2491 | 32 | Jupiter, the Bringer of Jolity.16b48k.wav |
| -0.2371 | 32 | Berling Drug.wav |

No valid curve: girl.16b48k.wav (no valid curve)
