# HE-v1 32 kbps: what the FAAC core does at onsets (decoder-dump decisions joined with per-event band pre-echo)

Script `scripts/s12/he32_core_onsets.py` (extract / report / md; extracted per-frame cache outside the repo in `/home/user/work/he32core`), full numbers in `he32_core_onsets.json`.
Streams: FAAC master HE-v1 CBR 32k (48 kHz output, core 24 kHz, crossover 11.6 kHz), FDK 48k-ref HE 32k (kx 5.6 kHz), Apple HE 32k (32 kHz output, 16 kHz core, ~6 kHz). 77 clips decoded with the instrumented libfaad; only 36 clips have scored onset events (965 per-channel events = the `nper_bands` cache rows, event order = `ref_onsets`). Core frame holding the onset: k-1 with k = floor((onset + lag)/2048), lag re-estimated per stream by xcorr (output-rate scaled for Apple); `onset k` is the following frame. For FAAC the k-1 assumption is not independently verifiable (100 % short at k-1 and 94 % at k), the alignment is the one used in `sbr_grid_he.md`. Per-event pre-echo = max(0, dB) in 2-6 / 6-12 kHz from `/home/user/work/nb/rows.jsonl` (same rows as `he_nper_bands.md`).

## Summary

1. **FAAC at an onset (k-1): 100 % EIGHT_SHORT, 0 % TNS, ~4.6 window groups, PNS on 73 % of the 4-12 kHz lines (31 % of 0-2 kHz, 57 % of 2-4 kHz), 11 % spectral, 7 % IS, packet 1966 bits (FAAC mean 1356, FDK 1531 at its onsets).** FAAC does not raise PNS at attacks: its PNS share in 4-12 kHz is lower at k-1 (73 %) than in all short frames (81 %) or long frames (93 %). It simply never turns PNS off (PNS is 86 % of its 4-12 kHz lines everywhere). FDK/Apple: no PNS at all, 65-66 % short at k-1 (the rest long/stop), **TNS in 97 % / 89 % of onset frames** (FDK 13 % / Apple 11 % of all frames), coded only to 6-6.5 kHz (everything above is SBR); 56-64 % spectral in 4-6 kHz.
2. **The pooled event-level story is dominated by one clip.** `13 fatboy_30sec` holds 44 % of the 965 events and 98 % of the "PNS-saturated" onset frames (4-12 kHz PNS > 90 %). Pooled event excess 21.2 dB (2-6 kHz) vs FDK 16.0 is a fatboy effect; **clip-balanced, 2-6 kHz is at parity (FAAC 7.68 vs FDK 7.73 vs Apple 7.96 dB) and the FAAC-only gap is in 6-12 kHz (14.9 vs FDK 11.1 vs Apple 13.3)**, full band 10.11 vs 8.71 vs 10.05 (matches the benchmark clip-mean). The statement "excess is in the core 2-12 kHz" therefore holds for 6-12 kHz at clip level, not for 2-6 kHz.
3. **PNS share in the onset frame correlates with excess, mostly through between-clip and saturation effects.** Pooled Spearman pns412 vs excess +0.79 (2-6k), +0.54 (6-12k), +0.78 (full); excluding fatboy +0.22 / +0.31 / +0.27; within-clip +0.14 / +0.22-0.26 / +0.21-0.23. Frame bits (k-1) correlate negatively (-0.12..-0.18 within clip), collinear with PNS (rho(pns412, bits) = -0.81). Zero-band share and short-vs-long are not testable (FAAC has ~0 % zero bands and 100 % short at k-1). Group count has no within-clip effect.
4. **Bound.** If PNS-heavy onset frames behaved like PNS-light frames of the same clip, the pooled 6-12 kHz excess would drop by about 1.8 dB (non-fatboy split: 20.2 vs 16.6 dB over half the events) and full band by about 1.2 dB, against a clip-mean FAAC-FDK gap of 3.8 dB (6-12k) and 1.4 dB (full). Treat these as ceilings (association, not causation). The dominant but unreachable part is fatboy (non-sat 15.1 vs sat 37.0 dB in 2-6k, 17 non-sat events), where the onset frames are bit-starved and PNS covers even 66 % of 0-2 kHz.

