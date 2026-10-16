# Experiment log: LC high bitrate vs Apple (2026-10-16)

Read this before proposing an experiment, so nobody repeats a measured dead end. Linked from HANDOFF.md.
Everything is nschimme/faac, plain two-channel LC, 49 clips (PMLT2014 + SoundExpert), ABR 128/160/192 kbps, vs master unless stated. Positive BD-rate = needs more bits. MOS is Zimtohrli; the CI gate fails any clip x rung below -0.10.
Calibration: at 128-192 kbps MOS rises only about 0.0016 per +1 % bitrate, so -5 % BD-rate is only about +0.009 MOS. Maintainer budget: at most +10-20 % instructions vs master (2-3x is unacceptable); VBR output stays bit-identical (gate on ABR/CBR bitrate only).

## Summary

| Experiment | Where | Result | Verdict |
|---|---|---|---|
| Disable IS (`--joint 1`) | CLI screen, HANDOFF s2 | -1.58 % | Led to #81 |
| Short-window M/S instead of IS from 80 kbps/ch | #81 | -1.50 % [-2.84, -0.47], gap to Apple 6.65 % -> ~4.2 % with bias | **Main candidate, CI green** |
| M/S bias k=1.5 from 64 kbps/ch | #81 (was #82) | -0.77 % on top | Keep (k=2 -0.57 %, k=4 neutral, worst -0.081) |
| Scalefactor search, exact | #84 | -5.4 %, +180 % instr | Too slow; record only |
| Scalefactor search, exact greedy | #85 | -1.6 %, +87 % | Dominated; record only |
| Scalefactor search, model Viterbi, all bands | #80 | -2.3 % [-4.3, -0.4], +34 %, MOS +0.005 | Over budget; record only |
| Scalefactor search, model, bands 16+ lines | #86 | -1.4 %, +17 % | Gain too small for cost |
| Scalefactor search, model greedy, sampled lines | #87 | -0.7 %, +11 % | Gain too small for cost |
| Rounding-only NMR RDO above 6 kHz | former #80 head b7d5b1f | +4.4 % to +20.7 % (worse) | **Dead end** |
| PNS gated by band peak/mean | local | +0.71 % to +2.24 % (worse) | **Dead end** |
| PNS level change | #83 (Jules), earlier screen | default 4 optimal | **Dead end** |
| Coarser target above 8 kHz (FFmpeg-inspired) | local | +2.2 % to +14.8 % (worse) | **Dead end** |
| Mid-band target tilt 2.5-10 kHz (+-1.5..4.5 dB, peak 3.4-7 kHz) | local | +0.31 % to +2.87 % one way, +1.10 % the other | **Dead end** |
| LC block-switch thresholds (level ratio, drop ratio) | local | best -0.69 % [-1.60, +0.06], worst cell -0.046 | **Dead end** (small, spans 0) |
| Stereo gates/bias on the measured LC stereo rungs (bias from 48 kbps/ch, short M/S from 48/64 kbps/ch, k 1.25/1.75) | local, on #81+#88 | best -0.13 % at 48 kHz only; fails gate at 44.1 kHz | **Dead end** |
| Masking-target retune (loudness 0.35, avg weight 0.15, slope x1.5, tilt table) for stereo LC ABR/CBR >= 32 kbps/ch | #89 | 48 kHz 96-192: -3.38 % [-3.93, -2.80], worst -0.019; 44.1 kHz 128-192: -3.41 % [-4.09, -2.80], worst -0.007; 192-320: -3.06 %; 48-80: -1.20 %. +1.3 % instr, +1344 B text | **Candidate, CI pending** |
| Global rounding offset (MAGIC_NUMBER) 0.35/0.38/0.43/0.47 (was 0.4054) | local | -0.30 % / -0.47 % / +0.76 % / +2.92 % | Small win at 0.38 only; not taken |
| `-c 16000` / `-c 17500` on the 8 weakest clips, 96/128 | local | 16000 worse (+6 %/+21 % est.); 17500 +0.0067 MOS at 96, neutral at 128 | Not pursued |
| `--no-pns`, `--no-tns`, `-c 20000` | CLI screen | +5.25 %, -0.05 %, -0.61 % (CI spans 0) | PNS is worth ~5 %; TNS and cutoff neutral |

