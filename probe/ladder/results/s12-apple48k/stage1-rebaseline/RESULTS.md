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

### 2. BD-rate (negative = FAAC needs fewer bits), two methods

P = benchmark PCHIP (`bd_rate_curve`); L = log-rate linear in MOS over the shared MOS range (`bd.py bd_linear`). Each: mean, clip-bootstrap 95 % CI, clips worse/better. A verdict needs both methods to agree. With three rungs and MOS saturating near 5.0, PCHIP on three points is fragile at 192-320 (curves are nearly flat, small MOS noise moves the shared interval).

| vs | range | clips | FAAD2 | FFmpeg | fdkdec |
|---|---|---|---|---|---|
| Apple | 128-192 | all | P -2.79 [-4.59,-0.85] 23/53; L -2.61 [-4.30,-0.82] 23/53 | P -2.72 [-4.53,-0.79] 24/52; L -2.55 [-4.24,-0.75] 21/55 | P -2.72 [-4.53,-0.80] 24/52; L -2.54 [-4.24,-0.74] 22/54 |
| Apple | 128-192 | len36 | P -3.77 [-6.32,-0.80] 8/28; L -3.69 [-6.03,-1.03] 8/28 | P -3.74 [-6.31,-0.75] 8/28; L -3.65 [-6.01,-0.98] 7/29 | P -3.74 [-6.29,-0.80] 8/28; L -3.63 [-5.98,-0.96] 8/28 |
| Apple | 192-320 | all | P -2.78 [-4.88,-0.77] 26/50; L -1.17 [-3.17,+0.72] 34/42 | P -2.77 [-4.87,-0.76] 26/50; L -1.16 [-3.15,+0.73] 34/42 | P -2.77 [-4.87,-0.76] 26/50; L -1.15 [-3.14,+0.74] 34/42 |
| Apple | 192-320 | len36 | P -3.64 [-6.81,-1.08] 12/23; L -2.05 [-5.25,+0.44] 16/19 | P -3.63 [-6.80,-1.06] 12/23; L -2.04 [-5.23,+0.46] 16/19 | P -3.62 [-6.79,-1.06] 12/23; L -2.02 [-5.19,+0.46] 16/19 |
| Apple | 128-320 | all | P -2.67 [-4.31,-1.08] 18/59; L -2.51 [-4.15,-0.91] 19/58 | P -2.62 [-4.26,-1.02] 20/57; L -2.46 [-4.11,-0.85] 21/56 | P -2.62 [-4.26,-1.01] 20/57; L -2.45 [-4.11,-0.84] 21/56 |
| Apple | 128-320 | len36 | P -3.10 [-4.97,-1.06] 8/28; L -2.97 [-4.81,-0.89] 9/27 | P -3.07 [-4.95,-1.01] 8/28; L -2.94 [-4.79,-0.83] 9/27 | P -3.05 [-4.94,-0.98] 8/28; L -2.92 [-4.78,-0.81] 9/27 |
| FDK | 128-192 | all | P -16.24 [-18.63,-13.70] 3/70; L -15.28 [-17.55,-12.90] 3/70 | P -16.16 [-18.56,-13.61] 3/70; L -15.21 [-17.49,-12.81] 3/70 | P -16.17 [-18.56,-13.62] 3/70; L -15.21 [-17.49,-12.81] 3/70 |
| FDK | 128-192 | len36 | P -16.80 [-20.27,-12.61] 1/33; L -15.84 [-19.05,-12.11] 1/33 | P -16.75 [-20.24,-12.51] 1/33; L -15.78 [-19.01,-12.01] 1/33 | P -16.74 [-20.23,-12.53] 1/33; L -15.77 [-18.98,-12.00] 1/33 |
| FDK | 192-320 | all | P -22.02 [-25.19,-18.89] 1/49; L -21.43 [-24.50,-18.43] 1/49 | P -21.96 [-25.13,-18.84] 1/49; L -21.37 [-24.45,-18.37] 1/49 | P -21.59 [-24.70,-18.41] 1/48; L -20.98 [-23.99,-17.95] 1/48 |
| FDK | 192-320 | len36 | P -23.66 [-28.67,-18.38] 1/21; L -23.03 [-27.95,-17.88] 1/21 | P -23.60 [-28.60,-18.34] 1/21; L -22.96 [-27.87,-17.81] 1/21 | P -23.59 [-28.61,-18.32] 1/21; L -22.96 [-27.88,-17.80] 1/21 |
| FDK | 128-320 | all | P -19.35 [-21.96,-16.76] 2/74; L -19.42 [-22.09,-16.77] 2/74 | P -19.29 [-21.89,-16.69] 2/74; L -19.36 [-22.04,-16.71] 2/74 | P -19.27 [-21.88,-16.68] 2/74; L -19.35 [-22.01,-16.71] 2/74 |
| FDK | 128-320 | len36 | P -20.61 [-24.55,-16.86] 1/35; L -20.66 [-24.67,-16.86] 1/35 | P -20.56 [-24.50,-16.80] 1/35; L -20.61 [-24.61,-16.80] 1/35 | P -20.52 [-24.45,-16.78] 1/35; L -20.57 [-24.56,-16.77] 1/35 |

Calls (both methods must agree; corrected 2026-11-17 after the parent recomputed 192-320 with linear interpolation):
- **128-192 and 128-320: FAAC leads Apple** (both methods, all three decoders, both clip sets; 53-59 of 76-77 clips better).
- **192-320 vs Apple: parity, lead not established.** PCHIP gives -2.8 % [-4.9, -0.8] but linear gives -1.2 % [-3.2, +0.7] with FAAC worse on 34 of 76 clips; the earlier "FAAC leads" call for this range was PCHIP-only and is withdrawn.
- **vs FDK: FAAC leads** by 15-23 % in every range (both methods).

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

Reading: at equal nominal rung Apple keeps a mean-MOS edge only at 192k (-0.0027) and 320k (-0.0012), where FAAC spends 1.7 % / 2.2 % fewer bits (189.4 vs 192.7, 314.3 vs 321.2 kbps). At the 0.0016 MOS per +1 % rate slope measured earlier that rate difference is about 0.003 MOS, the size of the deficit; at equal realized rate the 192-320 comparison is parity (BD-rate above). FAAC ABR realizes 1.3-1.8 % under nominal on this corpus, Apple +0.4 %.

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
