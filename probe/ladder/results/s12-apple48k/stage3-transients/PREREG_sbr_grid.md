# S12 Stage 3 prereg: onset-adaptive SBR time grid for HE (written before the arm exists)

Design and evidence: `SBR_GRID_DESIGN.md`, `decisions/sbr_grid_he.md`, `decisions/he_nper_bands.md`, the closed
attack-border arm (`PREREG_sbr_attack.md` addendum). Scope: HE-v1 and HE-v2 SBR grid only; LC byte-exact; core
untouched. Env-gated scratch patch `FAAC_SBR_GRID=1`, unset = master payload-identical.

## Arm
1. Detector: per QMF time slot, high-band energy (the SBR range of the analysis QMF) compared against a running mean of
   the previous 8 slots carried across frames; a slot is an onset when its energy exceeds T x mean; the onset slot is
   moved by a fixed lead correction L slots. T and L are the only constants.
2. Grid: frames with an onset get a variable-border class (VARFIX for an onset early in the frame, FIXVAR late, VARVAR
   when the previous frame's trailing border must carry over) with a border at the corrected onset slot; other frames
   keep master's FIXFIX choice. Writers follow ISO 14496-3 grid syntax (envelope counts, pointer width, border
   ordering, noise-floor borders, leading border = previous trailing border - numTimeSlots); validated against the
   syntax limits, not against decoder tolerance. FIXVAR/VARVAR writers may be reused from the S11 grid-probe patch where
   still correct against current `sbr_bitstream.c`.

## Calibration (disjoint from evaluation)
The 77 clips are split by alphabetical index: `sbr_calib_clips.txt` (odd positions, 39 clips) and
`sbr_eval_clips.txt` (even positions, 38 clips). T in {2, 3, 4, 6, 8} and L in {0, 1, 2, 3} are chosen on the
calibration half only, HE-v1 40k: the (T, L) that maximizes the share of scored onsets with a border within 2 slots,
subject to at most 40 % of all frames being non-FIXFIX. The chosen (T, L) are frozen in an addendum before any
evaluation-half grid or any MOS/NPER is read.

## Pre-scoring gates (evaluation half, HE-v1 32/40/48 and HE-v2 24/32/48)
- border within 2 slots of >= 70 % of scored onsets and within 1 slot of >= 60 %; FIXFIX at onset frames <= 15 %;
- LC payload byte-exact vs master; unset payload-identical in every mode;
- strict decode clean (exit 0, empty stderr) on FAAD2, in-tree faad `--strict`, FFmpeg `-xerror`, fdkdec over streams
  that contain every frame-class transition (FIXFIX/VARFIX/FIXVAR/VARVAR in all orders);
- footprint and instruction deltas reported with any offsets found (owner rule: no net growth).
Failing any gate stops the arm before scoring.

## Scoring (only after the gates pass)
HE-v1 CBR 24/32/40/48 and HE-v2 CBR 24/32/48, 77 clips, three decoders. Reading order: worst clip x rung MOS vs master
(gate -0.10); pooled NPER per rung with clip-bootstrap CI of the difference (full band and > 12 kHz); P1, mean MOS,
stereo fidelity; BD-rate vs master (PCHIP and linear). Advance to the full-dataset pass if NPER CI is below 0 at >= 2
HE-v1 rungs under all three decoders, no cell below -0.10, mean MOS not lower by more than 0.005 and P1 not lower at
any rung (HE-v1 and HE-v2). Results are also split by calibration and evaluation half.


## Addendum 2026-11-17 (calibration half only, before any evaluation-half clip, grid, MOS or NPER was read)

Patch `scripts/s12/sbr_grid_scratch.patch` (against origin/master 3a656f7), runner `scripts/s12/sbrgrid_run.py`, metrics
`scripts/s12/sbrgrid_metrics.py` (same conventions as `sbr_grid_analysis.py`: onsets merged within 64 samples, frame
k = floor((onset + lag - 256) / 2048), position = remainder / 128 slots, near-N = any onset channel's interior border within N
slots), table `scripts/s12/sbrgrid_calib_table.py`. 39 calibration clips, 429 scored onsets, HE-v1 40k CBR, decoded grids from the
instrumented libfaad (ADTS remux, so the codec delay is kept: xcorr lag 3042 for all 39 streams, as in `sbr_grid_he.md`).

