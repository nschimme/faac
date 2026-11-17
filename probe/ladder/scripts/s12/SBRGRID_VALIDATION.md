# S12 Stage 3 arm: onset-adaptive SBR time grid (HE), scratch patch validation

Patch: `sbr_grid_scratch.patch` (against origin/master 3a656f7; applies cleanly, 5 files in `libfaac/`). Worktree
`/home/user/work/sbrgridwt` (`/home/user/work/sbrgrid` is the older grid-analysis cache dir), builds `build` (release,
`-Dencoder-ps=true`), `build-lto-ps` (explicit `-Db_lto=true`). Master reference: `/home/user/work/faac-master/build-ps` (same
3a656f7, PS on). Spec: `results/s12-apple48k/stage3-transients/PREREG_sbr_grid.md` (+ calibration addendum), design
`SBR_GRID_DESIGN.md`. No MOS / NPER / Zimtohrli scoring was run.

Gate: `FAAC_SBR_GRID=1` (HE only; read once in `SbrInit`); `FAAC_SBR_T`, `FAAC_SBR_L` override the two constants (defaults are
the frozen T = 6, L = 1). Unset: payload-identical to master.

## What the patch does
- `SbrAnalyze` now stores the QMF energy of every slot (`slotE`, 34 slots: 32 plus 2 of lookahead for a trailing border) and
  bins it into the grid's envelopes afterwards (`env_border` = `tEnv * slots / 16` for every envelope, so a leading border
  above 0 or a trailing one past 16 leaves out / takes in the slots the neighbouring frame's envelope covers). HE-v2 stores the
  mono carrier's slot energy the same way; PS statistics stay on the frame's 32 slots.
- Detector (arm only): per slot, high-band energy = sum of the slot energy over [kx, k2); flagged when it exceeds T x mean of the
  previous 8 slots (history carried across frames) and the previous slot was not flagged (a run is one onset); the strongest
  onset of the frame (largest ratio, any channel) gets the border; slot -> time slot = slot / 2, plus L, clamped to 15.
- Grid: VARFIX (lead + 2..8), FIXVAR (trail - 2..8) or, when the previous frame ran past 16, VARVAR (lead = carried border,
  backward border); the reachable border nearest the corrected slot wins, ties to the earlier one, then to the class without a
  trailing border; trail 0 or 1. No onset: master's FIXFIX; after a trailing border a VARFIX opened at the carried lead, `numEnvFixFix`
  envelopes in even steps. The flush (silence) frame after a trailing border is a one-envelope VARFIX from the lead.
- Writer `put_grid`: all four classes per ISO 14496-3 4.6.18.3.3 (lead, trail, `bs_num_rel_0/1`, 2-bit even-step relative borders
  forward / backward, pointer 0 of width ceil(log2(num_env + 1)), one high-resolution flag per envelope). `eff_amp_res` is 0
  only for a one-envelope FIXFIX (the decoder's rule; master's `numEnvelopes == 1` test was equivalent only because VARFIX
  always had two envelopes).
- Checked against libfaad `sbr_read_grid` (envelope limit, `t_E` strictly increasing and <= 19, pointer <= L_E, pointer width table
  {1, 2, 2, 3}, noise floor borders from the pointer: pointer 0 -> FIXVAR/VARVAR `t_Q[1] = t_E[L_E-1]`, VARFIX `t_E[1]`,
  `L_Q = 2` for L_E > 1, which is what `put_channels` already writes) and by `sbrgrid_check.py` on every decoded grid
  (below).

## Calibration (calibration half, HE-v1 40k CBR, 39 clips, 429 scored onsets)
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


