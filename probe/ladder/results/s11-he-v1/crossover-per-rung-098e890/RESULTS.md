# Per-rung SBR crossover screen: results (49-clip screen, NOT confirmation)

Preregistered in `PREREG.md`. Arms built from frozen baseline 098e8900; `probe_fix_plus_env.patch` is the
cherry-picked table fix (51c589e) plus the probe env knob `FAAC_PROBE_SBR_START`. Control (fix, start 15) is
payload-identical to baseline on 3 clips x 4 rungs. 1372 jobs, 0 invalid. Same decoder/scorer/BD method as the scoreboard.

BD-rate vs control (negative = fewer bits at equal MOS), clip-bootstrap 95% CI:

| arm | n | BD-rate | 95% CI |
|---|---|---|---|
| fix13 | 49 | -2.26 % | [-4.14, -0.36] |
| fix11 | 49 | -4.60 % | [-7.54, -0.85] |
| fix9  | 48 | **-6.10 %** | [-8.50, -3.50] |
| fix7  | 47 | -1.03 % | [-5.33, +3.85] |
| nofix11 | 49 | -4.37 % | [-7.35, -0.66] |
| nofix7 | 46 | +5.87 % | [+1.64, +10.50] |

Per-rung paired MOS delta (mean, wins/losses, rate delta about -0.1 to -0.3 kbps, i.e. no bits bought):
fix11 +0.050/+0.058/+0.051/+0.047 at 40/48/56/64 (34-39 wins of 49, all CIs exclude 0);
fix9 +0.053/+0.061/+0.047/+0.039 (CIs exclude 0 at every rung); fix7 is neutral to negative
(-0.048 at 64k). So the effect is not rung-specific: 9 and 11 both help at all four rungs and the
earlier "static for all rungs" explanation is not what separates this from past MOS drops.

Table fix: at start 11 it makes no difference (-4.60 vs -4.37, overlapping CIs). At start 7 the
baseline table loses (+5.87 %) while the fixed table is neutral (-1.03 %), a difference of about
7 points. So the table bug is real and explains lost MOS at very low crossovers, but the gain at
9-11 does not depend on the fix. Earlier drops likely came from crossovers at or below ~7 (bug) and/or
from older encoder states.

Limits: selection screen on the same 49 clips that define the gap; the 9 vs 11 vs 7 choice is
post-hoc and the pooled-ladder BD-rate is not a per-rung optimum. The PREREG rule requires held-out
confirmation (below) before any claim. Not shown: perceptual agreement with a listening test, other
decoders (FDK/ffmpeg) decoding these streams identically, CPU/footprint cost (none expected: one constant).

## Held-out confirmation (PREREG amendment) and decoder cross-check
Held-out = 50 TCD-VoIP clean speech references (frozen rule in `PREREG.md`), dual-mono 48 kHz stereo; 800 jobs, 0 invalid.
BD-rate vs control (FAAD2 decode / FFmpeg `N`-series 6.1.1 decode):
- fix11: -14.05 % [-14.42, -13.66] / -14.07 % [-14.43, -13.71]; per-rung MOS delta +0.017/+0.041/+0.057/+0.040 (FAAD2).
- fix9:  -18.62 % [-19.11, -18.12] / -18.65 % [-19.14, -18.15]; per-rung +0.030/+0.062/+0.072/+0.031 (FAAD2).
Success criterion (CI excludes 0 favourably and per-rung delta > 0 at >= 3/4 rungs) is met. Caveats: speech-only and dual-mono,
whose ABR output undershoots the nominal rate (control 36.9 kbps at "40"); lower-crossover arms spend 4-6 kbps less at higher
MOS here, so the -14/-19 % is far larger than on music and should not be extrapolated. Tight CIs reflect a homogeneous clip set.

