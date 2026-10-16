# Scalefactor predictor vs search: measured results (2026-10-16)

Setup: base = PR #89 head (f159717); search = `claude/sfsearch-on-89`. 49 clips, ABR 96/128/160/192, 48 kHz, ffmpeg decode, Zimtohrli, BD-rate vs #89 (negative = better). Logger data: even clips (train), odd clips and 44.1 kHz (test) at 96-256 kbps, ~4M band rows.

## Search window (the teacher)
| search variant (SF_LO..SF_HI, SF_CHAIN) | BD-rate vs #89 |
|---|---|
| -2..+2 (as #84) | -2.56% [-3.08,-2.04] |
| **-1..+1** | **-3.26%** [-3.63,-2.88], worst -0.009 |
| -2..0 | -3.28% |
| -1..0 | -2.72% |
| 0..+1 | -2.39% |
| -2..-1 | -2.26% |
| -2..+2, no scalefactor-delta cost | -1.54% |

The delta-bit chain cost is worth about one point; the wide window is worse than ±1.

## Predictors (env-tunable build on `claude/sfpredict-tree-on-89`, local only)
All use cheap per-band features (gap = band energy over allowed noise, estimated qmax from peak and sfac, peak/avg, width, group size, block type, band index, neighbour wants). No extra quantizer call.
| predictor | BD vs #89 |
|---|---|
| tree d2 (±2 labels) | +2.57% |
| tree d6 (±2 labels) | +2.47% |
| regression tree d6 (±2) | +1.83% |
| linear in gap, 0..2, k=0.1 / 0.2 | +0.32% / +0.89% |
| class tree d4 (±1 labels) | +0.47% |
| regression tree d6 (±1 labels) | +0.34% |
Constant shifts -1/0/+1 are +0.13 / 0 / +0.64%. Every predictor loses to doing nothing.

## Why
- Frame bits (`SFSTAT=1`, -b 128, 3 clips): scalefactor bits base 120.8k/101.0k/117.8k, search ±1 110.2k/95.1k/105.9k (-8%), regression tree 125.3k/106.0k/121.7k (+4%). Independent per-band choices add scalefactor-delta bits; the search's Viterbi removes them.
- Offline accuracy is misleading: 74% agreement with the search label (57% for the constant), and a Pareto metric (distortion/allowed at equal bits, with delta bits, `tools/pareto2.py`) credits trees with ~24 of the search's 28 points. That metric does not reproduce the encoder ranking and must not be used for selection.
- The local costs themselves are poorly predictable: gradient-boosted regression of the bit change to ±1 reaches R^2 0.44 (pre-quantization features) / 0.50-0.54 (with q0, nz0, bits0); of log2 distortion change R^2 0.01 / 0.30. The search gains from measuring actual quantization error, which cheap features do not see.

## Cost of the search (callgrind, 6 s clip, -b 160)
base 96.9M Ir, search (±2) 333M Ir (+244%). `size_books` 103M (31%), `BlocQuant` self +104M (distortion loops), `quantize_sse2` 3.0M -> 18.5M (205k calls). One quantizer pass over everything is about +3%, so the +5% gate allows roughly one extra quantization and no exact per-candidate distortion or Huffman sizing.

## Cost decomposition of the ±1 search (SF_WD/SF_WB/SF_PEN, search binary)
| cost terms (±1 window, scalefactor-delta chain always on) | BD vs #89 |
|---|---|
| all terms (distortion + spectral bits) | -3.26% |
| distortion only | -3.00% |
| spectral bits only | -0.67% |
| chain only, deviation penalty 0 / 0.25 / 0.5 / 1 / 2 / 4 bits per step | -1.19 / -1.27 / -1.28 / -1.23 / -1.04 / -0.55% |
Distortion is the lever; the chain alone is worth -1.2% with no measurement at all.

## What landed as a candidate: scalefactor nudge (`sfnudge_on_89.patch`, branch `claude/sfnudge-on-89`, local)
Each coded band may move its scalefactor one step either way; a 3-state Viterbi over the group minimizes the real Huffman bits of the scalefactor deltas plus a quarter bit per step. Integer costs (table lookup of `book12` lengths), no extra quantizer call, no float compare added. Gated like the masking target (set-rate stereo LC, non-HE, >= 32 kbps per channel).
- 48 kHz 96-192: **-1.27%** [-1.43,-1.11], worst cell -0.007; 4-clip CI subset -1.29%; dMOS +0.0072/+0.0049/+0.0021/+0.0010.
- 44.1 kHz 128-256: -1.05% [-1.23,-0.89], worst -0.024. 48 kHz 192-320: -0.80%, worst -0.001.
- Instructions (callgrind, 6 s clip, -b 160): 96.92M -> 100.08M (+3.3%, inside the 1.05 gate, above the 1.02 warning).
- Footprint (`size`): text +1142 B, data -16 B vs #89 (gate +2048 B).
- VBR (-q 100, -q 200), mono, 5.1, HE-AAC v1 and a 20 kbps stereo stream decode to identical md5 vs #89.
- Not yet run: the actual CI gate, s390x.

## Verdict
Per-band feature predictors that replace the search: dead end; the sf-delta nudge above is the part of the search that fits the gates. Remaining lead: add a cheap distortion term (distortion only is -3.00%) within the ~1.7% instruction headroom (+0.3 to +2.5% vs #89, no instruction or footprint cost measured because they do not pay). The exact search at ±1 is the best quality found (-3.26%) but is far outside the throughput gate. Untested lead: an approximate search (table-based bit estimate, cheaper distortion, ±1, one-sided) to get under +5%; the profile suggests 5-10x is needed.
