# cbrmid vs faac  (decoder faad2, cbr/lc, rungs [128, 160, 192, 256, 320], metric mos)

BD-rate mean **-0.685%** (95% clip-bootstrap CI [-0.851, -0.509], 10000 resamples, seed 0), median -0.711%; valid curves 77/77, no valid curve: 0
PCHIP sign: 7 clips worse / 70 better. Linear (log-rate linear in MOS): **-0.715%** [-0.885, -0.545], n 77, 7 worse / 70 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 77 | 4.8698 | 4.8663 | +0.0035 | 128.10 | 127.95 | 4.718 | 4.707 | 67/0/10 |
| 160 | 77 | 4.9271 | 4.9258 | +0.0014 | 160.16 | 160.05 | 4.843 | 4.835 | 68/0/9 |
| 192 | 77 | 4.9560 | 4.9550 | +0.0011 | 192.22 | 192.15 | 4.909 | 4.895 | 68/0/9 |
| 256 | 77 | 4.9820 | 4.9814 | +0.0006 | 256.34 | 256.31 | 4.963 | 4.959 | 75/0/2 |
| 320 | 77 | 4.9909 | 4.9907 | +0.0002 | 320.47 | 320.47 | 4.981 | 4.981 | 74/0/3 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0095 | 160 | fms.wav |
| -0.0061 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0052 | 128 | 27-last-song-drums-and-trampets.441.16b48k.wav |
| -0.0041 | 192 | fms.wav |
| -0.0030 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0023 | 128 | 26fr_.wav |
| -0.0016 | 128 | 08 velvet.wav |
| -0.0014 | 160 | 24td_.wav |
| -0.0013 | 128 | Last_Of_The_Mohicanz__Sample_.16b48k.wav |
| -0.0013 | 192 | 06 trumpet.wav |
| -0.0013 | 160 | 27-last-song-drums-and-trampets.441.16b48k.wav |
| -0.0013 | 128 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.0010 | 128 | 01 castanets.wav |
| -0.0009 | 128 | mof.wav |
| -0.0005 | 256 | fms.wav |