Interpretation choices the prereg leaves open (fixed before the table was read, not tuned): the detector sums the QMF slot energy
over the SBR range [kx, k2) per analysis slot (64 samples, 32 per frame); a flagged run counts as one onset (its first slot); when a
frame has several onsets the one with the largest energy / mean ratio gets the border (the prereg grid has one border per
frame); L is in SBR time slots (128 samples), added to the detected slot (the QMF analysis sees an attack before the slot its energy
lands in) and clamped to slot 15; the border is the reachable position nearest to the corrected slot (VARFIX: lead + 2..8,
FIXVAR/VARVAR: trail - 2..8, trail 16 or 17), ties to the earlier border, then to the class with no trailing border; frames
without an onset keep master's FIXFIX (or, after a trailing border, a VARFIX from the carried lead border); the transient pointer
stays 0. Master's frame-strength test is not used in the arm. A first-onset rule instead of the strongest was also run on the
calibration half (T in {4,6,8}, L in {0,1,2}): within 2 slots 50.8-55.2 % against 54.8-57.8 % for strongest at the same cells;
strongest was kept.

Void first pass, recorded for the audit: the first calibration run read caches decoded from the .m4a (the edit list hides the
codec delay, lag 0), so its alignment was wrong and its within-2 shares (31-51 %) are chance level; it was discarded and rerun
after the lag was restored. The alignment re-check on the valid run peaks at delta = -256 samples again (T6 L1: within 2 slots
at delta -384 / -256 / -128 / 0 = 52.4 / 57.8 / 55.2 / 42.7 %), so delta = -256 is kept.

Calibration table (valid run):

| T | L | onsets | within 2 % | within 1 % | FIXFIX at onset % | non-FIXFIX frames % |
|---|---|---|---|---|---|---|
| 2 | 0 | 429 | 52.68 | 35.20 | 0.00 | 64.76 |
| 2 | 1 | 429 | 55.24 | 46.85 | 0.00 | 65.91 |
| 2 | 2 | 429 | 55.01 | 29.84 | 0.00 | 66.54 |
| 2 | 3 | 429 | 34.27 | 14.69 | 0.00 | 67.62 |
| 3 | 0 | 429 | 53.15 | 36.13 | 0.00 | 42.75 |
| 3 | 1 | 429 | 55.48 | 46.85 | 0.00 | 43.62 |
| 3 | 2 | 429 | 53.85 | 29.14 | 0.00 | 44.72 |
| 3 | 3 | 429 | 33.10 | 14.22 | 0.00 | 45.48 |
| 4 | 0 | 429 | 55.01 | 37.53 | 0.00 | 33.90 |
| 4 | 1 | 429 | 56.64 | 48.02 | 0.00 | 34.86 |
| 4 | 2 | 429 | 54.78 | 27.74 | 0.00 | 35.71 |
| 4 | 3 | 429 | 32.40 | 14.22 | 0.00 | 36.75 |
| 6 | 0 | 429 | 55.94 | 38.46 | 0.23 | 25.89 |
| 6 | 1 | 429 | 57.81 | 48.25 | 0.23 | 26.61 |
| 6 | 2 | 429 | 55.48 | 27.51 | 0.23 | 27.30 |
| 6 | 3 | 429 | 34.03 | 14.22 | 0.23 | 28.28 |
| 8 | 0 | 429 | 55.94 | 38.00 | 0.93 | 21.36 |
| 8 | 1 | 429 | 56.88 | 46.85 | 1.17 | 22.10 |
| 8 | 2 | 429 | 55.48 | 26.81 | 0.93 | 22.62 |
| 8 | 3 | 429 | 31.24 | 13.29 | 0.93 | 23.59 |


Rule (max within-2 subject to non-FIXFIX <= 40 % of all frames; T = 2 and 3 are over 40 %; ties would go to fewer non-FIXFIX):
**frozen (T, L) = (6, 1)**: within 2 slots 57.81 %, within 1 slot 48.25 %, FIXFIX at onset 0.23 %, non-FIXFIX 26.61 % of frames
(VARFIX 5478, FIXVAR 3286, VARVAR 408 of 34470 frame-channels). These are the constants for the evaluation half.
Calibration-half onset gates for reference (not the gate itself): within 2 slots 57.8 % < 70 %, within 1 slot 48.3 % < 60 %.


## Addendum 2026-11-17 (pre-scoring gates, evaluation half; no MOS/NPER read)
Details and tables: `scripts/s12/SBRGRID_VALIDATION.md`. Frozen (T, L) = (6, 1), HE-v1 CBR 32/40/48 and HE-v2 CBR 24/32/48,
38 evaluation clips, 268 scored onsets: border within 2 slots 86.2-91.0 % (>= 70), within 1 slot 64.6-72.8 % (>= 60), FIXFIX at
onset 0.7-2.6 % (<= 15). LC byte-exact vs master 54/54, unset payload-identical 288/288, strict decode 228/228 streams clean on
FAAD2, in-tree faad --strict, ffmpeg -xerror and fdkdec, 0 grid-syntax violations. Onset gates, LC and decode gates pass. Footprint
rule not met: LTO .text+.rodata+.data +2396 B net after offsets (+5392 B before); Ir +1.32 % unset, +2.61 % set (10 s HE-v1 32k).
Scoring stage is the owner's call on the footprint.
