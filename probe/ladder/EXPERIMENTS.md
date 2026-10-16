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

### Coarser HF target above 8 kHz (inspired by FFmpeg's NMR coder; FFmpeg is LGPL so its source may be read)
FFmpeg `aaccoder_nmr.h` is a Viterbi scalefactor search with lambda bisected to the bit budget; a comment claims Apple codes 7.5-12.6 kHz ~6 dB and 12.6 kHz+ ~28 dB coarser. In FAAC a quadratic target rise above 8 kHz: +0.04 dB/kHz^2 -> +2.2 %, +0.08 -> +7.2 %, +0.16 -> +14.8 %; opposite sign (-0.04/-0.08) -> -0.2 % (CI spans 0). HF allocation tilt is flat; not the lever.

## Per-band cost comparison vs Apple at 160 kbps (read-only, velvet and take_your_finger)
Tools in `scripts/band_cost/`. Master `-b 160`, 10 s clips, long windows unless stated. Apple streams are larger than nominal: velvet 225 KB vs FAAC 201 KB (+12 %), take_your_finger 211 vs 201 KB (+5 %).
- **Per coded band, entropy coding is not the gap.** In 6-16 kHz both encoders spend about 1.7-1.8 bits per coded line at nonzero share 50-53 % and mean |q| of nonzero lines about 1.15 (velvet). Apple's HF waveform coding is sparse +-1 lines, not finer quantization.
- **The difference is which bands are waveform-coded.** Apple codes 99-100 % of 6-16 kHz bands with real codebooks (velvet) and 55-65 % zero bands above 16 kHz with no PNS. FAAC makes 34 % (6-12 kHz) and 41 % (12-16 kHz) of velvet bands PNS, and on take_your_finger 67 % of 12-16 kHz and 96 % of 16-24 kHz. Apple spends 3-4x the spectral bits in 12-16 kHz on take_your_finger (203 vs 58 bits per channel-frame) and 776 vs 530 bits in 6-16 kHz on velvet. Gating PNS by tonality did not recover this (see dead ends), so the open question is when waveform coding of those bands pays, not whether.
- **Noise shaping tilt differs and is clip dependent.** Mean scalefactor, FAAC minus Apple (1 step = 1.5 dB, lower = finer; coded long bands, ch0/ch1): take_your_finger 0-3 kHz -5.2/-5.1, 3-7 kHz -5.3/-5.6, 7-12 kHz +1.3/+1.5, 12-16 kHz +4.5/+5.5. FAAC is about 8 dB finer than Apple below 7 kHz and about 7 dB coarser at 12-16 kHz (survivor bias: only 31 % of those bands are coded). Velvet does not show it (0-3 kHz +4.1/+3.1, 3-7 kHz -0.5/-3.8, 12-16 kHz about 0), partly M/S confounded. The earlier HF-only coarsening test did not test the opposite tilt over 3-7 kHz.
- **Block switching differs on velvet.** Short blocks: FAAC 404 of 940 ICS (43 %) vs Apple 240 (26 %); take_your_finger 140 (15 %) vs 162 (17 %). Short windows cost more per frame at this rate, so velvet's gap may partly be window decisions.
- Caveats: two clips, one rung, master not #81, Apple overshoots the nominal rate so absolute scalefactors are not strictly comparable.

## Open leads
- Remaining worst clips vs Apple: velvet and take_your_finger at 128/160 kbps (~0.08 MOS, 25-30 kbps equivalent gap at equal rate; Apple overshoots nominal ABR on velvet ~10 %).
- Cost per coded HF band: Huffman codebook choice, scalefactor cost, band-wise bit allocation, compared with Apple streams.
- Short-window rate: FAAC picks short blocks for ~17 % of frames vs Apple ~9 % (proxy source, unconfirmed); stricter block-switch threshold at high rates.
- Cutoff: Apple codes ~20.2 kHz at 128-192k; FAAC default 18.75 kHz. Retest `-c` on top of #81.
- IS gating rate at 96 and 256/320 kbps (FAAC vs FAAC).
- Never measured: VBR/CBR, a held-out music set, the full ladder (every LC and HE rung) for each change.

## Measurement setup
Local run: clone nschimme/faac-benchmark, PMLT2014 and SoundExpert with git (music is there, ~2 GB); venv with zimtohrli, soundfile, scipy; see HANDOFF s6. Speed: callgrind instruction counts; s390x CI compares decoded audio with a tolerance and FMA is kept (no `-ffp-contract=off`).
