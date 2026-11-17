# SBR time grid at scored onsets (HE-AAC v1, 48 kHz output, FAAC master CBR vs Apple/FDK/xaac)

**Reading note**: FAAC at 40/48 kbps (identical grids at both rungs) uses FIXFIX with 4 envelopes in 97.5 % of frames (borders at slots 4, 8, 12), so its >1-envelope and near-border shares are the chance level of a regular grid (an onset uniformly placed in 0-16 is within +-2 slots of 4/8/12 with p = 0.75), not transient adaptation. FAAC at 32 kbps: 73 % FIXFIX/1 env, 27 % VARFIX. Apple 32 kbps is a 32 kHz-output stream (16 kHz core). Best-delta for FAAC in the alignment table is noise (no peak).

Decoder: instrumented libfaad (`E` records, patch `libfaad_decdump_sbrgrid.patch`). Grid per SBR frame/channel: frame_class, num_env L_E, envelope borders t_E in time slots (1 slot = 128 output samples, 16 slots per 2048-sample frame; VAR classes can place borders up to slot 18), freq_res per envelope.
Onsets: the same merged scored onsets as `onsets_he.md` (697 onsets over 77 clips). Aligned frame k = floor((onset + lag + delta)/2048), lag by xcorr of ADTS-remuxed FAAD decode vs source (re-estimated here for every stream incl. FAAC master CBR, 40 kbps, xaac); position in slots = remainder/128.

## Alignment check (offset delta added to the xcorr lag, samples)

Share of onsets with an interior envelope border (t_E not 0/16) of frame k within +-2 slots, pooled over 32/40/48 kbps, as a function of delta (curves in `sbr_grid_align_curves.json`). Reference encoders peak sharply; FAAC (almost entirely FIXFIX) shows no peak.

| encoder | best delta (samples) | near2 at best | near2 at delta=0 |
|---|---|---|---|
| faac | 3328 | 54.0 | 51.1 |
| apple | -256 | 84.4 | 77.6 |
| fdk | -256 | 87.1 | 74.8 |
| xaac | -256 | 84.8 | 76.8 |

Apple, FDK and xaac all peak at delta = -256 samples (2 slots) relative to the xcorr lag (i.e. SBR grid timing leads the broadband lag by 2 slots, the residual offset); main tables below use delta = -256 for all encoders (FAAC has no peak of its own, its tiny VAR use makes the choice immaterial; delta = 0 also in JSON).

Core check: % of onsets whose onset channel(s) have EIGHT_SHORT at decoded frame floor((onset+lag+delta)/2048) vs delta (rungs pooled). Reference encoders peak at delta = -2048, i.e. the core frame holding the onset is k-1 under the xcorr-lag frame index (the ~1 frame residual noted earlier); the SBR grid of frame index k (delta -256) is the one carrying the onset. FAAC is saturated (short blocks in >90 % of onset frames at every delta), so its core curve is uninformative; its alignment is assumed identical (same xcorr procedure). Apple 32 kbps decodes at 32 kHz output (16 kHz core): delta and lag are scaled to its output rate.

| delta | faac | apple | fdk | xaac |
|---|---|---|---|---|
| -4096 | 93.1 | 43.2 | 24.8 | 22.7 |
| -3584 | 94.4 | 50.5 | 34.1 | 31.9 |
| -3072 | 97.8 | 55.9 | 45.9 | 43.7 |
| -2560 | 100.0 | 60.3 | 53.7 | 54.5 |
| -2048 | 100.0 | 63.8 | 64.3 | 62.6 |
| -1536 | 100.0 | 54.9 | 50.2 | 54.5 |
| -1024 | 97.7 | 45.1 | 36.0 | 38.9 |
| -512 | 97.0 | 35.5 | 24.7 | 24.1 |
| 0 | 95.0 | 25.3 | 12.2 | 12.5 |
| 512 | 92.8 | 21.8 | 10.5 | 7.7 |
| 1024 | 91.5 | 21.6 | 9.9 | 7.7 |


## Distribution over all SBR frames/channels

