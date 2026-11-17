# faac vs fdkref  (decoder faad2, abr/lc, rungs [192, 256, 320], metric mos)

BD-rate mean **-23.661%** (95% clip-bootstrap CI [-28.673, -18.381], 10000 resamples, seed 0), median -25.392%; valid curves 22/36, no valid curve: 14

| rung | n | test MOS | ref MOS | dMOS | test kbps | ref kbps | test P1 | ref P1 | W/T/L |
|---|---|---|---|---|---|---|---|---|---|
| 192 | 36 | 4.9538 | 4.9231 | +0.0307 | 188.94 | 192.72 | 4.900 | 4.829 | 34/0/2 |
| 256 | 36 | 4.9809 | 4.9498 | +0.0311 | 251.73 | 256.87 | 4.962 | 4.890 | 35/0/1 |
| 320 | 36 | 4.9904 | 4.9581 | +0.0323 | 313.59 | 321.07 | 4.981 | 4.902 | 35/0/1 |

Worst clip x rung dMOS (test - ref at equal nominal rung):

| dMOS | rung | clip |
|---|---|---|
| -0.0197 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0055 | 192 | 21-classic.441.16b48k.wav |
| -0.0030 | 256 | 21-classic.441.16b48k.wav |
| -0.0003 | 320 | 21-classic.441.16b48k.wav |
| +0.0023 | 192 | glk.wav |
| +0.0034 | 192 | Severance__1.31-1.51_.16b48k.wav |
| +0.0035 | 256 | glk.wav |
| +0.0037 | 256 | Severance__1.31-1.51_.16b48k.wav |
| +0.0038 | 192 | bah.wav |
| +0.0039 | 192 | bas.wav |
| +0.0040 | 256 | bah.wav |
| +0.0041 | 320 | glk.wav |
| +0.0050 | 256 | bas.wav |
| +0.0060 | 320 | Severance__1.31-1.51_.16b48k.wav |
| +0.0080 | 320 | bas.wav |

No valid curve: 03 eig.wav (no valid curve), 06 trumpet.wav (no valid curve), 10xh_.wav (no valid curve), 17qu_.wav (no valid curve), 22ex_.wav (no valid curve), 24td_.wav (no valid curve), 26fr_.wav (no valid curve), 29ru_.wav (no valid curve), 35_SQAM_glockenspiel_cut.16b48k.wav (no valid curve), Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav (no valid curve), Hey Tonight.16b48k.wav (no valid curve), Robots_old.16b48k.wav (no valid curve), With Love (Outro).16b48k.wav (no valid curve), glk.wav (no valid curve)
