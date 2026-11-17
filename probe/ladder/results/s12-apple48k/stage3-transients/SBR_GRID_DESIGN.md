# Onset-aligned SBR grid for FAAC HE-v1: design note (no code, no scoring)

Status: design only. Inputs are existing dumps (`decisions/sbr_grid_he.md`, `he_nper_*.md`), `PREREG_sbr_attack.md` (closed arm), S11 archive, master 3a656f7 and the in-tree decoder. Reference encoders enter only through their measured decision statistics.

## 1. What the reference grids do at onsets (697 scored onsets, 77 clips, delta = -256 samples)
| | FIXFIX share, all frames | env=1 / 2 / 3+4 | FIXFIX at onset frame | interior border within 2 / 1 slots |
|---|---|---|---|---|
| FAAC 32k | 73 % (rest VARFIX, border 8) | 73 / 27 / 0 | 28 % | 18.7 / 8.2 % |
| FAAC 40, 48k | 97.7 % (4 env) | 0 / 2 / 98 | 82 % | 64.7 / 30.7 % (chance level of 0-4-8-12-16) |
| Apple 32 / 40 / 48k | 54 / 64 / 64 % | 24-50 / 31-51 / 19-24 | 3-4 % | 80.5-86.5 / 68-76 % |
| FDK (all rungs) | 62 % | 34-40 / 40-45 / 20 | 5 % | 87.1 / 74.0 % |
| libxaac (all rungs) | 70 % | 49-53 / 31-36 / 15 | 7 % | 84-85 / 75 % |

References: VAR classes split about 15 % FIXVAR, 15 % VARVAR-or-VARFIX each, VARVAR 4-8 %; mean 1.9 envelopes per frame; at an onset 93-97 % of frames are non-FIXFIX with a border 1.1-1.3 slots (mean) from the onset. They spend borders only where an onset is and use 1 envelope elsewhere; FAAC spends 4 envelopes everywhere and places none. The previous frame (k-1) matters little (border near onset 18-20 %), i.e. the grid is frame-local. All three references peak at the same alignment (SBR grid leads the xcorr lag by 2 slots); FAAC has no peak of its own, so its alignment must be verified, not assumed.
HE pre-echo (`he_nper_bands.md`): FAAC excess at >12 kHz is 11.7-13.8 dB vs 6.1-9.3 (FDK/Apple); at 40/48k the 6-12 kHz band (FAAC crossover 8.25 kHz) is 19.0-19.8 vs 14.1-15.5. At 32k (crossover 11.6 kHz) 2-12 kHz is core, so the grid can only act on the >12 kHz band there. Expect the grid to fix the SBR part of NPER only.

## 2. Detector change
Master: one scalar per frame, `smax * n / (sum + floor)` of full-band 64-sample slot energy, VARFIX-at-slot-8 if > 4.0 (8.0 when `rich`); the argmax is thrown away. Result: 27 % flagged at 32k, 2.3 % at 40/48k, versus reference non-FIXFIX shares of 30-46 %.
Proposed (time-domain, no extra QMF pass): per channel, per analysis slot s, e(s) from the 1st-difference (high-passed) signal so LF energy does not mask HF onsets; onset(s) = e(s) > T * mean(e(s-8..s-1)) and e(s) > floor, with the 8-slot history carried across frames (`qmfOvl64` already holds the samples). Output a list of onset time slots (0-15 plus trailing region), not a frame flag; position = first crossing, then refined to the local maximum within 2 analysis slots. T basis: calibrate offline on `onsets_per_onset.jsonl`, choosing the T whose flagged-frame share is 30-40 % (reference level) at the best recall of the onset list within 1 time slot; one T per rich/non-rich setting, no per-clip tuning. Apply the measured +2-slot lead (argmax was 1-2 slots early on real clips in the closed arm, floor-to-even added to the miss).

## 3. Writer and frame classes
| field | master `put_grid` | needed |
|---|---|---|
| FIXFIX (1/2/4 env, freq_res) | yes | keep (stationary frames, tier17 count) |
| VARFIX: var_bord_0 (2 b), num_rel (2 b), rel x(L_E-1), pointer, freq_res x L_E | only lead 0, L_E=2, pointer 2 b fixed 0 | lead 0-3, L_E 1-4, pointer width ceil_log2(L_E+1) (decoder table {1,2,2,3}) |
| FIXVAR: end = 16+d (d 0-3), num_rel, rel from the end, pointer, freq_res (reverse order) | no | add; reaches late borders 8..14+d with 2 env |
| VARVAR: lead, trail, num_rel_0, num_rel_1, rels, pointer, freq_res | no | add; border before and after an onset (3 env) |
Decoder facts (libfaad `sbr_read_grid`): rel = 2*bs_rel_bord+2; t_E[i+1] > t_E[i], t_E <= 19; HF is generated from 2*t_E[0] to 2*t_E[L_E] (+ T_HFADJ), so a lead border > 0 leaves slots 0..t_E[0] without HF unless the previous frame ended past 16 (`t_E_end_prev`). Rule for the encoder: next lead = previous trail - 16 (tiling); after a trail d>0 the next frame cannot be FIXFIX (use VARFIX/VARVAR with lead d). Trail d needs d slots (<= 384 samples) of lookahead for envelope energies; check against the existing SBR payload FIFO (payload is already one frame behind the core). Pointer: 0 gives l_A = -1 and the noise border t_Q[1] follows t_E[L_E/2]; pointer p>0 marks the transient envelope (no smoothing in the decoder) - an option to test, not required. Noise floor: `put_channels` already writes n_q = 2 whenever L_E > 1, constants, borders decoder-derived; no change beyond L_E up to 4. Delta coding: time deltas reference the last envelope of the previous frame, so any grid is legal; `eff_amp_res` 0 for one envelope vs `SBR_AMP_RES` otherwise affects the `linked` test only.