| rung | encoder | n | FIXFIX % | FIXVAR % | VARFIX % | VARVAR % | env=1 % | env=2 % | env=3 % | env=4 % | freq_res hi % |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 32 | faac | 69982 | 73.3 | 0.0 | 26.7 | 0.0 | 73.3 | 26.7 | 0.0 | 0.0 | 100.0 |
| 32 | apple | 46966 | 53.6 | 19.1 | 19.1 | 8.2 | 24.1 | 51.1 | 10.8 | 13.5 | 76.5 |
| 32 | fdk | 70204 | 61.6 | 14.9 | 14.9 | 8.5 | 33.8 | 45.2 | 8.6 | 11.9 | 78.6 |
| 32 | xaac | 70062 | 70.3 | 13.0 | 12.9 | 3.8 | 48.7 | 35.8 | 5.8 | 9.2 | 81.9 |
| 40 | faac | 69982 | 97.7 | 0.0 | 2.3 | 0.0 | 0.2 | 2.3 | 0.0 | 97.5 | 100.0 |
| 40 | apple | 70212 | 64.3 | 14.6 | 14.5 | 6.6 | 47.0 | 33.9 | 8.1 | 10.6 | 78.7 |
| 40 | fdk | 70204 | 61.6 | 14.9 | 14.9 | 8.5 | 35.9 | 43.1 | 8.6 | 11.9 | 78.3 |
| 40 | xaac | 70062 | 70.3 | 13.0 | 12.9 | 3.8 | 50.2 | 34.3 | 5.8 | 9.2 | 81.8 |
| 48 | faac | 69982 | 97.7 | 0.0 | 2.3 | 0.0 | 0.2 | 2.3 | 0.0 | 97.5 | 100.0 |
| 48 | apple | 70212 | 64.4 | 14.5 | 14.5 | 6.6 | 49.8 | 31.1 | 8.1 | 10.6 | 78.4 |
| 48 | fdk | 70204 | 61.6 | 14.9 | 14.9 | 8.5 | 39.6 | 39.5 | 8.6 | 11.9 | 77.9 |
| 48 | xaac | 70062 | 70.3 | 13.0 | 12.9 | 3.8 | 53.4 | 31.2 | 5.8 | 9.2 | 81.4 |

## At scored onsets, delta = -256

env>1 = any onset channel has more than one envelope; near2 / near1 = interior border within +-2 / +-1 slots (onset slot position vs borders of that frame); mean dist (slots) to the nearest interior border over onsets that have one; `any-border` dist also counts the frame edges (0/16).

| rung | encoder | frame | n | env>1 % | has interior border % | near2 % | near1 % | mean dist interior | mean dist any-border | FIXFIX % at onset |
|---|---|---|---|---|---|---|---|---|---|---|
| 32 | faac | k | 697 | 72.3 | 72.3 | 18.7 | 8.2 | 4.00 | 2.46 | 27.7 |
| 32 | faac | k-1 | 697 | 54.2 | 54.2 | 0.0 | 0.0 | 15.38 | 8.14 | 45.8 |
| 32 | apple | k | 697 | 99.7 | 99.7 | 80.5 | 68.1 | 1.31 | 1.00 | 3.2 |
| 32 | apple | k-1 | 697 | 81.2 | 81.2 | 18.9 | 9.0 | 10.44 | 7.32 | 29.0 |
| 32 | fdk | k | 697 | 99.4 | 99.4 | 87.1 | 74.0 | 1.19 | 0.87 | 4.9 |
| 32 | fdk | k-1 | 697 | 67.9 | 67.9 | 17.8 | 13.3 | 9.82 | 7.58 | 45.5 |
| 32 | xaac | k | 697 | 98.4 | 98.4 | 85.4 | 75.5 | 1.20 | 0.97 | 6.7 |
| 32 | xaac | k-1 | 697 | 63.4 | 63.4 | 20.5 | 16.1 | 8.65 | 7.26 | 47.2 |
| 40 | faac | k | 697 | 100.0 | 100.0 | 64.7 | 30.7 | 2.08 | 1.19 | 81.8 |
| 40 | faac | k-1 | 697 | 100.0 | 100.0 | 0.0 | 0.0 | 12.56 | 8.14 | 89.4 |
| 40 | apple | k | 697 | 98.1 | 98.1 | 86.5 | 76.0 | 1.13 | 0.92 | 4.0 |
| 40 | apple | k-1 | 697 | 62.8 | 62.8 | 20.4 | 16.4 | 7.91 | 7.26 | 43.0 |
| 40 | fdk | k | 697 | 98.9 | 98.9 | 87.1 | 74.0 | 1.18 | 0.88 | 4.9 |
| 40 | fdk | k-1 | 697 | 66.7 | 66.7 | 17.8 | 13.3 | 9.75 | 7.58 | 45.5 |
| 40 | xaac | k | 697 | 97.3 | 97.3 | 85.1 | 75.2 | 1.15 | 0.98 | 6.7 |
| 40 | xaac | k-1 | 697 | 61.8 | 61.8 | 20.5 | 16.1 | 8.44 | 7.26 | 47.2 |
| 48 | faac | k | 697 | 100.0 | 100.0 | 64.7 | 30.7 | 2.08 | 1.19 | 81.8 |
| 48 | faac | k-1 | 697 | 100.0 | 100.0 | 0.0 | 0.0 | 12.56 | 8.14 | 89.4 |
| 48 | apple | k | 697 | 97.4 | 97.4 | 86.1 | 76.0 | 1.12 | 0.95 | 4.0 |
| 48 | apple | k-1 | 697 | 62.0 | 62.0 | 20.4 | 16.4 | 7.85 | 7.27 | 42.9 |
| 48 | fdk | k | 697 | 98.3 | 98.3 | 87.1 | 74.0 | 1.16 | 0.89 | 4.9 |
| 48 | fdk | k-1 | 697 | 64.6 | 64.6 | 17.8 | 13.3 | 9.47 | 7.58 | 45.5 |
| 48 | xaac | k | 697 | 95.1 | 95.1 | 84.1 | 74.6 | 1.11 | 1.05 | 6.7 |
| 48 | xaac | k-1 | 697 | 60.4 | 60.4 | 20.5 | 16.1 | 8.28 | 7.26 | 47.2 |

