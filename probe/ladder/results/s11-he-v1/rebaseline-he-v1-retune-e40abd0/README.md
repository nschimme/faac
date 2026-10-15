# Re-baseline: nschimme/faac `he-v1-retune` (e40abd0) vs Apple and FDK, HE 48 kHz stereo 40/48/56/64 kbps

Same 49 clips, rungs, scorer and BD method as `../scoreboard-refresh-098e890`; encoder = `he-v1-retune` e40abd0 built
with meson release (`--object-type he-aac-v1 -b N`, ABR). Reference streams: Apple (packaged) and fdkaac 1.0.9/libfdk 2.0.0 (built here).
Per the maintainer, `he-v1-retune` is likely to merge, so it becomes the base for further experiments. 196 jobs, 0 invalid.
What the branch does (from its diff): start frequency 15 -> 12 from 20 kbps/ch, transient threshold 4 -> 8, inverse-filter mode 3 -> 1,
stop-frequency capped so k2/kx stays under 2.2449 (keeps the one-region master table), narrower stop target below 13 kbps/ch.

## BD-rate, mean [95 % CI] (negative = FAAC needs fewer bits for equal MOS)
| comparison | FAAD2 decode | FDK-decoder (`fdkdec`) |
|---|---|---|
| baseline 098e8900 vs FDK | +6.46 % [+2.70, +10.23] | +4.70 % [+1.32, +8.12] |
| baseline 098e8900 vs Apple | +4.95 % [+0.50, +9.54] | +3.49 % [-0.39, +7.57] |
| **retune vs FDK** | **-4.37 % [-7.41, -0.85]** | -0.81 % [-3.83, +2.64] |
| **retune vs Apple** | **-4.10 % [-8.29, +0.53]** | -2.02 % [-5.33, +1.43] |
| retune vs 098e8900 | -11.02 % [-12.63, -9.39] | -5.94 % [-7.79, -4.12] |

Mean MOS (FAAD2): 40k retune 4.172 / FDK 4.147 / Apple 4.136; 64k 4.567 / 4.527 / 4.545. FDK-decoder: 40k 4.119/4.114/4.112; 64k 4.515/4.515/4.531.

## Reading
- retune is ahead of both references when scored with FAAD2, and at statistical parity (CIs include 0) with the independent FDK decoder.
  Its gain over the old baseline is significant under both decoders (-11 % / -5.9 %). "Beating" Apple and FDK is therefore not yet
  established independently of the decoder: the honest status is parity to a small lead. Apple's decoder, music held-out clips,
  44.1/32 kHz references and VBR/CBR were not run here (no Apple/FDK reference streams exist for those).
- The earlier standalone finding (crossover 9-11 beats 15) is in line with what retune adopted (start 12, from 20 kbps/ch);
  retune keeps one region by capping the stop frequency, which my own work suggested trades away the top of the SBR range.
  Whether a spec two-region master table (branch sbr-two-region-master-table, 51c589e) plus a lower start (9-11) beats retune is
  untested and is the next experiment on this base. Pinned FAAD2 287c00c fails to decode a number of start indices even with a
  spec-conformant table (see `../crossover-per-rung-098e890/RESULTS.md`), so any such arm must be gated on a decoder-agreement check.