## 4. Exists / to add, by file (master)
| file | exists | add |
|---|---|---|
| `sbr_analysis.c` (175 L) | frame strength loop, tEnv/envSampled/env_border, bandE per envelope (accepts any tEnv, L_E<=4) | per-slot energy + onset list; grid builder choosing class and borders (about 60-80 L); previous-trail memory |
| `sbr.c` (528 L) | `sbr_adopt_envelope_grid` copies class and tEnv; quantizer generic in L_E | carry trail d / lead in `SbrFrameData`; `numEnvFixFix` stays for stationary frames |
| `sbr_bitstream.c` (306 L) | `put_grid`, `put_channels` generic in n_env | FIXVAR/VARVAR branches, variable pointer width (about 25 L); `SbrCount` path unchanged (shared writer) |
| `sbr.h` / `sbr_internal.h` | class enum has all four; `SBR_MAX_ENVELOPES` 4 | thresholds, trail field; no ROM |
| `ps*.c` | PS path binning uses its own 4 phases | HE-v2 reads the same grid: re-check, or gate off for v2 |
S11 `raw/grid/grid-probe.patch` (466 L) already contains FIXVAR/VARVAR writers with the pointer-bit loop; reuse as reference, not as a base (it carries low-resolution code too).

## 5. Footprint and instructions (rough)
Closed attack arm: about 15 L changed, +160 B (LTO .text+.rodata+.data), +0.03 % Ir. This design: about 110-140 new/changed lines, so about +1.0-1.4 KB text, no ROM, stack +128 B (32 floats) and +16 B per channel state. Ir: detector adds one pass over the time-domain samples (differences) at about 2 flops/sample: +0.1-0.3 % total Ir (SbrAnalyze is 19 % of the total, 29.0 of 152.5 M Ir on a 10 s clip). Do not add a second QMF pass or dry bit scoring (S11 mode1: about +8 KiB/channel slotBandE and a second SbrWrite per candidate). Bit cost of more 2-3 envelope frames is offset by 1-envelope stationary frames only if the tier17 count policy is revisited: measure payload bytes per frame class before scoring.

## 6. S11 history and what differs now
S11 "mode1" (`FAAC_SBR_BORDER_MODE=1`): VARFIX only, 4 candidate borders {2,4,6,8} plus the original, each requantised from a second QMF pass (`slotBandE`), objective = log-energy distortion + lambda (0.001 or 0.003) * bits from a dry `SbrWrite` run, min over candidates; lead 0, L_E 2. Recoverable: yes, as patches, not as a branch commit: `raw/transient-border/upstream098-mode1-gate/faac-upstream098-adaptive-border.patch` (436 L, on 098e890) and older `raw/adaptive/adaptive-probe.patch` (473 L); scripts under `raw/transient-border/tools`; probes were archived in the "never merge" docs commits (66599361, 9ab431ce). Results (MOS BD-rate): fixed8 -0.51 % vs merged baseline (this is master's border 8); mode1 -0.64 / -0.66 % vs baseline, -0.13 % [-0.33, +0.06] vs fixed8 (not resolved); late FIXVAR +0.59 % [+0.28, +0.94] (worse); center VARVAR -0.08 % (n.s.); cross-frame mode2 no gain over tier17. Pre-echo and border position were never measured; selection used a distortion proxy, not onset position, and borders were limited to VARFIX 2..8, i.e. most onsets stayed unreachable (cf. the attack arm). Now: (a) judge on NPER first (full band, >12 kHz, per band), MOS second; (b) validate border-to-onset share offline before any scoring; (c) deterministic, onset-driven rule (no RD selection, no second pass); (d) full class set so late and mid onsets are reachable.

## 7. Validation plan (before any MOS scoring; light CPU, `nice`)
1. Detector offline: dump per-frame onset slot from the encoder (env-gated), compare to `onsets_per_onset.jsonl`: recall within 1 and 2 time slots, flagged-frame share per rung; pick T on 6-clip subset, freeze, then report on all 77.
2. Alignment: with the decoder dump (`libfaad_decdump_sbrgrid.patch`) recompute the delta curve (`sbr_grid_align_curves.json` method) for the new FAAC streams; require a peak near -256 like the references, plus the synthetic burst test (bursts at all 32 analysis-slot offsets over frame boundaries, border lands 0-2 slots before the burst).
3. Gate: interior border within 2 slots of scored onsets >= 70 % on the 77 clips at 32, 40, 48k (master 18.7 / 64.7 / 64.7; 40/48k chance is already 65 %, so also report within 1 slot, aim >= 60 %, and FIXFIX share at onset frames <= 15 %); VAR share of frames 25-45 %.
4. Integrity: LC bit-exact vs master (md5 of ADTS payload, the 261-case matrix of `SBRATK_VALIDATION.md` with the gate unset or HE off); HE decodes strict-clean on FAAD, in-tree FAAD3 `--strict`, `ffmpeg -xerror`, FDK with equal PCM length; exercise every class transition (trail d>0 followed by each class, mono, stereo, HE-v2).
5. Only then: NPER (clip-bootstrap CI, full band and >12 kHz) with the PREREG_sbr_attack.md advance rule, then MOS.