## Merged-candidate work

### Short-window M/S + M/S bias (PR #81; #82 closed, folded in)
- Finding: from 80 kbps/ch short windows used L/R below 3.5-7 kHz and IS above; Apple codes only 2-3 % of 6-16 kHz bands with IS (FAAC 13-16 % on short windows), and 72-80 % with M/S on long windows (FAAC 48-64 %). Apple analysis: black-box decision dump of the 196 packaged LC streams.
- Change 1: short windows take M/S on every band from 80 kbps/ch. Per rung dMOS: 128k 0, 160k +0.0032, 192k +0.0020; worst clip -0.006; BD-rate -1.50 % under FAAD2 and FFmpeg.
- Change 2: M/S when mid*side < 1.5*left*right from 64 kbps/ch (`StereoConfig.msBias`). -0.77 % [-1.33, -0.33], worst cell -0.010. More M/S loses quality on `Girl_In_The_Fire`.
- VBR: also helped locally (-0.84 % over -q 200..500) but left bit-identical on purpose. At -q 100 the bias lost quality (worst cell -0.098). A VBR trigger may come later as its own PR.
- Limits: same corpus that located the gap, FDK decoder not measured, CBR only by CI.
- History to respect before touching `stereo.c`: #110 Mixed Mode default, #138 LGPL rewrite, #182/#188 short windows forced to IS (dropping IS there cost 4.33 % at the time), #201 short-window L/R below crossover at 80 kbps/ch+, #225 no quieter-channel muting, #229 real M/S noise handling.

## Scalefactor search (PRs #80, #84-#87, branches claude/project-thread-e5fkdb, claude/sfsearch-*)
Viterbi/greedy choice of per-band scalefactors around the masking-formula value. Instruction counts from callgrind, 6 s clip at -b 160 (master 106M). Gated to ABR/CBR >= 48 kbps/ch; below that it lost MOS on speech/low-rate stereo.
- Exact (real quantize + Huffman bits, 5 candidates): -5.4 %, 2.8x. Capping the books checked: -3.9 %.
- Exact greedy hill-climb: -1.6 %, 1.9x.
- Histogram-summary model (bits fitted per codebook class by max q), 3 candidates: -2.3 %, 1.34x. 5 candidates: -1.8 %, 1.7x.
- Why the cheap model loses gain: per-band rate depends strongly on the largest quantized value (codebook class); fitted bits for a zero line range 0.46 to 3.7 across classes, so cliff effects drive the exact gain and are hard to model.
- Verdict: none is worth its cost (-2.3 % is about +0.004 MOS). To retry, improve the rate model or find where the exact gain comes from first, then check HE rungs and 224-320 kbps.
- The handoff says the scalefactor +/-1 search is owned by other agents; coordinate before touching it.

## Dead ends (do not retry without new causal evidence)

### Rounding-only NMR RDO (original PR #80 head, commit b7d5b1f; same idea as faac-old #580/#568)
Above ~6 kHz, a line of magnitude >= 1 drops one step when extra squared error relative to the band's allowed noise is below lambda x a constant per-magnitude bit saving. Weight 1/(target^2 x line energy). ABR 128/160/192, ffmpeg decode:
- lambda 0.05: +4.4 % (CI 1.8..7.7), mean MOS -0.009, worst cell -0.111 (velvet @128).
- lambda 0.15: +7.6 %. lambda 0.5: +20.7 %. Monotone worse.
- Notes: faac-old #580 and #568 are the same joint RDO (scalefactor +/-1, codebook, rounding, codebook DP). #568 also reverts the HE/LC crossover 32 -> 22 kbps/ch, loosens the footprint gate and uses `noinline` attributes (forbidden); neither has MOS/BD-rate data. Retry only with real Huffman tuple costs and after finding which bands lose MOS. The handoff also lists per-line RD, DP and path search as closed (S8-L..S10).