Decoder cross-check on the 49 music clips, same encoded streams scored after FFmpeg decode: fix11 -6.03 % [-8.22, -3.78],
fix9 -5.75 % [-8.40, -2.93] (FAAD2: -4.60 / -6.10). The gain is not a FAAD2 artefact.

### bs_start_freq 10 is a FAAD2 decode failure, not an encoder defect
At 48 kHz start 10 gives kx 18 and a two-region table. FAAD2 (287c00c) decodes the streams to MOS 1.30 (cst) / 1.91 (SinceAlways),
FFmpeg to 3.96 / 4.32 (control 4.03 / 4.27). On the held-out set fix10 scored about 1.97 under FAAD2 (excluded from the
BD table). FAAD2's `master_frequency_table` (libfaad/sbr_fbt.c) differs from the spec algorithm the branch implements:
it applies the whole `change` to vDk1[0] without the (vDk1[last]-vDk1[0])/2 cap, and sorts `nrBand1+1` entries first.
Starts 9 and 11 agree between decoders. Any production choice must therefore avoid start 10 (or FAAD2 must be fixed),
and be re-validated per sample rate: kx differs at 32/44.1/48 kHz.

### Gap to the references on the 49 clips (FAAD2 decode, mean BD-rate [95 % CI])
| arm | vs FDK | vs Apple |
|---|---|---|
| baseline | +6.46 % [+2.70, +10.23] | +4.95 % [+0.46, +9.60] |
| start 11 | +1.19 % [-2.20, +5.15] | +0.33 % [-3.35, +4.65] |
| start 9  | +1.38 % [-2.32, +5.44] | -2.37 % [-5.25, +0.68] |
The gap to both references is no longer statistically resolved. Start 9 vs 11 differ by less than the CI width.

### Reading against the stated hypothesis
Static crossover tests losing MOS was not explained by "static across rungs": 9 and 11 help at every rung. The branch's
table fix matters only at very low crossovers (7: +5.87 % without it vs -1.03 % with it); at 9-11 it is neutral. The earlier
drops therefore came from low crossovers hitting the table bug and/or older encoder states, not from rate dependence.
Open: music held-out (none available here), 44.1/32 kHz, mono, VBR/CBR, decoder coverage beyond FAAD2+FFmpeg (no FDK
decoder here), CPU/footprint (a constant change).

## Amendments 2-3: 44.1 / 32 kHz and an independent FDK decoder (this section supersedes the headline above where they conflict)
Decoders: FAAD2 287c00c, FFmpeg 6.1.1, and `fdkdec` from faac-benchmark (`scripts/fdkdec`, built here against libfdk-aac 2.0.0;
Linux sha256 629616c1f0b41518...). FFmpeg's SBR descends from the same design as FAAD2's, so FDK is the more independent check.
Files: `rows_44k1_*`, `rows_32k_*`, `rows_32kb_*`, `fdkdec_*.json`, `bd_rates.py`, `decoder_agreement_scan.py`.

### 1. Decoder agreement of the stream (cst / SinceAlways, 10 start values x 3 rates; scan script above)
FAAD2 decodes to MOS ~1.2-1.7 (broken) while FFmpeg and FDK decode normally at: **48 kHz** start 10; **44.1 kHz** 6, 7, 10, 11;
**32 kHz** 7, 8, 11. Start 9 is accepted by FAAD2 at all three rates. FDK-decoder rescoring confirms FAAD2 is the odd one out
(start 11 at 44.1/32: faad2 minus fdkdec = -2.5 / -2.3 MOS on every stream). The failures are in FAAD2's master-table adjustment
(see above); the branch's spec-compliant split decodes correctly under FFmpeg and FDK.

