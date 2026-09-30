# Stage S6 pre-registered decision rules (written before each run)

## S6-X48 core vs SBR split at HE 48k, against fdk-aac and Apple (2026-09-30 ~01:35 UTC, before any encode or score)
Question: does FAAC's HE 48k gap (fdk +0.075, Apple +0.042, S5) sit in the AAC core or in the SBR payload?
Tool: bitstream splice (`s3/he_splice.py`): core CPE of stream Y + FIL/SBR tail (up to ID_END) of stream Z, per AU,
written as raw ADTS at the 24 kHz core rate, decoded by the FAAD dump decoder (implicit SBR, 48 kHz out), scored with
`score_clip.py` on the decoded WAV (it aligns by cross-correlation). Every arm, the unspliced ones included, goes
through the same ADTS → FAAD path. Bytes = sum of raw AU bytes over the spliced frames.

FAAC base F = static probe, `FAAC_SF_SMOOTH=0.6 FAAC_BS_DROPRATIO=12 FAAC_SBR_FREQ_SCALE=3` (master + #579 + #595 +
#599 + #601), HE under auto, ABR 48k. Reference X ∈ {fdk (`fdkaac -p 5 -b 48000`), Apple (`ref/apple_he48k`)}.
Crossovers differ (FAAC kx 31, fdk kx 22, Apple kx 24), and a core coded to kx 22 under an SBR starting at kx 31 would
leave a hole, so FAAC is also encoded at X's crossover: Fm = F + `FAAC_SBR_START` (12 → kx 22 for fdk, 13 → kx 24 for
Apple; checked with `FAAC_SBR_DUMPTAB`). FAAC input is zero-padded so its decoded output lines up with X's frame for
frame (pad P and frame offset k from the alignment control).
Arms per X: F, Fm, X, FmC+XS (FAAC core at X's crossover under X's SBR), XC+FmS (X's core under FAAC's SBR),
FC+XS (FAAC's own kx 31 core under X's SBR, diagnostic).
Adjustment: bits-adjusted vs F with FAAC's own per-clip 40/56k slope from padded anchors (same env as F).
Components (all adj): Gm = X − Fm; SBR effect s1 = (FmC+XS) − Fm (FAAC core held), s2 = X − (XC+FmS) (X core held);
core effect c1 = X − (FmC+XS) (X SBR held), c2 = (XC+FmS) − Fm (FAAC SBR held); s1 + c1 = s2 + c2 = Gm;
crossover cost Fm − F.
Controls, all before any arm is read:
- Alignment: raw-ADTS lag of X and of FAAC (FAAD decode vs source, low-band cross-correlation); P (even, input samples)
  and k chosen so that padded-FAAC lag + 2048·k = X lag within ±1 output sample on ≥ 47/49 clips.
- Control 0 on X's HE core: dump → `parse_dump.py` → `reemit_tool` at 24 kHz → X's own SBR tail spliced back →
  decoded PCM identical to X's own ADTS decode, 49/49 (fdk is new here; Apple 48k reruns the 32k E0 control).
- Control S: self-splice (own core + own tail) byte-identical to the original AU on every frame, F, Fm and X, 49/49.
- Scorer determinism: 5 clips (Severance, 21-classic, velvet, 24-Greensleeves, 12-German) of one spliced arm scored
  twice, identical MOS.
Reading (per X, 49 clips, report mean, median, W/L with |d| > 0.0005, bytes, worst clips):
- Only if Gm ≥ +0.010 is the split read; otherwise shares are reported as diagnostic.
- "SBR carries it" if s1 and s2 are each ≥ 50 % of Gm and each has W > L; "core carries it" if c1 and c2 are each
  ≥ 50 % of Gm with W > L; otherwise "both" (shares and the interaction s1 − s2 reported).
- No encoder change follows from the split itself. SBR → next is FAAC's SBR payload (envelope time/frequency
  resolution, noise floor, inverse filtering), not kx (S4-H). Core → next is the HE core decisions.