## At scored onsets, delta = 0

env>1 = any onset channel has more than one envelope; near2 / near1 = interior border within +-2 / +-1 slots (onset slot position vs borders of that frame); mean dist (slots) to the nearest interior border over onsets that have one; `any-border` dist also counts the frame edges (0/16).

| rung | encoder | frame | n | env>1 % | has interior border % | near2 % | near1 % | mean dist interior | mean dist any-border | FIXFIX % at onset |
|---|---|---|---|---|---|---|---|---|---|---|
| 32 | faac | k | 697 | 66.7 | 66.7 | 15.6 | 8.9 | 4.00 | 2.57 | 33.3 |
| 32 | faac | k-1 | 697 | 61.3 | 61.3 | 0.0 | 0.0 | 15.01 | 7.91 | 38.7 |
| 32 | apple | k | 697 | 99.7 | 99.7 | 79.3 | 57.5 | 1.39 | 1.00 | 3.3 |
| 32 | apple | k-1 | 697 | 83.8 | 83.8 | 12.6 | 6.2 | 9.48 | 6.86 | 25.3 |
| 32 | fdk | k | 697 | 99.3 | 99.3 | 75.0 | 58.4 | 1.48 | 1.05 | 4.9 |
| 32 | fdk | k-1 | 697 | 75.0 | 75.0 | 13.2 | 6.5 | 9.14 | 7.27 | 36.4 |
| 32 | xaac | k | 697 | 97.7 | 97.7 | 77.6 | 56.5 | 1.38 | 1.11 | 7.5 |
| 32 | xaac | k-1 | 697 | 68.3 | 68.3 | 12.8 | 5.3 | 8.14 | 7.29 | 41.5 |
| 40 | faac | k | 697 | 100.0 | 100.0 | 68.9 | 36.4 | 1.82 | 1.08 | 86.5 |
| 40 | faac | k-1 | 697 | 100.0 | 100.0 | 0.0 | 0.0 | 12.53 | 7.91 | 84.5 |
| 40 | apple | k | 697 | 98.0 | 98.0 | 77.0 | 56.8 | 1.36 | 1.12 | 3.9 |
| 40 | apple | k-1 | 697 | 68.7 | 68.7 | 15.2 | 6.9 | 7.46 | 7.19 | 35.7 |
| 40 | fdk | k | 697 | 98.7 | 98.7 | 74.9 | 58.2 | 1.46 | 1.06 | 4.9 |
| 40 | fdk | k-1 | 697 | 73.7 | 73.7 | 13.2 | 6.5 | 9.01 | 7.27 | 36.4 |
| 40 | xaac | k | 697 | 96.6 | 96.6 | 76.9 | 56.7 | 1.35 | 1.12 | 7.5 |
| 40 | xaac | k-1 | 697 | 66.9 | 66.9 | 13.5 | 5.3 | 7.73 | 7.17 | 40.7 |
| 48 | faac | k | 697 | 100.0 | 100.0 | 68.9 | 36.4 | 1.82 | 1.08 | 86.5 |
| 48 | faac | k-1 | 697 | 100.0 | 100.0 | 0.0 | 0.0 | 12.53 | 7.91 | 84.5 |
| 48 | apple | k | 697 | 97.4 | 97.4 | 76.5 | 55.7 | 1.37 | 1.16 | 3.9 |
| 48 | apple | k-1 | 697 | 68.1 | 68.1 | 15.4 | 6.7 | 7.46 | 7.19 | 35.7 |
| 48 | fdk | k | 697 | 98.1 | 98.1 | 74.6 | 58.0 | 1.46 | 1.08 | 4.9 |
| 48 | fdk | k-1 | 697 | 71.7 | 71.7 | 13.2 | 6.5 | 8.70 | 7.27 | 36.4 |
| 48 | xaac | k | 697 | 94.3 | 94.3 | 75.9 | 56.0 | 1.30 | 1.19 | 7.5 |
| 48 | xaac | k-1 | 697 | 65.7 | 65.7 | 13.5 | 5.3 | 7.60 | 7.17 | 40.7 |