## Tables

### A. Window sequence, TNS, gain, bits at the onset frame (per-channel events, 965; k-1 = frame holding the onset)

| stream | frame set | n | short | start | stop | long | TNS | mean global gain | mean sf | frame bits | coded cutoff Hz |
|---|---|---|---|---|---|---|---|---|---|---|---|
| faac | onset k-1 | 965 | 100% | 0% | 0% | 0% | 0% | 156.2 | 157.3 | 1966 | 12000 |
| faac | onset k | 965 | 94% | 0% | 6% | 0% | 0% | 161.5 | 162.6 | 1360 | 12000 |
| faac | all | 70134 | 57% | 9% | 9% | 25% | 0% | 158.2 | 159.5 | 1356 | 12000 |
| faac | all long-type | 30378 | 0% | 21% | 21% | 58% | 0% | 160.0 | 162.8 | 1356 | 12000 |
| faac | all short | 39756 | 100% | 0% | 0% | 0% | 0% | 156.8 | 157.1 | 1356 | 12000 |
| fdk | onset k-1 | 965 | 65% | 0% | 10% | 25% | 97% | 160.4 | 159.1 | 1531 | 5984 |
| fdk | onset k | 965 | 13% | 2% | 53% | 32% | 58% | 166.6 | 164.0 | 1353 | 5959 |
| fdk | all | 70204 | 6% | 4% | 4% | 86% | 13% | 164.8 | 163.2 | 1378 | 5936 |
| fdk | all long-type | 66216 | 0% | 5% | 5% | 91% | 9% | 165.5 | 163.8 | 1378 | 5932 |
| fdk | all short | 3988 | 100% | 0% | 0% | 0% | 68% | 154.3 | 153.6 | 1378 | 6000 |
| apple | onset k-1 | 965 | 66% | 4% | 14% | 16% | 89% | 155.0 | 157.8 | 2374 | 6508 |
| apple | onset k | 965 | 35% | 8% | 36% | 21% | 49% | 158.7 | 160.7 | 1921 | 6282 |
| apple | all | 46966 | 12% | 6% | 6% | 76% | 11% | 162.4 | 161.4 | 2053 | 6084 |
| apple | all long-type | 41402 | 0% | 7% | 7% | 87% | 6% | 163.4 | 162.7 | 2053 | 5994 |
| apple | all short | 5564 | 100% | 0% | 0% | 0% | 49% | 155.1 | 151.6 | 2053 | 6750 |

Frame bits = access-unit size of the container packet (all elements, incl. SBR payload). Apple 32k is a 32 kHz-output stream (64 ms frames), so its bits are not per-time comparable.

### B. Band-type shares by region (line-weighted per ICS, mean over frames; PNS = codebook 13, IS = 14/15, zero = codebook 0 within max_sfb, unc = above max_sfb / beyond the coded cutoff; spectral = the rest)

