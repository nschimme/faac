# cbrv2 vs faac

| decoder | rung | n | dMOS mean | worst dMOS (clip) | cells < -0.10 | dkbps % |
|---|---|---|---|---|---|---|---|---|---|---|
| faad2 | 128 | 77 | +0.0029 | -0.016 (10xh_.wav) | 0 | 5.12 | nan | -5.12 [-7.16, -1.71] | +nan [+nan, +nan] | +0.12 |
| faad2 | 160 | 77 | +0.0016 | -0.002 (27-last-song-drums-and) | 0 | 2.89 | nan | -2.89 [-4.02, -1.08] | +nan [+nan, +nan] | +0.06 |
| faad2 | 192 | 77 | +0.0009 | -0.002 (06 trumpet.wav) | 0 | 2.06 | nan | -2.06 [-2.82, -0.84] | +nan [+nan, +nan] | +0.04 |
| faad2 | 256 | 77 | +0.0005 | -0.002 (08 velvet.wav) | 0 | 1.10 | nan | -1.10 [-1.41, -0.52] | +nan [+nan, +nan] | +0.01 |
| faad2 | 320 | 77 | +0.0002 | -0.000 (12-German-male-speech.) | 0 | 0.64 | nan | -0.64 [-0.86, -0.35] | +nan [+nan, +nan] | +0.00 |
| fdkdec | 128 | 77 | +0.0030 | -0.018 (10xh_.wav) | 0 | 5.11 | nan | -5.11 [-7.14, -1.70] | +nan [+nan, +nan] | +0.12 |
| fdkdec | 160 | 77 | +0.0018 | -0.001 (27-last-song-drums-and) | 0 | 2.88 | nan | -2.88 [-4.04, -1.09] | +nan [+nan, +nan] | +0.06 |
| fdkdec | 192 | 77 | +0.0010 | -0.001 (06 trumpet.wav) | 0 | 2.06 | nan | -2.06 [-2.80, -0.83] | +nan [+nan, +nan] | +0.04 |
| fdkdec | 256 | 77 | +0.0005 | -0.002 (08 velvet.wav) | 0 | 1.10 | nan | -1.10 [-1.41, -0.52] | +nan [+nan, +nan] | +0.01 |
| fdkdec | 320 | 77 | +0.0002 | -0.000 (12-German-male-speech.) | 0 | 0.64 | nan | -0.64 [-0.87, -0.35] | +nan [+nan, +nan] | +0.00 |
| ffmpeg | 128 | 77 | +0.0030 | -0.011 (10xh_.wav) | 0 | 5.12 | nan | -5.12 [-7.18, -1.77] | +nan [+nan, +nan] | +0.12 |
| ffmpeg | 160 | 77 | +0.0017 | -0.002 (27-last-song-drums-and) | 0 | 2.88 | nan | -2.88 [-4.02, -1.05] | +nan [+nan, +nan] | +0.06 |
| ffmpeg | 192 | 77 | +0.0010 | -0.002 (06 trumpet.wav) | 0 | 2.06 | nan | -2.06 [-2.79, -0.83] | +nan [+nan, +nan] | +0.04 |
| ffmpeg | 256 | 77 | +0.0005 | -0.002 (08 velvet.wav) | 0 | 1.10 | nan | -1.10 [-1.41, -0.51] | +nan [+nan, +nan] | +0.01 |
| ffmpeg | 320 | 77 | +0.0002 | -0.000 (12-German-male-speech.) | 0 | 0.64 | nan | -0.64 [-0.86, -0.35] | +nan [+nan, +nan] | +0.00 |

Worst clip x rung x decoder cells:

| dMOS | decoder | rung | clip |
|---|---|---|---|
| -0.0178 | fdkdec | 128 | 10xh_.wav |
| -0.0160 | faad2 | 128 | 10xh_.wav |
| -0.0115 | ffmpeg | 128 | 10xh_.wav |
| -0.0079 | faad2 | 128 | mof.wav |
| -0.0073 | fdkdec | 128 | mof.wav |
| -0.0069 | ffmpeg | 128 | mof.wav |
| -0.0045 | fdkdec | 128 | 27-last-song-drums-and-trampets.441.16b48k.wav |
| -0.0031 | faad2 | 128 | 18vr_.wav |