### PNS gating by tonality (flatness gate)
Finding at 160 kbps on velvet and take_your_finger: Apple uses 0 % PNS and waveform-codes 12-16 kHz in 99 % of bands; FAAC uses PNS in 61 % of 12-16 kHz and 78 % of 16-24 kHz long bands. Band error 12-16 kHz: Apple about -9 dB, FAAC -1 to -5 dB. FAAC is better than Apple below 6 kHz. Skipping PNS when band peak/mean exceeds T (long windows only): T=20 +0.71 % (CI +0.41..+1.12), T=12 +1.41 %, T=8 +2.24 % (worst cell velvet 128 -0.137). Monotone worse. PNS above 8 kHz already saves bits for FAAC; Apple's HF waveform coding must be cheaper per coded band (quantizer, entropy coding, allocation). Look at cost per coded HF band, not tool choice.

### PNS level / restriction
Default PNS level 4 is optimal; restricting PNS or changing the level did not help. #83 (Jules, PNS level 6 at >= 192 kbps) is an unmeasured automatic PR; treat as the same dead end unless CI proves otherwise.

### Mid-band target tilt, 3.4-7 kHz hump (motivated by the take_your_finger scalefactor comparison)
Opt-in scratch change (`scripts/band_cost/tilt_experiment.patch`, env `TILT_DB`): multiplies the masking target by a raised bump, full strength 3.4-7 kHz, ramps 2.5-3.4 and 7-10 kHz, long and short windows, all rungs. Sign check on the decoded scalefactors: negative dB = coarser in the bump. 49 clips, ABR 128/160/192, ffmpeg decode, Zimtohrli, own harness (`score_ladder.py`, `compare_bd.py`), vs the same build with the tilt off:
- -1.5 dB: BD-rate +0.31 % [+0.00, +0.62], worst cell -0.013 (Robots_old @128)
- -3 dB: +1.21 % [+0.60, +1.83], worst -0.030
- -4.5 dB: +2.87 % [+1.92, +3.86], worst -0.052
- +3 dB (finer in the bump, control): +1.10 % [+0.57, +1.60], worst -0.042 (Girl_In_The_Fire @128)
Worse in both directions, so master's mid-band allocation sits at a local optimum for a hump-shaped tilt. The take_your_finger scalefactor difference is not a general tilt that Apple exploits; do not retry it. A per-clip adaptive tilt is a different idea and is not covered.

### Block-switch thresholds (LC `PSY_LEVEL_RATIO_LC` 2.5, `PSY_DROP_RATIO_LC` 12 in `blockswitch.c`)
Motivation: velvet uses short blocks in 43 % of channel-frames vs Apple's 26 %. Opt-in scratch change reading `BS_LR` / `BS_DR` (`scripts/band_cost/blockswitch_experiment.patch`), LC only. Velvet is drop-driven: short ICS at -b 160 are 398 (default), 390 (LR 3.5), 380 (LR 5), 324 (DR 24), 322 (DR 48), 120 (triggers off). 49 clips, ABR 128/160/192, vs master:
- DR 24: -0.20 % [-0.48, 0.00], worst -0.010
- DR 48: +0.03 % [-0.58, +0.89], worst -0.137 (Last_Of_The_Mohicanz @128, fails the gate)
- LR 3.5, DR 12: -0.49 % [-1.22, +0.13], worst -0.046 (liberate @128)
- LR 3.5, DR 18: -0.55 % [-1.38, +0.14], worst -0.046
- LR 3.5, DR 24: -0.69 % [-1.60, +0.06], worst -0.046; mean MOS 128/160/192: -0.0009 / +0.0009 / +0.0008. Velvet itself: dMOS +0.019 / +0.010 / +0.007, bitrate -0.1 to -0.2 %.
- LR 5, DR 48: -0.20 % [-1.64, +1.18], worst -0.135.
The ceiling is under 1 %, every interval spans 0, and the better settings still hurt liberate at 128. Short-block rate is not the main gap on velvet (cutting it by a quarter bought +0.019 MOS at one rung). Do not retune these for LC without a new reason. HE uses its own ratios and was not touched.