| stream | frame set | region | PNS | zero | IS | spectral | uncoded |
|---|---|---|---|---|---|---|---|
| faac | onset k-1 | 0-2 | 31% | 0% | 20% | 42% | 6% |
| faac | onset k-1 | 2-4 | 57% | 0% | 18% | 25% | 0% |
| faac | onset k-1 | 4-6 | 70% | 0% | 10% | 14% | 6% |
| faac | onset k-1 | 6-8 | 80% | 0% | 8% | 12% | 0% |
| faac | onset k-1 | 8-12 | 70% | 0% | 6% | 8% | 16% |
| faac | all long-type | 0-2 | 5% | 2% | 36% | 57% | 0% |
| faac | all long-type | 2-4 | 35% | 1% | 28% | 32% | 4% |
| faac | all long-type | 4-6 | 87% | 1% | 6% | 6% | 0% |
| faac | all long-type | 6-8 | 98% | 1% | 0% | 0% | 0% |
| faac | all long-type | 8-12 | 92% | 1% | 0% | 0% | 6% |
| faac | all short | 0-2 | 5% | 1% | 34% | 54% | 6% |
| faac | all short | 2-4 | 28% | 1% | 34% | 37% | 0% |
| faac | all short | 4-6 | 68% | 1% | 12% | 13% | 6% |
| faac | all short | 6-8 | 91% | 1% | 4% | 4% | 0% |
| faac | all short | 8-12 | 80% | 1% | 2% | 2% | 16% |
| fdk | onset k-1 | 0-2 | 0% | 14% | 1% | 81% | 4% |
| fdk | onset k-1 | 2-4 | 0% | 16% | 1% | 81% | 1% |
| fdk | onset k-1 | 4-6 | 0% | 39% | 1% | 56% | 4% |
| fdk | onset k-1 | 6-8 | 0% | 0% | 0% | 0% | 100% |
| fdk | onset k-1 | 8-12 | 0% | 0% | 0% | 0% | 100% |
| fdk | all long-type | 0-2 | 0% | 10% | 1% | 88% | 0% |
| fdk | all long-type | 2-4 | 0% | 16% | 2% | 78% | 4% |
| fdk | all long-type | 4-6 | 0% | 32% | 2% | 66% | 0% |
| fdk | all long-type | 6-8 | 0% | 0% | 0% | 0% | 100% |
| fdk | all long-type | 8-12 | 0% | 0% | 0% | 0% | 100% |
| fdk | all short | 0-2 | 0% | 12% | 1% | 81% | 6% |
| fdk | all short | 2-4 | 0% | 18% | 1% | 81% | 0% |
| fdk | all short | 4-6 | 0% | 25% | 1% | 68% | 6% |
| fdk | all short | 6-8 | 0% | 0% | 0% | 0% | 100% |
| fdk | all short | 8-12 | 0% | 0% | 0% | 0% | 100% |
| apple | onset k-1 | 0-2 | 0% | 15% | 10% | 75% | 0% |
| apple | onset k-1 | 2-4 | 0% | 18% | 9% | 62% | 11% |
| apple | onset k-1 | 4-6 | 0% | 21% | 6% | 64% | 8% |
| apple | onset k-1 | 6-8 | 0% | 7% | 4% | 31% | 59% |
| apple | all long-type | 0-2 | 0% | 8% | 7% | 85% | 1% |
| apple | all long-type | 2-4 | 0% | 13% | 9% | 68% | 10% |
| apple | all long-type | 4-6 | 0% | 18% | 11% | 71% | 1% |
| apple | all long-type | 6-8 | 0% | 0% | 0% | 0% | 100% |
| apple | all short | 0-2 | 0% | 9% | 14% | 77% | 0% |
| apple | all short | 2-4 | 0% | 11% | 12% | 64% | 12% |
| apple | all short | 4-6 | 0% | 10% | 10% | 67% | 12% |
| apple | all short | 6-8 | 0% | 6% | 6% | 50% | 38% |

FAAC onset frames also: `onset k-1` is 100 % EIGHT_SHORT, so short-vs-long cannot be tested inside FAAC. FDK/Apple cores end at 5.6-6.5 kHz (their 6-8 and 8-12 kHz regions are uncoded = SBR).

### C. FAAC 32k: per-event pre-echo (nper_bands cache) vs onset-frame (k-1) decisions

