# cbrmid vs faac  (decoder fdkdec, cbr/lc, rungs [128, 160, 192, 256, 320], metric mos)

BD-rate mean **-0.721%** (95% clip-bootstrap CI [-0.879, -0.554], 10000 resamples, seed 0), median -0.786%; valid curves 77/77, no valid curve: 0
PCHIP sign: 8 clips worse / 69 better. Linear (log-rate linear in MOS): **-0.747%** [-0.910, -0.584], n 77, 8 worse / 69 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 77 | 4.8695 | 4.8658 | +0.0036 | 128.10 | 127.95 | 4.718 | 4.706 | 67/0/10 |
| 160 | 77 | 4.9271 | 4.9255 | +0.0016 | 160.16 | 160.05 | 4.841 | 4.834 | 70/0/7 |
| 192 | 77 | 4.9560 | 4.9549 | +0.0011 | 192.22 | 192.15 | 4.909 | 4.894 | 70/0/7 |
| 256 | 77 | 4.9820 | 4.9814 | +0.0006 | 256.34 | 256.31 | 4.963 | 4.959 | 74/0/3 |
| 320 | 77 | 4.9909 | 4.9907 | +0.0002 | 320.47 | 320.47 | 4.981 | 4.981 | 74/0/3 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0087 | 160 | fms.wav |
| -0.0042 | 128 | 27-last-song-drums-and-trampets.441.16b48k.wav |
| -0.0042 | 192 | fms.wav |
| -0.0036 | 128 | 08 velvet.wav |
| -0.0035 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0022 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0018 | 128 | 26fr_.wav |
| -0.0017 | 128 | Last_Of_The_Mohicanz__Sample_.16b48k.wav |
| -0.0016 | 192 | 06 trumpet.wav |
| -0.0013 | 160 | 27-last-song-drums-and-trampets.441.16b48k.wav |
| -0.0013 | 160 | 24td_.wav |
| -0.0013 | 128 | 9-Have-big-expensive-car.441.16b48k.wav |
| -0.0011 | 128 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.0009 | 128 | 01 castanets.wav |
| -0.0006 | 256 | fms.wav |
