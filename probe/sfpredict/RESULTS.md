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

## Rebased onto `codex/lc-mask-compact` (PR nschimme/faac#94, supersedes #93)
Compact base vs #89 head: -0.04% BD (equivalent), text 81125 B vs 82355 B. Nudge on compact (gate = `cfg->mask_tuned`, no new field): 48 kHz 96-192 **-1.18%** (worst -0.008), 44.1 kHz -1.10% (worst -0.024), instructions +3.0/+3.1/+3.5/+3.3% on sandman/velvet/21-classic/fms, text +1484 B. VBR/mono/5.1/HE/20 kbps md5-identical. `-b` is the total rate: `-b 64` auto-selects HE, so the LC edge needs `--object-type lc` (mean dMOS +0.013; one outlier clip, bah, -0.130 at 64 but +0.083 at 68, noise).
Generalization (temporary env override forcing the gate on, same binary otherwise): **mono** (49 clips downmixed, 48/64/80/96) -1.36% [-1.68,-1.07], worst -0.016; **HE-AAC v1 stereo** (40/48/56/64) -1.22% [-1.50,-0.97], worst -0.048. Both would need a separate PR since the brief keeps them unchanged.

## Round 3: siblings, two-state nudge, distortion route (2026-10-16, later)
Sibling PRs read (#80 +34% Ir/-2.3% BD, #85 +87%/-1.6%, #86 +17%/-1.4%, #87 +11%/-0.7%, #84 +180%/-5.4% vs master, #23/#27 one step coarser): all pricing variants are over the +5% budget and include the delta-bit chain, so none isolates a distortion term. #23 found the finer step ~0 on HE-v1, matching the window sweep here.
- Window, chain only (no measurement), pen 0.25 bit: [-1,0] **-1.20%**, ±2 -0.27% (five states are worse), ±1 about -1.27%. #94 now uses two states (coarser only): 48 kHz -1.15%, CBR -1.29%, 44.1 kHz -1.08%, 32 kHz -2.01%, Ir +2.6..+3.1%, text +1100 B vs `codex/lc-mask-compact`.
- Distortion scaled from one measurement (±1, WB=0, chain on): X1 (exact d at the formula sf, d(±1)=d0*2^(-+1/2)) **-0.76%**, below chain-only, so per-band luck is what the search exploits; X3 (zeroed-line energy proxy) -0.63%; an inconsistent variant (centre exact, sides proxy with k=0.1) -1.50%, not a valid X3 result and not built on.
- Distortion on narrow bands only (full ±1 costs, WB=0, width <= W): W=4 -1.44%, W=8 -1.63%, W=16 -1.86%. Estimated cost from the sfb tables (96 lines of bands <= 8 lines per long block, 3 candidates): about +3.8% Ir for W=8 and +1.9% for W=4 on top of the nudge's ~3%, over the +5% gate even with two candidates. **Distortion route closed under the gates.**
- Generalization (3-state numbers then 2-state): mono 48 kHz -1.50%, mono 24 kHz -2.24%, HE-AAC stereo -0.82% (worst cell -0.131 on velvet at 48, erratic base curve; 3-state HE was -1.22% worst -0.048); HE at `-b 24` loses (worst -0.144) so the HE gate starts at 16 kbps per channel.
- CI cells checked locally before CI: 32 kHz stereo -2.01%, CBR -1.29%, mono 24 kHz -2.24%.

## Verdict
Per-band feature predictors that replace the search: dead end; the sf-delta nudge above is the part of the search that fits the gates. Remaining lead: add a cheap distortion term (distortion only is -3.00%) within the ~1.7% instruction headroom (+0.3 to +2.5% vs #89, no instruction or footprint cost measured because they do not pay). The exact search at ±1 is the best quality found (-3.26%) but is far outside the throughput gate. Untested lead: an approximate search (table-based bit estimate, cheaper distortion, ±1, one-sided) to get under +5%; the profile suggests 5-10x is needed.
