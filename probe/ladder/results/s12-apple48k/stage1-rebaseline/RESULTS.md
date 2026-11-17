# S12 Stage 1 results: re-baseline on `ref/apple-48k` (FAAC master 3a656f7)

Prereg: `PREREG.md` (+ addenda). Raw rows: `rows_lc_abr.jsonl` (3465 rows: faac/apple/fdkref x 77 clips x 5 rungs x 3 decoders, 0 errors).
Analysis: `scripts/s12/bd.py` (per-clip PCHIP BD-rate via faac-benchmark `bd_rate_curve`, clip bootstrap 10 000, seed 0) -> `lc_abr/*.md|json`; `scripts/s12/rungsum.py` -> `lc_abr/rungsum_{all,len36}.md`.
`fdkref` = packaged fdkaac 1.0.9 streams (`ref/fdk-48k`). `len36` = the 36 clips whose length matches the manifest (no clip matches `src_sha256`).

## A. LC 48 kHz stereo, ABR, 128-320

### 1. Worst clip x rung, FAAC minus Apple at equal nominal rung (FAAD2; FFmpeg and fdkdec agree within 0.001)

| dMOS | rung | clip |
|---|---|---|
| -0.0797 | 160 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0686 | 128 | 10xh_.wav |
| -0.0582 | 128 | take_your_finger_frin_my_head.16b48k.wav |
| -0.0566 | 128 | SinceAlways.16b48k.wav |
| -0.0534 | 128 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0492 | 128 | fms.wav |
| -0.0447 | 128 | 18vr_.wav |
| -0.0394 | 128 | 14fe_.wav |
| -0.0379 | 192 | 24-Greensleeves-Korean-male-speech.441.16b48k.wav |
| -0.0345 | 128 | Girl_In_The_Fire__Sample_.16b48k.wav |

No cell is below -0.10; none below -0.08. Against FDK see `lc_abr/faac_vs_fdkref_faad2_128-160-192-256-320_all.md`.

### 2. BD-rate (negative = FAAC needs fewer bits), clip-bootstrap 95 % CI, (valid curves)

| vs | range | clips | FAAD2 | FFmpeg | fdkdec |
|---|---|---|---|---|---|
| Apple | 128-192 | all | -2.79 [-4.59, -0.85] (76) | -2.72 [-4.53, -0.79] (76) | -2.72 [-4.53, -0.80] (76) |
| Apple | 128-192 | len36 | -3.77 [-6.32, -0.80] (36) | -3.74 [-6.31, -0.75] (36) | -3.74 [-6.29, -0.80] (36) |
| Apple | 192-320 | all | -2.78 [-4.88, -0.77] (76) | -2.77 [-4.87, -0.76] (76) | -2.77 [-4.87, -0.76] (76) |
| Apple | 192-320 | len36 | -3.64 [-6.81, -1.08] (35) | -3.63 [-6.80, -1.06] (35) | -3.62 [-6.79, -1.06] (35) |
| Apple | 128-320 | all | -2.67 [-4.31, -1.08] (77) | -2.62 [-4.26, -1.02] (77) | -2.62 [-4.26, -1.01] (77) |
| Apple | 128-320 | len36 | -3.10 [-4.97, -1.06] (36) | -3.07 [-4.95, -1.01] (36) | -3.05 [-4.94, -0.98] (36) |
| FDK | 128-192 | all | -16.24 [-18.63, -13.70] (73) | -16.16 [-18.56, -13.61] (73) | -16.17 [-18.56, -13.62] (73) |
| FDK | 128-192 | len36 | -16.80 [-20.27, -12.61] (34) | -16.75 [-20.24, -12.51] (34) | -16.74 [-20.23, -12.53] (34) |
| FDK | 192-320 | all | -22.02 [-25.19, -18.89] (50) | -21.96 [-25.13, -18.84] (50) | -21.59 [-24.70, -18.41] (49) |
| FDK | 192-320 | len36 | -23.66 [-28.67, -18.38] (22) | -23.60 [-28.60, -18.34] (22) | -23.59 [-28.61, -18.32] (22) |
| FDK | 128-320 | all | -19.35 [-21.96, -16.76] (76) | -19.29 [-21.89, -16.69] (76) | -19.27 [-21.88, -16.68] (76) |
| FDK | 128-320 | len36 | -20.61 [-24.55, -16.86] (36) | -20.56 [-24.50, -16.80] (36) | -20.52 [-24.45, -16.78] (36) |

Call (prereg rule): **FAAC leads Apple** in 128-192, 192-320 and 128-320 under all three decoders, all 77 and the 36 length-matched clips alike; **FAAC leads FDK** by 16-24 %. The old HANDOFF gap (+3.8 to +6.9 %) does not exist on master and this corpus.

### 3. Per rung (FAAD2; all decoders in `lc_abr/rungsum_all.md`)