| target | feature | Spearman (events) | n | within-clip | excl. fatboy | excl. fatboy within-clip |
|---|---|---|---|---|---|---|
| ex26 | pns412 | +0.79 | 965 | +0.14 | +0.22 | +0.14 |
| ex26 | pns012 | +0.77 | 965 | +0.22 | +0.23 | +0.18 |
| ex26 | zero412 | -0.02 | 965 | -0.13 |  |  |
| ex26 | spec412 | -0.55 | 965 | -0.09 |  |  |
| ex26 | bits | -0.61 | 965 | -0.14 | -0.14 | -0.13 |
| ex26 | ng | +0.70 | 965 | +0.03 | -0.02 | -0.00 |
| ex26 | gain | +0.65 | 965 | +0.04 |  |  |
| ex26 | short | n/a (constant) | 965 | n/a (constant) |  |  |
| ex26 | tns | n/a (constant) | 965 | n/a (constant) |  |  |
| ex612 | pns412 | +0.54 | 965 | +0.22 | +0.31 | +0.26 |
| ex612 | pns012 | +0.56 | 965 | +0.31 | +0.31 | +0.30 |
| ex612 | zero412 | -0.02 | 965 | -0.06 |  |  |
| ex612 | spec412 | -0.33 | 965 | -0.07 |  |  |
| ex612 | bits | -0.40 | 965 | -0.17 | -0.14 | -0.18 |
| ex612 | ng | +0.38 | 965 | +0.02 | -0.17 | -0.02 |
| ex612 | gain | +0.41 | 965 | +0.10 |  |  |
| ex612 | short | n/a (constant) | 965 | n/a (constant) |  |  |
| ex612 | tns | n/a (constant) | 965 | n/a (constant) |  |  |
| exfull | pns412 | +0.78 | 965 | +0.21 | +0.27 | +0.23 |
| exfull | pns012 | +0.77 | 965 | +0.27 | +0.29 | +0.26 |
| exfull | zero412 | -0.01 | 965 | -0.07 |  |  |
| exfull | spec412 | -0.55 | 965 | -0.09 |  |  |
| exfull | bits | -0.60 | 965 | -0.15 | -0.12 | -0.15 |
| exfull | ng | +0.69 | 965 | +0.05 | +0.05 | +0.07 |
| exfull | gain | +0.62 | 965 | +0.05 |  |  |
| exfull | short | n/a (constant) | 965 | n/a (constant) |  |  |
| exfull | tns | n/a (constant) | 965 | n/a (constant) |  |  |

ex26 / ex612 / exfull = per-event max(0, dB) pre-echo in 2-6 kHz / 6-12 kHz / benchmark full band. pns412 = PNS line share in 4-12 kHz (the 2-12 kHz and 0-12 kHz variants are pns212/pns012), zero412 = zero + uncoded share in 4-12 kHz, ng = number of window groups, bits = packet bits of frame k-1.

### D. Split tables

| split | n hi / lo | ex26 hi / lo | ex612 hi / lo | exfull hi / lo | within-clip diff ex26 / ex612 / exfull (clips) |
|---|---|---|---|---|---|
| pns412 > median 0.79 | 481 / 484 | 33.2 / 9.2 | 25.7 / 17.0 | 31.1 / 11.2 | +5.2 / +2.6 / +4.0 (17) |
| bits > median 1816.00 | 477 / 488 | 11.0 / 31.1 | 17.6 / 25.0 | 12.5 / 29.5 | -3.6 / -2.2 / -4.1 (18) |
| pns412 > median 0.68, excl. fatboy | 123 / 416 | 11.6 / 8.8 | 20.2 / 16.6 | 13.4 / 10.9 | |

Zero-band share is not testable: 963 of 965 FAAC onset frames have <= 8.7 % zero/uncoded lines in 4-12 kHz.

| PNS share 4-12 kHz (k-1) | n | ex26 | ex612 | exfull | frame bits |
|---|---|---|---|---|---|
| =0 | 18 | 10.4 | 17.9 | 15.1 | 4168 |
| (0.00,0.25] | 29 | 7.4 | 12.4 | 8.6 | 3077 |
| (0.25,0.50] | 85 | 7.2 | 11.7 | 8.2 | 2755 |
| (0.50,0.75] | 323 | 9.6 | 18.2 | 11.9 | 2166 |
| (0.75,0.90] | 92 | 10.9 | 19.7 | 12.6 | 1693 |
| (0.90,1.00] | 418 | 36.7 | 26.9 | 34.0 | 1538 |

Window groups at k-1 (short frames): faac mean 4.55; fdk mean 4.00; apple mean 4.38.

### E. One clip dominates the pooled event numbers

13 fatboy_30sec holds 426 of 965 events (44 %), 96 % of them have PNS > 90 % of the 4-12 kHz lines, and it holds 98 % of all such events. Only 36 of 77 clips have scored events.

