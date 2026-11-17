# cbrmid vs faac  (decoder ffmpeg, cbr/lc, rungs [128, 160, 192, 256, 320], metric mos)

BD-rate mean **-0.701%** (95% clip-bootstrap CI [-0.862, -0.527], 10000 resamples, seed 0), median -0.752%; valid curves 77/77, no valid curve: 0
PCHIP sign: 8 clips worse / 69 better. Linear (log-rate linear in MOS): **-0.732%** [-0.899, -0.563], n 77, 5 worse / 72 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 128 | 77 | 4.8696 | 4.8660 | +0.0036 | 128.10 | 127.95 | 4.717 | 4.704 | 68/0/9 |
| 160 | 77 | 4.9271 | 4.9257 | +0.0014 | 160.16 | 160.05 | 4.842 | 4.835 | 69/0/8 |
| 192 | 77 | 4.9560 | 4.9549 | +0.0011 | 192.22 | 192.15 | 4.909 | 4.894 | 69/0/8 |
| 256 | 77 | 4.9820 | 4.9814 | +0.0006 | 256.34 | 256.31 | 4.963 | 4.959 | 74/0/3 |
| 320 | 77 | 4.9909 | 4.9907 | +0.0002 | 320.47 | 320.47 | 4.981 | 4.981 | 74/0/3 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0087 | 160 | fms.wav |
| -0.0058 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0056 | 128 | 08 velvet.wav |
| -0.0048 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0037 | 192 | fms.wav |
| -0.0031 | 128 | fms.wav |
| -0.0018 | 128 | 26fr_.wav |
| -0.0018 | 128 | Last_Of_The_Mohicanz__Sample_.16b48k.wav |
| -0.0015 | 160 | 24td_.wav |
| -0.0013 | 192 | 06 trumpet.wav |
| -0.0013 | 160 | 27-last-song-drums-and-trampets.441.16b48k.wav |
| -0.0012 | 128 | Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav |
| -0.0011 | 128 | 27-last-song-drums-and-trampets.441.16b48k.wav |
| -0.0006 | 256 | fms.wav |
| -0.0005 | 128 | 01 castanets.wav |
