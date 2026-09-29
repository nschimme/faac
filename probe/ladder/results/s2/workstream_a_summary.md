# sf-smooth (PR #595) vs master: local probe results
Harness: faac-benchmark encode (phase1 run_benchmark) + compute_single_mos, scoring serial, encodes <=3 workers, no
throughput/max_clips cap (all 400 speech clips). Driver: /home/user/wsA/drv.py; analysis: an44.py, ansp.py.
JSONs (same schema bd_rate.py reads): base_vbr44/cand_vbr44, base_sp20{abr,cbr,vbr}/cand_*, base_sp24abr/cand_sp24abr,
base_51/cand_51 (all in /home/user/wsA). Outputs m4a in /home/user/wsA/out. Nothing committed.

## 1. VBR 44.1k stereo LC (49 clips, q 76/203/284/369/569, all object type "Low Complexity")
Reproduction with bd_rate.py's own fit (cubic, per-clip, MOS->log10 rate):
| ladder | mean BD | median | max | clips worse |
|---|---|---|---|---|
| 5 rungs 64k+128..256k (cubic) | **+1.003%** | **-0.170%** | +16.84 | 23/49 |
| 4 rungs 128..256k (cubic) | **-2.039%** | -1.515% | 0.00 | 0/49 |
| 4 rungs, quadratic | -2.115% | -1.554% | 0.00 | 0/49 |
| 5 rungs, piecewise-linear interp (no polynomial) | -1.667% | -1.630% | 0.00 | 0/49 |
=> CI's +1.07/-0.21 is reproduced (+1.00/-0.17) ONLY when the 64k/q76 rung is in the ladder. bd_rate.py groups by
(corpus, object type) and 64k resolves to LC here, so it is silently added to the 128-256 ladder. The positive mean is a
fit artefact of that rung, not a genuine loss. Without it: mean -2.04%, no clip worse than 0.00% (velvet: bytes identical).

Per-rung aggregate (cand-base, 49 clips): rung | mean dMOS | clips dMOS<-0.001 | clips >+0.001 | mean size delta
- 64k/q76:  -0.0006 | 13 | 16 | -1.41%   (the noisy rung; worst single clip -0.0347)
- 128k/q203: +0.0029 | 3 | 33 | -1.19%  (worst -0.0020)
- 160k/q284: +0.0021 | 0 | 32 | -0.99%
- 192k/q369: +0.0017 | 0 | 24 | -0.88%
- 256k/q569: +0.0010 | 0 | 14 | -0.74%
Base mean MOS 4.48 / 4.87 / 4.92 / 4.94 / 4.97 (never 5.0, scorer not saturated but compressed to 4.9-4.98 at top rungs).

Top 8 clips by 5-rung BD (all have MOS monotonic in rate for both builds; cand never loses bytes-for-quality):
| clip | BD5 | BD4 (128-256) | PL-interp BD5 | 64k dMOS | 128k dMOS | 256k dMOS | bytes ratio 64k..256k |
|---|---|---|---|---|---|---|---|
| TrosYGareg | +16.84 | -5.19 | -2.97 | -0.0074 | +0.0089 | +0.0017 | .975 .978 .980 .982 .985 |
| fms | +15.36 | -1.17 | -0.28 | -0.0016 | -0.0018 | +0.0005 | .996 .996 .996 .996 .997 |
| Severance (1.31-1.51) | +11.76 | -6.75 | -4.43 | +0.0060 | +0.0103 | +0.0066 | .980 .976 .979 .982 .986 |
| take_your_finger_frin_my_head | +11.07 | -3.15 | -2.52 | +0.0034 | +0.0049 | +0.0018 | .982 .985 .986 .987 .988 |
| 35_SQAM_glockenspiel_cut | +10.45 | -5.55 | -2.46 | -0.0007 | +0.0094 | +0.0039 | .992 .988 .989 .989 .989 |
| hrp | +7.42 | -3.90 | -2.65 | -0.0001 | +0.0035 | +0.0006 | .983 .981 .983 .985 .986 |
| Jupiter, the Bringer of Jolity | +5.77 | -3.53 | -2.34 | -0.0015 | +0.0040 | +0.0008 | .981 .982 .984 .985 .987 |
| 4-Sound-English-male | +4.45 | -0.84 | -0.60 | +0.0027 | +0.0006 | +0.0004 | .997 .997 .997 .998 .998 |
Per-rung base -> cand (kbps, MOS), e.g. TrosYGareg: 64k 71.7/4.5731 -> 69.8/4.5657; 128k 135.8/4.9121 -> 132.8/4.9210;
160k 161.0/4.9480 -> 157.7/4.9534; 192k 182.3/4.9653 -> 178.9/4.9687; 256k 220.4/4.9824 -> 217.1/4.9841.
Severance: 64k 66.8/4.5040 -> 65.4/4.5100; 128k 123.1/4.8745 -> 120.0/4.8848; 256k 213.7/4.9747 -> 210.7/4.9814.
fms: 64k 66.3/4.4672 -> 66.0/4.4656; 128k 128.0/4.8865 -> 127.5/4.8847; 160k 155.1/4.9257 -> 154.5/4.9285; 256k 220.6/4.9673 -> 220.0/4.9678.
(Full 5-rung tables for all 8: `python3 an44.py`; hoisted from that, PL-interp says all 8 are <=-0.28%.)