| target | enc | pooled events | clip-mean (36 clips) | clip-mean excl. fatboy | fatboy | pooled excl. fatboy |
|---|---|---|---|---|---|---|
| ex26 | faac | 21.20 | 7.68 | 6.87 | 36.1 | 9.43 |
| ex26 | fdk | 16.03 | 7.73 | 7.24 | 24.7 | 9.20 |
| ex26 | apple | 17.27 | 7.96 | 7.41 | 27.3 | 9.37 |
| ex612 | faac | 21.35 | 14.91 | 14.59 | 26.3 | 17.42 |
| ex612 | fdk | 14.87 | 11.13 | 10.95 | 17.4 | 12.88 |
| ex612 | apple | 17.36 | 13.31 | 13.09 | 21.0 | 14.48 |
| exfull | faac | 21.14 | 10.11 | 9.44 | 33.4 | 11.48 |
| exfull | fdk | 15.44 | 8.71 | 8.31 | 22.6 | 9.77 |
| exfull | apple | 17.00 | 10.05 | 9.62 | 25.2 | 10.49 |

### F. Paired on the same events, split by FAAC onset-frame PNS saturation (pns412 > 0.9; 418 events, 43 %)

| target | enc | all | sat | non-sat |
|---|---|---|---|---|
| ex26 | faac | 21.2 | 36.7 | 9.4 |
| ex26 | fdk | 16.0 | 24.9 | 9.3 |
| ex26 | apple | 17.3 | 27.3 | 9.6 |
| ex612 | faac | 21.4 | 26.9 | 17.1 |
| ex612 | fdk | 14.9 | 17.6 | 12.8 |
| ex612 | apple | 17.4 | 21.4 | 14.3 |
| exfull | faac | 21.1 | 34.0 | 11.3 |
| exfull | fdk | 15.4 | 22.8 | 9.8 |
| exfull | apple | 17.0 | 25.4 | 10.6 |

| target | FAAC-FDK pooled gap (dB) | from sat events | from non-sat | FAAC-Apple gap | from sat | from non-sat |
|---|---|---|---|---|---|---|
| ex26 | 5.18 | 5.11 | 0.07 | 3.93 | 4.04 | -0.11 |
| ex612 | 6.48 | 4.01 | 2.47 | 3.99 | 2.38 | 1.61 |
| exfull | 5.70 | 4.82 | 0.88 | 4.14 | 3.72 | 0.42 |

Bits x PNS stratification (bit terciles of k-1 packet size: 1624, 2056): bits low / pns>0.9: n=265, ex26 36.9, ex612 26.8; bits low / pns<=0.9: n=58, ex26 9.4, ex612 15.7; bits mid / pns>0.9: n=153, ex26 36.3, ex612 27.1; bits mid / pns<=0.9: n=172, ex26 10.1, ex612 20.1; bits high / pns<=0.9: n=317, ex26 9.0, ex612 15.8. Spearman(pns412, bits) = -0.81.


## History check (what was tested at HE, on which metric)