Chosen by the prereg rule: **(T, L) = (6, 1)** (max within-2 subject to non-FIXFIX <= 40 %): within 2 slots 57.81 %, within 1 slot
48.25 %. Alignment re-verified: lag 3042 on all 39 streams, within-2 peaks at delta -256 samples (52.4 / 57.8 / 55.2 / 42.7 % at
-384 / -256 / -128 / 0). The calibration half is dominated by one clip: `13 fatboy_30sec` holds 312 of the 429 onsets
(dense, 43 % within 2 at T6 L1); without it 117 onsets, 97.4 % within 2, 82.1 % within 1. The (T, L) choice is therefore a
fatboy-weighted pick; the evaluation half has no such clip (largest: `Changes` 102 of 268 onsets, 77 %). A first-onset rule
(instead of strongest) was run on T in {4, 6, 8} x L in {0, 1, 2}: 50.8-55.2 % within 2 vs 54.8-57.8 % strongest. A first
calibration pass read the decode of the .m4a (edit list hides the codec delay, lag 0): void, discarded, rerun (see the prereg
addendum).

## Pre-scoring gates (evaluation half, 38 clips, 268 scored onsets, CBR, constants frozen)
| stream | within 2 (>= 70) | within 1 (>= 60) | FIXFIX at onset (<= 15) | non-FIXFIX frames | FIXFIX / FIXVAR / VARFIX / VARVAR (frame-channels) |
|---|---|---|---|---|---|
| HE-v1 32k | 86.2 | 68.3 | 1.5 | 25.2 | 26562 / 3350 / 5300 / 300 |
| HE-v1 40k | 89.6 | 72.8 | 1.9 | 22.6 | 27492 / 3004 / 4780 / 236 |
| HE-v1 48k | 89.6 | 72.8 | 1.9 | 22.6 | 27492 / 3004 / 4780 / 236 |
| HE-v2 24k | 89.9 | 64.6 | 0.7 | 20.6 | 14102 / 1371 / 2225 / 61 |
| HE-v2 32k | 89.9 | 64.6 | 0.7 | 20.6 | 14102 / 1371 / 2225 / 61 |
| HE-v2 48k | 91.0 | 69.0 | 2.6 | 18.3 | 14517 / 1244 / 1954 / 44 |
(Percent. 40k and 48k are identical streams in grid terms, as are v2 24k and 32k: the same band/rich settings.) Onset gates:
**pass** at every rung. The master share was 18.7 % (32k) / 64.7 % (40/48k) within 2 (`sbr_grid_he.md`). Grid share of non-FIXFIX frames
is 18-25 %, below the design note's 25-45 % aim (not a prereg gate).

| gate | result |
|---|---|
| LC payload byte-exact vs master, LC ABR 96 / CBR 96 / VBR q100, `FAAC_SBR_GRID=1` | **pass** 54 / 54 (`ffmpeg -map 0:a -c copy -f md5`) |
| unset payload-identical to master, every mode | **pass** 288 / 288: 3 clips x 48 / 44.1 / 32 kHz x stereo / mono x LC (3), HE-v1 ABR 24/32/48/64, CBR 24/32/48/64, VBR q20/q60, HE-v2 CBR 24/32/48 (`sbrgrid_matrix.sh`; the v2 mono rows ran as well and matched) |
| strict decode, HE-v1 32/40/48 and HE-v2 24/32/48 CBR, 38 eval clips = 228 streams | **pass** 228 / 228 clean (exit 0, empty stderr) on FAAD2 287c00c, in-tree `faad --strict`, `ffmpeg -v error -xerror`, fdkdec; decoded sample count equal to master's in 228 / 228 (`sbrgrid_strict.sh`) |
| grid syntax on all decoded E records (`sbrgrid_check.py`) | **pass** 0 violations: `num_env` <= 4, `t_E` strictly increasing and <= 19, and `t_E[0] = t_E_prev[last] - 16` (0 after a frame ending at 16), so no FIXFIX / FIXVAR frame follows a trailing border |
| frame-class transitions observed (all six eval streams, ordered pairs, frame-channels) | FIXFIX>FIXFIX 102320, FIXFIX>FIXVAR 11014, FIXFIX>VARFIX 10708, FIXVAR>FIXFIX 3817, FIXVAR>FIXVAR 494, FIXVAR>VARFIX 8094, FIXVAR>VARVAR 875, VARFIX>FIXFIX 17547, VARFIX>FIXVAR 1750, VARFIX>VARFIX 1919, VARVAR>FIXFIX 241, VARVAR>FIXVAR 86, VARVAR>VARFIX 543, VARVAR>VARVAR 63 |

