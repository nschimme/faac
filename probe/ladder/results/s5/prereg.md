# Stage S5 pre-registered decision rules (written before each run)

## F2 fdk-aac on the #595 + #599 base (2026-09-29 ~23:20 UTC, before any encode or score)
FAAC: static probe, `FAAC_SF_SMOOTH=0.6 FAAC_BS_DROPRATIO=12` (= master + #595 + #599), ABR, 48 kHz stereo, 49 clips.
HE rungs 32k and 48k under `--object-type auto` (HE); LC rungs 64k, 96k, 128k with `--object-type lc`.
fdk-aac: mstorsjo/fdk-aac master + nu774/fdkaac, `fdkaac -p 5 -b <N>000` (HE) / `-p 2 -b <N>000` (LC), m4a, defaults
otherwise (afterburner on).
Score: serial zimtohrli (`score_clip.py`, aligns by cross-correlation), one results file per rung, jobs chained.
Bits-adjusted with FAAC's own per-clip slope from its anchors around the rung (HE 32k: 28/40k; HE 48k: 40/56k;
LC 64k: 56/72k; LC 96k: 80/112k; LC 128k: 112/144k):
adj(fdk − FAAC) = ΔMOS − slope·log2(bytes_fdk/bytes_FAAC); positive = fdk leads.
Control (before any rung is read): re-score 5 clips (Severance, 21-classic, velvet, 24-Greensleeves, 12-German) of one
FAAC and one fdk stream twice; MOS must be identical. FAAC ctl at each rung must be ADTS/PCM from the static binary.
Reading per rung: "at par" if |mean adj| < 0.005, "FAAC ahead" if mean adj ≤ −0.005, "fdk leads" if ≥ +0.005.
The fdk goal is met if no rung has fdk leading by ≥ 0.01. Any rung with fdk ≥ +0.01 is named the next target.
Report mean, median, W/L (|d| > 0.0005), byte ratio, worst clips each way.

## H2 SBR stop frequency and frequency scale (2026-09-29 ~23:25 UTC, before any H2 encode or score)
Base as H1 (static probe, `FAAC_SF_SMOOTH=0.6 FAAC_BS_DROPRATIO=12`, HE-AAC auto, ABR, rate loop on), crossover at the
base (start 15, kx 31, 11.6 kHz). k2 read back with `FAAC_SBR_DUMPTAB` (48 kHz output, 375 Hz per QMF band):
stop 7 → k2 38 (14.3 kHz, Apple 32k), 8 → 41 (15.4 kHz, fdk 32k), 9 → 45 (16.9 kHz, fdk 48k), 10 → 49 (18.4 kHz),
11 → 54 (20.3 kHz, base at both rates), 12 → 59 (22.1 kHz).
Freq scale: production is 3 (8 bands/oct) at 32k and 1 (12/oct) at 48k; arms cover 1, 2, 3 at both rates.
`FAAC_SBR_ALTER` only acts with two master regions (k2/kx > 2.2449); at kx 31 every stop gives one region, so alter is
inert here (tables identical, checked with DUMPTAB). Alter is therefore not an arm; its control is reported.
Controls first: knob at its neutral value = base, decoded PCM 49/49 (`FAAC_SBR_STOP=11` at 32k and 48k,
`FAAC_SBR_FREQ_SCALE=3` at 32k, `=1` at 48k), plus `FAAC_SBR_ALTER=1` PCM-identical (expected, since inert).
Arms, both rates: stop 7, 8, 9, 10, 12; freq scale 1/2/3 (the two non-base values per rate).
Score: 49 clips, serial, bits-adjusted vs the knob-unset base with FAAC's own HE slope (28/40k at 32k, 40/56k at 48k),
one results file per rate, jobs chained.
Pass (H1 rule): mean adj ≥ +0.005, W > L (|d| > 0.0005), no clip < −0.05, total bytes within ±12.5 %, chosen value at
the bracket centre (both neighbouring tried values worse; 11 is a stop neighbour, the base scale is a scale neighbour;
the ends of the 1–3 scale range count as bounded). Stop is one target applied at every rate, so a passing stop must also
be not worse (mean adj ≥ 0) at the other rate. Freq scale is chosen per rate tier in production (≥ 24k/ch → 1,
≥ 12k/ch → 3), so a scale passes per rate. If both a stop and a scale pass, their combination is scored and must pass
too; only then a crossover retry (start 14) on top of it. Apple HE 32k raw MOS/bytes reported per arm, not in the rule.