Why the mean is positive (mechanism):
- Every top-8 outlier has cand smaller at every rung (bytes -0.2..-2.5%) with MOS >= base at 3-5 of 5 rungs (fms is the weakest: -0.0016/-0.0018 at 64k/128k, ~-0.4% bytes); the fit says
  +5..+17% only because of the 64k rung. Base and cand raw points are monotonic in rate (checked all 8: True/True).
- The 64k rung sits at MOS ~4.5 while the other four are packed in MOS 4.87-4.98: one far-away point plus four points in
  a 0.1-MOS window. A cubic in MOS through 5 such points has an inflection between them: the fitted d(log rate)/dMOS is
  NEGATIVE somewhere inside the overlap for 37/49 clips (e.g. TrosYGareg base min -1.50, cand min -2.44; glockenspiel
  -3.43/-4.04), i.e. "more quality for fewer bits" - non-physical. The integral over [max(minMOS), min(maxMOS)] is then dominated by
  where the two wiggly cubics differ (cand 64k MOS drops -0.007 while its 128k MOS jumps +0.009, shifting the inflection).
- A tiny dMOS at the 64k point (noise, |d|<=0.007) is amplified because the lower integration limit is the higher of the two
  64k MOSs; a -0.0074 dMOS there cuts the overlap start and the cubic tail extrapolates.
- BD5 vs BD4 per-clip correlation is -0.46 (unrelated). 14 clips have BD5>+2%; their mean BD4 is -3.0% and mean 64k dMOS is only -0.003.
- Genuine per-rung loss: only at 64k (13/49 clips lose >0.001 MOS, worst -0.035, at ~-1.4% bytes); at 128k 3 clips lose <=0.002; none >=160k.
Verdict for Q1: positive mean = ill-conditioned 5-point cubic (64k rung), not a real regression; 4-rung and interpolation methods say -1.7..-2.1%.

## 2. 16 kHz mono speech, speech_clean_16k (all 400 R_* clips, ViSQOL speech mode), cand-base
Encoded ES differs on 271/400 clips in every mode (129 byte-identical; their MOS delta is exactly 0 and they were not
rescored). dMOS averages: "all400" includes those zeros.
| mode | mean dMOS all400 | mean dMOS changed(271) | median | wins/losses/ties (|d|>.001) | t (changed) | total byte ratio |
|---|---|---|---|---|---|---|
| 20k ABR (-b 20) | -0.00151 | -0.00222 | 0.0000 | 85 / 80 / 235 | -1.68 | 0.99989 |
| 20k CBR (-b 20 --cbr) | +0.00185 | +0.00274 | +0.0005 | 123 / 98 / 179 | +1.48 | 1.00000 (sizes identical) |
| 20k VBR q220 | +0.00151 | +0.00223 | +0.0004 | 108 / 41 / 251 | +3.45 | 0.99978 |
| 24k ABR (-b 24) | -0.00001 | -0.00001 | +0.0000 | 84 / 73 / 243 | -0.01 | 0.99993 |
Base mean MOS on changed clips: 4.00 (ABR20), 3.87 (CBR), 4.22 (VBR), 4.05 (ABR24).
Delta spread (changed clips, ABR20): p5 -0.038, p95 +0.031, std 0.022 - a symmetric noisy distribution, not a shifted one.

