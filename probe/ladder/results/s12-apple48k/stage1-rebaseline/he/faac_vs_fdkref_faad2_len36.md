# faac vs fdkref  (decoder faad2, cbr/he, rungs [32, 40, 48, 64], metric mos)

BD-rate mean **-6.866%** (95% clip-bootstrap CI [-10.049, -3.718], 10000 resamples, seed 0), median -6.637%; valid curves 36/36, no valid curve: 0
PCHIP sign: 7 clips worse / 29 better. Linear (log-rate linear in MOS): **-6.804%** [-9.860, -3.743], n 36, 7 worse / 29 better.

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 32 | 36 | 3.8471 | 3.7857 | +0.0614 | 31.88 | 32.60 | 3.243 | 2.672 | 19/0/17 |
| 40 | 36 | 4.1585 | 4.0931 | +0.0655 | 39.90 | 40.57 | 3.511 | 3.211 | 23/0/13 |
| 48 | 36 | 4.3395 | 4.2517 | +0.0878 | 47.93 | 48.56 | 3.843 | 3.861 | 29/0/7 |
| 64 | 36 | 4.5557 | 4.5175 | +0.0382 | 63.97 | 64.61 | 4.108 | 4.038 | 28/0/8 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.4155 | 32 | Severance__1.31-1.51_.16b48k.wav |
| -0.3000 | 32 | 24td_.wav |
| -0.2695 | 32 | 29ru_.wav |
| -0.2163 | 32 | Asleep__4.11-4.31_.16b48k.wav |
| -0.2137 | 32 | Changes.16b48k.wav |
| -0.2097 | 40 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.2048 | 48 | 01 castanets.wav |
| -0.1868 | 32 | 35_SQAM_glockenspiel_cut.16b48k.wav |
| -0.1851 | 40 | 01 castanets.wav |
| -0.1849 | 64 | 03 eig.wav |
| -0.1766 | 48 | 07 applaud.wav |
| -0.1755 | 40 | Asleep__4.11-4.31_.16b48k.wav |
| -0.1623 | 32 | 22ex_.wav |
| -0.1607 | 64 | 01 castanets.wav |
| -0.1512 | 32 | With Love (Outro).16b48k.wav |