Metric in every item below is MOS (Zimtohrli via score_clip.py, bits-adjusted with FAAC's own slope), never NPER, unless stated. "HE 32k" = 32 kbps at 48 kHz stereo unless stated as 32 kHz sample rate.

| lever | where | result | metric |
|---|---|---|---|
| lower SBR crossover at HE 32 kbps (start 14/13/11/8/7 = 10.1/9.0/7.5/6.0/5.6 kHz) | S4-H (`LADDER_RESULT.md` l.1167) | monotone loss: -0.044 / -0.107 / -0.122 / -0.143 / -0.276 adj, W/L 3/46 at 6 kHz; 48k: none beat base | MOS adj |
| same, FAAC core at the reference crossover | S6-X (l.1417-1498) | Fm (FAAC at ref kx) -0.28 / -0.14 at HE 32k; FAAC core at kx 16 under Apple's SBR +0.056 (34/15): the failure is FAAC's SBR at low crossover, not the core | MOS adj, splice oracle |
| noise-floor / invf constants at the low crossover | S6-N (l.1505-1528) | recover at most +0.007 of -0.142 (32k) | MOS adj |
| kx 22/18/16 on the HE core; crossover 12 + PNS level 2/1 | S10 (l.1912-1953) | "PNS bits swap for SBR bits"; crossover 12 + level 2/1 at 48k -0.214 / -0.735 (cost grows with short share: 29 % short -0.089, 86 % -0.312) | MOS adj |
| PNS threshold 0.2-0.5 (global, long and short) | S8-P (l.1803) | 0.3 wins at 48k (+0.024) but HE 32k -0.0046, 21/27, worst -0.193; not merged at 32k | MOS adj |
| `pnslevel` 3 / 5 | S10 | 32k -0.016 / -0.011 | MOS adj |
| PNS energy Viterbi (+-1 / +-2 steps) | S10 | 32k -0.025 / -0.11..-0.14 | MOS adj |
| PNS gated by tonality (peak/mean), long windows, LC 160k | EXPERIMENTS.md l.58-62 | monotone worse (+0.7..+2.2 % bytes) | BD-rate, LC |
| LC block switcher on the HE core (short 58 -> 26 %) | S10 | 32k -0.05..-0.24 | MOS adj |
| per-rung crossover, 48 kHz 40-64 kbps (music) | S11 `crossover-per-rung-098e890` | starts 9/11 gain 4.6-6.1 % (FAAD2) but 1-2.5 % with CI over 0 on FDK decoder; 32 kHz sample rate loses +4..+7 %; HE 32 kbps not covered | BD-rate |
| TNS on every short window (STNS v1), LC | S12 stage 3 | LC NPER -1..-3.8 dB but 2-8 cells per rung below -0.10 MOS; v2 attack-only and HE scoring "queued" | NPER + MOS |

Not tested anywhere I could find: (a) PNS disabled or restricted only on onset/attack frames (all PNS experiments were global or by band statistic; the only block-type split, S8-P C3, only reports PNS share per block type); (b) a lower crossover at HE 32 kbps judged on NPER (every crossover result is MOS); (c) any TNS on the HE core (FAAC HE core has 0 % TNS, S10 notes 0 % on long frames); (d) the FAAC SBR grid at the lower crossover (S12 SBR grid work is for the grid, not the crossover; FAAC 32k grid is 73 % FIXFIX/1 env). Context: FAAC leads FDK/Apple on HE P1 MOS at 32k (`SCOREBOARD.md` item 4), so any NPER lever must be MOS-neutral to be worth a prereg.


## Recommendation

Lower crossover at 32 kbps stays closed: five prior MOS runs lose monotonically, S6-X locates the loss in FAAC's SBR at a low crossover (its time grid is 73 % FIXFIX/1 envelope), and the data here show FAAC's 2-6 kHz excess is already at parity at clip level, so there is nothing for a lower crossover to fix there; the 6-12 kHz gap (3.8 dB) is exactly the band a lower crossover would hand to that non-adaptive SBR, so a crossover retry only makes sense after the S12 SBR grid result, and would need NPER and MOS gates together. The one untested lever the decisions point at is **PNS suppression in the 6-12 kHz (or 4-12 kHz) bands of EIGHT_SHORT frames holding a detected onset**, justified only at the margin: the clip-level 6-12 kHz gap is 3.8 dB and the within-clip association with onset-frame PNS share is +0.26..+0.30 (excl. fatboy), bounding the removable excess at about 1.8 dB of the 6-12 kHz clip-mean (about 1.2 dB full band, i.e. roughly the whole 1.4 dB clip-mean full-band gap to FDK if it were fully causal; it is an association, not causation). Cost side: PNS in those bands is what pays for the core at 32k (global PNS threshold 0.3 already -0.0046 at 32k, `pnslevel` 3 -0.016), and the largest per-event excess (fatboy, 33 dB) sits in bit-starved all-PNS frames where waveform coding has no bits to spend. If a prereg is written it should be a narrow attack-frame-only PNS gate (frame holds a detected onset, 4-12 kHz only), decided jointly on clip-mean NPER 6-12 kHz (primary, predicted effect at most -1.8 dB, fail if smaller than -0.5 dB) and MOS (bits-adjusted at 28/40 slope, fail below -0.005), with fatboy reported separately; do not expect more than the bound above. TNS on the HE core at onsets (FDK 97 %, Apple 89 %, FAAC 0 %) is the largest structural difference in the dump but acts mainly below 6 kHz where FAAC is already at parity; it belongs with the queued HE STNS scoring, not with this PNS lever.