## S6-X32 same split at HE 32k (written with X48, before any encode or score)
Same arms, controls and reading at HE 32k: F = static probe `FAAC_SF_SMOOTH=0.6 FAAC_BS_DROPRATIO=12` (32k is already
freq scale 3), slope 28/40k padded anchors, X ∈ {fdk (`-b 32000`, kx 15 → `FAAC_SBR_START=7`), Apple
(`ref/apple_he32k`, kx 16 → `FAAC_SBR_START=8`)}. At 32k S5 had fdk +0.015 and Apple −0.011 (par), so the fdk split
is the target and Apple's is diagnostic.

## S6-K48 core-knob diagnostic at HE 48k (2026-09-30 ~02:15 UTC, after X48/X32 were read, before any encode)
X48 put the 48k gap in the core; `x_char.py` shows FAAC's HE core at 45 % short blocks (fdk 6 %, Apple 8 %), 23–25 % of
coded long bands PNS (fdk and Apple 0 %) and TNS on 0.5 % of ICS (fdk 15 %, Apple 9 %). Two existing CLI switches are
screened as a diagnostic of where in the core the gap sits: `--no-pns` and `--shortctl 1` (no short blocks), each on
F (probe base with `FAAC_SBR_FREQ_SCALE=3`, unpadded, `s3/sweep.py`), bits-adjusted vs F with the 40/56k slope,
49 clips. Reading: an arm ≥ +0.005 with W > L names that decision as a lever worth an encoder rule (not a PR as is:
blanket switches, S2/S3 show blanket long loses at LC); ≤ −0.005 rules it out as a blanket change. No PR from this.

## S6-N SBR noise floor and inverse filtering (2026-09-30 ~02:45 UTC, before any encode or score)
New probe knobs in `libfaac/sbr_bitstream.c` (unset = production): `FAAC_SBR_INVF` (bs_invf_mode, written for every
channel, production 3 = strong) and `FAAC_SBR_NOISE` (the single noise band's level, production 12; the decoder's noise
floor is 2^(6 − level), so lower = more noise; a coupled balance channel keeps its centre 6).
Base F: static probe `FAAC_SF_SMOOTH=0.6 FAAC_BS_DROPRATIO=12` (+ `FAAC_SBR_FREQ_SCALE=3` at 48k), HE auto, ABR,
unpadded (`s3/sweep.py`), slope anchors 28/40k (32k) and 40/56k (48k).
Controls first: `FAAC_SBR_INVF=3` and `FAAC_SBR_NOISE=12` each PCM-identical to the base, 49/49, at 32k and 48k.
Arms, each at the base crossover (kx 31) and at a low crossover (32k: `FAAC_SBR_START=8`, kx 16 = Apple 32k;
48k: `FAAC_SBR_START=13`, kx 24 = Apple 48k), plus the low-crossover base itself (L):
invf 0, 1, 2 (noise 12); noise 6, 8, 10, 14 (invf 3); invf 1 with noise 8 and 10. Nine arms per crossover.
All bits-adjusted vs F (kx 31 base) at the same rate.
Pass (H1 rule): mean adj ≥ +0.005, W > L (|d| > 0.0005), no clip < −0.05, bytes within ±12.5 %, the chosen value at
the bracket centre of each swept dimension (invf 0–3 bounded; noise neighbours among 6/8/10/12/14), and not worse
(mean ≥ 0) at the other rate at the same crossover, unless the value is tiered per rate (then the per-rate rule alone).
A low-crossover arm that passes vs F is a crossover move plus the SBR constants; it is also reported against L to show
how much of the S6-X ceiling (+0.056 at 32k: FAAC core at kx 16 under Apple's SBR) the constants recover.
A passing change is re-measured on master (+#601 where it applies) before a PR, per the S5 correction (#579).