### 2. BD-rate of lower crossover vs control (start 15), mean [95 % CI], by decoder
| rate | arm | FAAD2 | FFmpeg | FDK dec |
|---|---|---|---|---|
| 48 | 11 | -4.60 [-7.55,-0.89] | -6.03 [-8.22,-3.78] | **-0.97 [-3.69,+2.24]** |
| 48 | 9  | -6.10 [-8.50,-3.50] | -5.75 [-8.40,-2.93] | **-2.45 [-5.05,+0.27]** |
| 44.1 | 11 | undecodable | -6.60 [-8.77,-4.31] | -2.56 [-4.87,-0.16] |
| 44.1 | 9  | -4.81 [-7.44,-2.15] | -4.87 [-7.48,-2.13] | -1.43 [-4.17,+1.55] |
| 32 | 11 | undecodable | +4.07 [+1.39,+7.20] | +3.96 [+1.91,+6.25] |
| 32 | 9  | +7.06 [+3.81,+10.89] | +6.51 [+3.41,+10.11] | +5.32 [+2.13,+9.06] |
| 32 | 13 | -0.88 [-1.60,-0.12] | -0.97 [-1.79,-0.17] | -0.39 [-1.45,+0.75] |
| 32 | 14 | -1.15 [-1.96,-0.46] | -1.06 [-1.90,-0.36] | -0.19 [-0.80,+0.39] |
Held-out speech (48 kHz, dual mono): start 11 -14.05 / -14.05 / -12.28 %, start 9 -18.62 / -18.62 / -14.95 % (FAAD2 / FFmpeg / FDK);
per-rung FDK deltas are mixed (start 9: -0.012 at 40, -0.024 at 64) while the BD-rate is large.

### 3. What this establishes and what it does not
- The earlier statement that lower crossover brings FAAC to parity with FDK/Apple was scored with FAAD2 only. With the FDK decoder
  the 48 kHz gain is roughly 1-2.5 % and its CI includes 0 for both arms; at 44.1 kHz start 9 also includes 0. FAAD2 and FFmpeg agree
  with each other (-4.6 to -6.6 %) but are not independent of one another. The parity-with-references numbers above therefore rest on
  the decoder family that favours the change, and must not be quoted as decoder-independent.
- Lowering the start index is **not** rate-invariant. 32 kHz loses at start 9/11 under every decoder (+4 to +7 %). Indices matched in Hz
  (13/14 at 32 kHz = 6.75/7.25 kHz) are within about -1 % to 0 %: marginal.
- The table split fix is needed for correct operation at lower crossovers (see start 7) and is spec-conformant; it does not make
  start 10/11 decode in FAAD2.
- Net: a real but decoder-dependent effect at 44.1/48 kHz (4-6 % on FAAD2/FFmpeg, ~1-2.5 % [CI spans 0] on FDK), no reliable gain at
  32 kHz, and a hard decoder-compat constraint (FAAD2 fails at several indices that other decoders accept). A production change
  would need FDK-decoder significance, FAAD2 fixed or the index restricted, and an independent music held-out set. Not shown: Apple's
  decoder, mono, VBR/CBR, CPU/footprint.

### 4. Is the FAAD2 master-table deviation also in the in-tree `libfaad` branch? No.
`nschimme/faac` branch `libfaad` (d7fe6f7, `FAAD 3.0.0`, rewritten decoder) implements the spec adjustment in
`libfaad/sbr.c: sbr_master_log` including the `(vDk1[last]-vDk1[0])/2` cap and the post-sort. Built with meson and run on the same
encoder streams (cst.wav; MOS, FAAD2 287c00c vs libfaad branch; `libfaad_branch_check.py`):
- 48 kHz start 10: 1.30 vs **3.96**; 44.1 kHz start 7/10/11: 1.22/1.27/1.46 vs **3.59/4.05/4.00**;
  32 kHz start 7/8/11: 1.22/1.24/1.35 vs **3.52/3.29/3.27**. Control (start 15) is identical in both (4.03 / 4.00 / 3.82 vs 3.81).