| profile | mode | arm | rung | decoder | n | MOS | P1 | NPER pooled dB | events | stereo fid | kbps |
|---|---|---|---|---|---|---|---|---|---|---|---|
| lc | abr | faac | 128 | faad2 | 77 | 4.8668 | 4.708 | 5.121 | 965 | 0.9952 | 126.36 |
| lc | abr | faac | 160 | faad2 | 77 | 4.9259 | 4.837 | 2.793 | 965 | 0.9970 | 157.85 |
| lc | abr | faac | 192 | faad2 | 77 | 4.9553 | 4.905 | 1.992 | 965 | 0.9975 | 189.41 |
| lc | abr | faac | 256 | faad2 | 77 | 4.9815 | 4.963 | 1.021 | 965 | 0.9982 | 252.38 |
| lc | abr | faac | 320 | faad2 | 77 | 4.9907 | 4.981 | 0.573 | 965 | 0.9987 | 314.28 |
| lc | ref | apple | 128 | faad2 | 77 | 4.8466 | 4.488 | 3.847 | 965 | 0.9940 | 128.56 |
| lc | ref | apple | 160 | faad2 | 77 | 4.9192 | 4.766 | 2.878 | 965 | 0.9958 | 160.64 |
| lc | ref | apple | 192 | faad2 | 77 | 4.9580 | 4.898 | 2.620 | 965 | 0.9970 | 192.74 |
| lc | ref | apple | 256 | faad2 | 77 | 4.9795 | 4.952 | 1.822 | 965 | 0.9979 | 256.98 |
| lc | ref | apple | 320 | faad2 | 77 | 4.9919 | 4.982 | 0.959 | 965 | 0.9986 | 321.19 |
| lc | ref | fdkref | 128 | faad2 | 77 | 4.8262 | 4.633 | 4.979 | 965 | 0.9960 | 128.61 |
| lc | ref | fdkref | 160 | faad2 | 77 | 4.8799 | 4.720 | 3.225 | 965 | 0.9967 | 160.67 |
| lc | ref | fdkref | 192 | faad2 | 77 | 4.9266 | 4.828 | 2.612 | 965 | 0.9972 | 192.74 |
| lc | ref | fdkref | 256 | faad2 | 77 | 4.9524 | 4.886 | 2.021 | 965 | 0.9976 | 256.90 |
| lc | ref | fdkref | 320 | faad2 | 77 | 4.9598 | 4.896 | 1.659 | 965 | 0.9979 | 321.11 |

Per-clip wins vs Apple at equal nominal rung (FAAD2): 128k 51/77, 160k 48/77, **192k 17/77**, 256k 43/77, **320k 33/77**.

Reading: at equal nominal rung Apple keeps a mean-MOS edge only at 192k (-0.0027) and 320k (-0.0012), where FAAC spends 1.7 % / 2.2 % fewer bits (189.4 vs 192.7, 314.3 vs 321.2 kbps). At the 0.0016 MOS per +1 % rate slope measured earlier that rate difference is about 0.003 MOS, the size of the deficit; at equal realized rate FAAC leads (BD-rate above). FAAC ABR realizes 1.3-1.8 % under nominal on this corpus, Apple +0.4 %.

### Master vs leaderboard build (gist FAAC 3.0.0 a7d2f755, Apple 27.2, fdkaac 1.0.9; FAAD2 local)

| rung | FAAC local / gist | Apple local / gist | FDK local / gist | FAAC NPER local / gist | Apple NPER local / gist |
|---|---|---|---|---|---|
| 128 | 4.8668 / 4.866 | 4.8466 / 4.847 | 4.8262 / 4.826 | 5.12 / 5.11 | 3.85 / 3.85 |
| 160 | 4.9259 / 4.926 | 4.9192 / 4.919 | 4.8799 / 4.880 | 2.79 / 2.88 | 2.88 / 2.88 |
| 192 | 4.9553 / 4.955 | 4.9580 / 4.958 | 4.9266 / 4.927 | 1.99 / 2.08 | 2.62 / 2.62 |
| 256 | 4.9815 / 4.981 | 4.9795 / 4.980 | 4.9524 / 4.953 | 1.02 / 1.08 | 1.82 / 1.82 |
| 320 | 4.9907 / 4.991 | 4.9919 / 4.992 | 4.9598 / 4.960 | 0.57 / 0.64 | 0.96 / 0.96 |

Master matches the leaderboard build at LC 128-320 within 0.001 MOS; Apple and FDK reproduce exactly, so the local scorer reproduces the leaderboard. FAAC NPER is 0.06-0.09 dB lower locally at 160-320 (small build or pooling difference). Open: stereo fidelity is about 0.002 higher locally for every encoder (pooling not resolved), and the gist reports FAAC LC bitrate error 0.0-0.1 % while the harness measures -1.3 to -1.8 % with the same ES-bytes / source-duration definition as `phase1_encode` (not resolved). HE-v2 differs (anomaly item).

## B. HE-v1 / HE-v2, C. band split: pending (queued).
