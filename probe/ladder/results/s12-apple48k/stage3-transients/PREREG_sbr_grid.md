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