Worst 10, 20k ABR (base -> cand MOS, bytes ratio): R_15_CHOP_FA 4.008->3.887 (1.0005); R_22_NOISE_FA 3.755->3.640 (1.0002);
R_02_COMPSPKR_FA 3.497->3.400 (1.0004); R_01_CLIP_FA 4.030->3.952 (0.9991); R_09_ECHO_FA 3.603->3.538 (0.9998);
R_12_CLIP_FA 4.406->4.341 (0.9998); R_11_CLIP_FA 3.713->3.651 (0.9998); R_23_ECHO_FA 4.083->4.023 (0.9994);
R_04_ECHO_FA 3.513->3.457 (0.9997); R_13_NOISE_MK 4.038->3.982 (0.9999). Worst -0.121; best +0.078 (R_11_NOISE_FA).
Worst 5, 20k CBR: R_17_NOISE_FA -0.132, R_06_COMPSPKR_FA -0.125, R_11_CLIP_FA -0.089, R_13_CLIP_FA -0.076, R_07_NOISE_FA -0.072 (ratio 1.0000).
Worst 3, 20k VBR: R_20_CHOP_ML -0.049 (1.0002), R_10_COMPSPKR_FA -0.037 (0.9990), R_03_COMPSPKR_FA -0.028.
Worst 3, 24k ABR: R_06_COMPSPKR_FA -0.098 (1.0010), R_16_NOISE_FG -0.069, R_05_CLIP_FA -0.060.

R_02_COMPSPKR_FA (bytes base->cand, MOS base->cand, dMOS):
- 20k ABR: 22235->22243 (1.0004), 3.4971->3.3998, -0.0973 (3rd worst)
- 20k CBR: 22926->22926 (1.0000), 3.5431->3.5188, -0.0243 (26th worst)
- 20k VBR: 33496->33484 (0.9996), 3.9119->3.9117, -0.0002 (67th worst; a tie)
- 24k ABR: 25251->25253 (1.0001), 3.6003->3.6537, +0.0534 (a top-5 winner among 400)
Is the loss systematic? No. Same clip goes -0.097 / -0.024 / -0.000 / +0.053 across modes. Per-clip dMOS correlation across
modes ~0 (ABR20 vs CBR20 r=-0.004, vs VBR20 -0.27, vs ABR24 -0.16). The 20 worst ABR20 clips, in the other modes: CBR mean
+0.019 (7 losses), VBR +0.008 (3 losses), ABR24 +0.007 (8 losses) - regression to the mean. Losses are as large as wins;
ABR20 mean -0.0015 is within noise (t=-1.7) and the sign flips in CBR/VBR. Probable cause: ViSQOL speech MOS jitter (+-0.1)
from tiny quantization changes in a few long frames, plus ABR loop path divergence (bytes move by +-0.1%).
Short blocks: speech is encoded ~85% EIGHT_SHORT (parsed from ADTS window_sequence; parser sanity-checked on music: 90%
ONLY_LONG) - ABR20 changed clips 84.7% short, identical clips 88.1%; R_02_COMPSPKR_FA 85.3% short (nonlong 89.7% incl. START/STOP);
R_15_CHOP_FA 76%, R_22_NOISE_FA 76%. Window sequences are identical base vs cand on all 400 clips. Only the ~10-15% ONLY_LONG
frames can change, yet 271/400 clips differ; corr(short%, dMOS) on changed clips = +0.09 (no relation). Zero clips have 0 short frames.

## 3. 5.1: audio_51/6_Channel_ID.wav (10 s, 6 x sine 110..880 Hz, 44.1 kHz; scenarios 44k1_51_96k/160k, ABR)
Both resolve to HE-AAC v1. Scorer: Zimtohrli per channel, ref/deg at 48k.
| rate | base MOS | cand MOS | dMOS | bytes base->cand | ratio |
|---|---|---|---|---|---|
| -b 96 | 4.9836 | 4.8737 | -0.1099 | 122137->122157 | 1.00016 |
| -b 160 | 4.9434 | 4.8490 | -0.0944 | 201987->201998 | 1.00005 |
Scorer does not saturate (base 4.94-4.98) but is very sensitive on pure tones. Waveform SNR per channel (dB, ref-aligned) is NOT
worse for cand: 96k base [44.2 44.3 46.6 29.1 42.9 42.6] vs cand [44.4 44.7 47.5 29.1 43.3 42.5]; 160k base [45.2 45.9 49.6 29.1 44.3 45.5]
vs cand [45.0 46.5 50.1 29.1 44.3 45.9]. Window sequences identical (97.7% long). So this is a metric-level loss on synthetic
tones (SBR/HE tonal content) at equal bytes, one file only (n=1, deterministic); likely metric quirk but a real MOS delta that CI's
5.1 rows would show if this file is the 5.1 corpus. (score_clip.py was not used: it converts to 48k stereo, not 6ch.)

## Bottom line
- Q1: +1.07% mean is a 64k-rung fit artefact (non-monotone cubic in 37/49 clips); 4-rung (-2.0%) and PL-interp (-1.7%) agree cand is better.
- Q2: no systematic speech loss; sign flips across modes/rates; R_02_COMPSPKR_FA loss is one-mode noise (bytes +0.04%).
- Q3: 5.1 sine file shows -0.09..-0.11 MOS at ~equal bytes with unchanged SNR; flag as metric-sensitive outlier.