### Stereo gates and bias on the benchmark's own stereo scenarios (after #81 + #88)
Scope: `faac-benchmark` config scenarios, auto object type. 48 kHz stereo is HE up to 64 kbps and LC from 96; 44.1 kHz HE at 64, LC from 96; 32 kHz stereo is LC from 48 kbps (24 kbps/ch). All 34 scenarios x 2 clips x ABR/CBR/VBR: #88 output is identical to #81 (204 decoded-PCM md5 compares), so #88 is a pure simplification. Env-driven scratch change `scripts/band_cost/stereo_sweep.patch` (`ST_SHORT`, `ST_BIASBR`, `ST_K`); 49 clips, ABR, own harness (`score_ladder.py`, `CORP`/`RUNGS` env; the reference is resampled to 48 kHz stereo like the CI scorer), paired against the same build with defaults.
- 48 kHz LC 96-256: short M/S from 96 kbps (48/ch): -0.06 % [-0.58, +0.68], worst -0.208 (Greensleeves @96). From 128: -0.03 %, worst -0.092. Bias from 96 kbps: -0.13 % [-0.24, -0.05], MOS +0.0038 at the 96 rung, worst -0.008 (the only clean gain). Both: worst -0.242. k 1.25: -0.06 % [-0.15, +0.02]; k 1.75: +0.07 % [-0.05, +0.22], worst -0.023.
- 44.1 kHz LC 96-256, same bias-from-96: -0.01 % [-0.09, +0.09] but velvet @96 is -0.442 (fails the -0.10 gate), mean MOS at 96 -0.008. So the 48 kHz gain does not transfer; do not lower the bias gate.
- 32 kHz LC 48-128: bias from 24 kbps/ch (64 kbps stereo): -0.10 % [-0.39, +0.16], worst -0.051 (Wurlitzer @64); from 40 kbps/ch: -0.11 % [-0.26, +0.03], worst -0.024, MOS +0.0028 at the 80 rung only; from 48 kbps/ch: -0.04 %, k=2 +0.04 %. Nothing consistent.
- Verification of #81+#88 vs master on the other measured ladders: 44.1 kHz (96-256) -0.81 % [-1.23, -0.43], worst -0.014; 48 kHz (96-320) -0.83 % [-1.23, -0.47], worst -0.013 (Girl_In_The_Fire @128), MOS +0.0005/+0.0043/+0.0029/+0.0012/+0.0005 at 128-320, bitrate -0.14 % to -0.47 %; 32 kHz (48-128) +0.02 % [-0.01, +0.04], MOS unchanged except -0.001 at 128 (worst -0.029): #81's gates start at 64 kbps/ch, so 32 kHz stereo is untouched below 128 kbps.
Verdict: stereo.c is at an optimum for the measured scenarios as #81+#88 leave it. The remaining stereo lever is the 32 kHz stereo family below 64 kbps/ch (starved/IS tiers), which this sweep did not move.

### Coarser HF target above 8 kHz (inspired by FFmpeg's NMR coder; FFmpeg is LGPL so its source may be read)
FFmpeg `aaccoder_nmr.h` is a Viterbi scalefactor search with lambda bisected to the bit budget; a comment claims Apple codes 7.5-12.6 kHz ~6 dB and 12.6 kHz+ ~28 dB coarser. In FAAC a quadratic target rise above 8 kHz: +0.04 dB/kHz^2 -> +2.2 %, +0.08 -> +7.2 %, +0.16 -> +14.8 %; opposite sign (-0.04/-0.08) -> -0.2 % (CI spans 0). HF allocation tilt is flat; not the lever.