## B3 fit of Apple's 0–6 kHz scalefactors (2026-09-29 ~23:32 UTC, before fitting or building any arm)
Base as S4-B2 (LC forced, 128k ref apple slope 112/144, 96k ref apple_lc96k slope 80/112; step1, no rate loop).
Data: `s5/b3_feat.py` rows. Target y = Apple sf − FAAC sf on the 0–6 kHz rSFr set (band centre < 6 kHz, regular in both
channels of both streams, same layout). Features from FAAC's own encode only (so a rule can run in the encoder):
band energy per line relative to the ICS's mean over its FAAC-regular 0–6 kHz bands (relE), the ICS mean itself (mE),
peak/mean energy (tonal), log width, target/energy (smr), FAAC sf minus its coded neighbours' mean (nres, the #595
residual), sf relative to the ICS's 0–6 kHz mean (relsf), band centre frequency, short/long. Linear least squares,
fit per rate, 2-fold by clip (even/odd g2 index); report test R² per fold. Predictions for every clip come from the
model fitted on the other fold (cross-fitted), rounded to integer steps.
Controls first: Control 0, KA, KF 49/49 at both rates; mask-dump encode PCM-identical to the normal encode 49/49; K0
(builder, no change) PCM-identical to KF 49/49.
Arms vs F, bits-adjusted: rSF06 (Apple sf on the set, < 6 kHz; the ceiling), rFIT (FAAC sf + prediction on the set),
rFITall (FAAC sf + prediction on every FAAC-regular band < 6 kHz, no Apple needed; what an encoder rule would do).
Offline pass (to promote to an encoder knob): rFITall mean adj ≥ +0.005 with W > L (|d| > 0.0005) and total bytes
within ±12.5 % at both 96k and 128k. rFIT is diagnostic (how much of rSF06 the fit captures). If it passes, the encoder
knob is screened with the H1 rule (rate loop on, 49 clips, ≥ +0.005, W > L, no clip < −0.05, bytes ±12.5 %, bracket
centre on a strength scale 0.5/1/1.5 of the fitted offsets, 96k and 128k not worse), then a focused PR.

## B3b diagnostic (2026-09-30, after the 128k B3 screen, before fitting): does a richer fit of the same features carry it?
B3 fails at 128k (rFITall −0.0019, rFIT +0.0018 of the rSF06 +0.0161 ceiling), so no encoder knob follows from it.
Diagnostic only, not a decision: same rows, features and folds, gradient-boosted trees (sklearn
HistGradientBoostingRegressor, defaults, max_iter 200), cross-fitted, arms rGBM (set) and rGBMall at 128k.
Reading: if rGBMall ≥ +0.005 with W > L, the within-frame features carry Apple's gain and a compact rule is worth deriving
next; if not, they don't, and the 0–6 kHz allocation needs features across frames (or is not reachable offline).

## H2 confirmation on master (2026-09-30, after H2, before scoring)
Found while cutting the PR: the probe's HE is not master's. It carries #579 (SBR time deltas / stereo coupling) and its
follow-up, so HE results since S4 are on master + #579 + #595 + #599 (probe knobs unset ≠ master at HE 32k and 48k,
0/49 PCM). The PR (freq scale 3 in the ≥ 24 kbps/ch tier) is cut from master, so it is re-measured there: master vs
master + change, HE 48k, 49 clips, static builds, bits-adjusted with master's own 40/56k slope. Confirm if mean
adj ≥ +0.005, W > L, no clip < −0.05, bytes ±12.5 %. HE 32k and LC are PCM-identical to master (checked 49/49).
