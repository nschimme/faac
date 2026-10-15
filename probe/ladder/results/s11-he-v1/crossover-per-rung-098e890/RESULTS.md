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