## Per-band cost comparison vs Apple at 160 kbps (read-only, velvet and take_your_finger)
Tools in `scripts/band_cost/`. Master `-b 160`, 10 s clips, long windows unless stated. Apple streams are larger than nominal: velvet 225 KB vs FAAC 201 KB (+12 %), take_your_finger 211 vs 201 KB (+5 %).
- **Per coded band, entropy coding is not the gap.** In 6-16 kHz both encoders spend about 1.7-1.8 bits per coded line at nonzero share 50-53 % and mean |q| of nonzero lines about 1.15 (velvet). Apple's HF waveform coding is sparse +-1 lines, not finer quantization.
- **The difference is which bands are waveform-coded.** Apple codes 99-100 % of 6-16 kHz bands with real codebooks (velvet) and 55-65 % zero bands above 16 kHz with no PNS. FAAC makes 34 % (6-12 kHz) and 41 % (12-16 kHz) of velvet bands PNS, and on take_your_finger 67 % of 12-16 kHz and 96 % of 16-24 kHz. Apple spends 3-4x the spectral bits in 12-16 kHz on take_your_finger (203 vs 58 bits per channel-frame) and 776 vs 530 bits in 6-16 kHz on velvet. Gating PNS by tonality did not recover this (see dead ends), so the open question is when waveform coding of those bands pays, not whether.
- **Noise shaping tilt differs and is clip dependent.** Mean scalefactor, FAAC minus Apple (1 step = 1.5 dB, lower = finer; coded long bands, ch0/ch1): take_your_finger 0-3 kHz -5.2/-5.1, 3-7 kHz -5.3/-5.6, 7-12 kHz +1.3/+1.5, 12-16 kHz +4.5/+5.5. FAAC is about 8 dB finer than Apple below 7 kHz and about 7 dB coarser at 12-16 kHz (survivor bias: only 31 % of those bands are coded). Velvet does not show it (0-3 kHz +4.1/+3.1, 3-7 kHz -0.5/-3.8, 12-16 kHz about 0), partly M/S confounded. The earlier HF-only coarsening test did not test the opposite tilt over 3-7 kHz.
- **Block switching differs on velvet.** Short blocks: FAAC 404 of 940 ICS (43 %) vs Apple 240 (26 %); take_your_finger 140 (15 %) vs 162 (17 %). Short windows cost more per frame at this rate, so velvet's gap may partly be window decisions.
- Re-checked on the benchmark's own 10 s centred clips (my first pass used the first 10 s): the numbers moved by under 1 % and every statement here holds. Caveats: two clips, one rung, master not #81, Apple overshoots the nominal rate so absolute scalefactors are not strictly comparable.

## Open leads
- Remaining worst clips vs Apple: velvet and take_your_finger at 128/160 kbps (~0.08 MOS, 25-30 kbps equivalent gap at equal rate; Apple overshoots nominal ABR on velvet ~10 %).
- Cost per coded HF band: Huffman codebook choice, scalefactor cost, band-wise bit allocation, compared with Apple streams.
- Cutoff: Apple codes ~20.2 kHz at 128-192k; FAAC default 18.75 kHz. Retest `-c` on top of #81.
- IS gating rate at 96 and 256/320 kbps (FAAC vs FAAC).
- Never measured: VBR/CBR, a held-out music set, the full ladder (every LC and HE rung) for each change.

## Measurement setup
Pitfall: the `faac` CLI refuses to overwrite an existing output (`Output file ... already exists (use --overwrite)`, exit status non-zero, stderr only), so a re-run into the same filename silently scores the previous encode. Use fresh directories per variant (the harness does) or pass `--overwrite`.
Local run: clone nschimme/faac-benchmark, PMLT2014 and SoundExpert with git (music is there, ~2 GB); venv with zimtohrli, soundfile, scipy; see HANDOFF s6. Speed: callgrind instruction counts; s390x CI compares decoded audio with a tolerance and FMA is kept (no `-ffp-contract=off`).