So the libfaad branch agrees with FFmpeg and FDK where standalone FAAD2 (FreewareAdvancedAudio/faad2 287c00c, the pin used by
faac-benchmark) fails. The defect is in `libfaad/sbr_fbt.c: master_frequency_table` of the standalone FAAD2: no cap on `change`
and an extra `qsort(vDk1, nrBand1 + 1, ...)`. Single clip per case; not a full-corpus decode.

## Corrections and new base (he-v1-retune 217f913) -- supersedes earlier statements where they conflict
### FAAD2 diagnosis corrected (section 4 above was incomplete)
The standalone FAAD2 failure is mainly that `master_frequency_table()` ignores `bs_alter_scale` and always warps the upper region by
1.3 (`(void)bs_alter_scale; /* TODO */`), so for `bs_alter_scale == 0` the upper region has fewer bands than the spec formula gives
(k1=36,k2=49,fs3: 4 vs 2). The unbounded `change` and the `nrBand1 + 1` sort are additional deviations; warp alone fixes most, not
44.1 kHz / start 11. Fix: https://github.com/nschimme/faad2/pull/2 (no code from any other decoder was read or copied; other decoders
were used only as black-box binaries to compare output). The patched decoder tracks the spec-conformant streams: of 600 sampled streams
522 are bit-identical and the 78 that change are all two-region streams (decoded vs source: 1.3-1.4 before, 3.4-4.1 after).

### Experiment on he-v1-retune 217f913 (PREREG amendment 4; 49 clips, 48 kHz, 40-64 kbps; mean BD-rate [95 % CI], neg = fewer bits)
Control = capped start 12 (= retune as shipped). "capped" keeps retune's k2/kx <= 2.2449 one-region rule; "lifted" removes it and uses the
two-region table fix 51c589e. Table fix alone is payload-identical to 217f913 (12/12).
| arm | FAAD2 | FFmpeg | FDK decoder |
|---|---|---|---|
| capped 13 | +3.25 [+2.51,+4.12] | +3.16 [+2.48,+3.92] | +1.91 [+0.13,+4.67] |
| capped 11 | +2.60 [+0.67,+4.71] | +2.67 [+0.79,+4.85] | +2.35 [+0.44,+4.58] |
| capped 10 | +1.91 [-0.55,+4.82] | +1.85 [-0.60,+4.77] | -0.03 [-1.82,+2.05] |
| capped 9  | +1.95 [-0.37,+4.47] | +2.52 [-0.10,+5.66] | +1.84 [-0.66,+4.62] |
| lifted 12 | -0.58 [-1.27,+0.17] | -0.61 [-1.32,+0.19] | -1.37 [-2.35,-0.52] |
| **lifted 9** | **-3.20 [-4.17,-2.10]** | **-3.02 [-4.18,-1.64]** | **-2.81 [-3.68,-1.95]** |
| lifted 9 vs capped 9 | -4.66 [-6.45,-3.03] | -4.69 [-6.49,-3.10] | -2.12 [-4.66,+0.85] |
Findings: (1) inside retune's capped design a lower start loses, because the cap truncates the SBR range more the lower the crossover;
(2) removing the cap and using start 9 beats retune under all three decoders with CIs excluding 0 (the first change in this study that does);
(3) mean MOS at 40/64 kbps (FAAD2): retune 4.172/4.567, lifted 9 4.199/4.588 at the same bitrate to within 0.1 kbps.
Starts 9 and 12 decode correctly in unpatched FAAD2, starts 10/11 do not (see the scan script and PR above), so the arm that wins is also the one
compatible with the pinned decoder. Limits: same 49 clips that selected 9 in the earlier screen (not independent), 48 kHz only. 44.1/32 kHz
need a start chosen in Hz (index 9 is 5.25 kHz at 32 kHz, where lower crossovers lost); no held-out music; VBR/CBR and mono untested.
Production sketch (not implemented): table fix 51c589e + remove the k2/kx cap + SBR_START_FREQ_RICH 12 -> 9 at 44.1/48 kHz, with a unit test of the table.