Transitions FIXFIX>VARVAR and VARFIX>VARVAR are not produced by the rule (VARVAR is used only to open from a carried trailing
border; a frame after FIXFIX / VARFIX has lead 0, where FIXVAR is the same grid). They are legal syntax, so they were tested
with a throwaway build (not in the patch) that codes the backward-border grid as VARVAR with lead 0: both transitions occur
(FIXFIX>VARVAR 684, VARFIX>VARVAR 168 on 8 clips), grids pass the checker, 16 streams (HE-v1 32/48) strict-clean on all four
decoders.

## Cost
- Instructions, `09 Linchpin` 10 s, HE-v1 32k CBR, callgrind total Ir (release build, as in `SBRATK_VALIDATION.md`): master
  152,682,212; patch unset 154,693,072 (+1.32 %); patch `FAAC_SBR_GRID=1` 156,664,339 (+2.61 %). `SbrAnalyze` plus the QMF
  transform: 29.05 M master vs about 31.8 M. The unset cost is the store-then-bin energy path (every HE encode pays it) and the
  now out-of-line `qmf_transform` call; the set mode adds two lookahead slots and the detector.
- Memory: +8.7 KB per SBR channel (34 x 64 slot energies) + 0.2 KB detector state, heap, no ROM.
- Footprint, `libfaac.so` `size -A`, `-Db_lto=true -Dencoder-ps=true`, bytes:

| | .text | .rodata | .data | sum |
|---|---|---|---|---|
| master 3a656f7 | 96032 | 11484 | 1184 | 108700 |
| patch | 98352 | 11560 | 1184 | 111096 |
| net | +2320 | +76 | 0 | **+2396 (+2.2 %)** |

**The owner rule (no net growth) is not met.** Offsets tried, in order (LTO .text after each): first version +5312 (101344); bin whole
64-wide rows instead of [kx, k2) (-1040, fixed trip counts vectorize compactly); per-slot high-band sum in the QMF loop so the
detector loop has no inner loop, and a direct nearest-step border computation instead of the 3 x 4 candidate search (-1020);
`qmf_transform` out of line (-910: it was inlined into both `SbrQmfAnalysis` and `SbrQmfAnalysisComplex`; costs call overhead,
which is part of the Ir above); `put_grid` compaction (pointer + flags in one `PutBit`): 0. Tried and reverted: single-loop
binning (+352), narrower `memset` of the slot rows (more Ir, no size gain). Remaining growth: `SbrAnalyze` +995 plus the new
`qmf_transform` 651, `BuildFrame` (`put_grid` inlined) +399, `faacEncEncode` +148 (flush frame), `faac_encoder_open` +115 (env read).
The detector loop alone is about 830 B uncompiled-out of the arm (LTO fully unrolls its 32 slots). No further offset elsewhere in
libfaac was identified within this arm's scope.

## Notes
- Pointer stays 0 (decoder keeps its smoothing across the attack envelope); a transient pointer is an option, not tested.
- The patch requires `FAAC_SBR_DECIMATION == 1` for the arm (default); otherwise the env gate is ignored.
- Scripts: `sbrgrid_run.py` (encode, ADTS remux, dump, lag), `sbrgrid_metrics.py`, `sbrgrid_check.py`, `sbrgrid_calib.sh`,
  `sbrgrid_calib_table.py`, `sbrgrid_gate_run.sh`, `sbrgrid_matrix.sh`, `sbrgrid_strict.sh`; decoder: `/home/user/work/faad-dump`
  (`libfaad_decdump.patch` + `libfaad_decdump_sbrgrid.patch`, unchanged).
- Verdict at the pre-scoring level: all prereg gates pass on the evaluation half; the footprint rule fails (+2.4 KB). Scoring is
  not run here.