## Masking-target retune (PR #89, base = upstream master abcc332 + benchmark workflow)
- Gates are tighter than the budget noted above: `compare_results.py` fails throughput when the 95 % CI lower bound of candidate/baseline instructions exceeds 1.05 (warns at 1.02) and fails footprint at +2048 bytes (and +0.5 %) of `.text+.rodata+.data` unless `--footprint-allow`.
- CI table (gist 6c2ae3fe) is the `--gate` run: 4 clips (sandman, velvet, 21-classic, fms), not 49. On those 4 the gap to Apple is far larger than on the 49-clip average (BD +12 % vs +3.4 % before this change).
- Finding: on fms at 96/128 kbps Apple has 4-6 dB MORE waveform noise than FAAC in every band and still scores higher (Zimtohrli). SNR is not the lever; noise shape is. Apple also lowpasses at ~16 kHz (96k) / ~17.5 kHz (128k).
- Method: env-driven scratch hook over the quantizer's target (loudness exponent, average/peak weights, floors, smoothing, slope, 8-knot dB tilt over 0.5-18 kHz at 48 kHz), coordinate descent on 25 of 49 clips x 96/128/160/192 against m48. Accepted steps: t1 -1.5, t2 -1.5, t5 +3, t7 -3 dB, avgw 0.15, loud 0.35, slope x1.5 (-3.8 % in sample, -3.38 % on all 49 incl. 24 held out, -3.41 % at 44.1 kHz).
- Not in the final: PEAK weight, smoothing, floors and quiet-band constants were scanned in round 0 only and did not move.
- Scope: stereo LC, ABR/CBR, >= 32 kbps/ch. VBR, HE, mono and the 24 kbps stereo case verified bit-identical by decoded md5.

## Scalefactor search, predictor attempts and the delta-chain nudge (2026-10-16; base = #89 head)
Details, tables and scripts: `probe/sfpredict/RESULTS.md` on `claude/sfsearch-on-89`. 49 clips, ABR 96/128/160/192, Zimtohrli, BD-rate vs #89 (negative = better).
- **Exact search (#84) on #89:** -2.56% [-3.08,-2.04], +244% instructions (96.9M -> 333M Ir, 6 s, -b 160). A ±1 window is better than ±2: **-3.26%**. Without the scalefactor-delta cost the ±2 search drops to -1.54%.
- **Predictors replacing the search: dead end.** Decision trees (depth 2-6, class and regression) and a linear rule on cheap per-band features (gap = energy/allowed noise, est. qmax, peak/avg, width, block type, neighbours), trained on logged search choices: +0.3% to +2.6% BD vs #89 (constant shifts -1/0/+1: +0.13/0/+0.64%). They agree with the search 74% of the time but add ~4% scalefactor bits while the search removes ~8% (SFSTAT). Local cost changes are not predictable from features (R^2 0.44-0.54 for bit change, 0.01-0.30 for log distortion change). The offline Pareto metric did not reproduce the encoder ranking; do not select models with it.
- **Cost decomposition (±1 window, chain on):** distortion only -3.00%, spectral bits only -0.67%, chain only (no measurement) -1.2%.
- **Landed as a candidate: scalefactor nudge**, PR https://github.com/nschimme/faac/pull/94 (branch `claude/sfnudge-on-compact`, stacked on `codex/lc-mask-compact`; supersedes #93). On the compact base: 48 kHz -1.18%, 44.1 kHz -1.10%, +3.0..+3.5% Ir, text +1484 B. Forced on in a scratch build: mono -1.36%, HE-AAC v1 stereo -1.22% (not in the PR; brief keeps them unchanged). 3-state Viterbi on integer `book12` delta bits, one step either way, no extra quantizer call. 48 kHz -1.27% (worst cell -0.007), 4-clip CI subset -1.29%, 44.1 kHz -1.05%, 192-320 kbps -0.80%, 64-96 kbps -0.99%. Instructions +3.1..+3.6% on sandman/velvet/21-classic/fms, text +1142 B. VBR, mono, 5.1, HE and 20 kbps decode md5-identical. CI gate and s390x not yet run.
- **Verdict:** nudge is a keeper pending CI. Open lead: a cheap distortion term (ceiling -3.0%) inside the ~1.5% instruction headroom; per-line work must be limited to a subset of bands.
