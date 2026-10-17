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

## Round 4 (advisor): CI triage, like-for-like checks, two-state distortion, knobs
- The red benchmark on #94 (head 010461b) was my own doing: pushing the two-state commit triggered `cancel-in-progress`, the amd64 ABR candidate run was cancelled (job 117189494827 concluded `cancelled`), and the report counted the missing MOS scores as regressions. Not a result for the change. The run on the current head is the one to read.
- HE like-for-like (40/48/56/64): two-state -1.14% vs three-state -1.22%; velvet at 48 is -0.131 in both HE comparisons' worst cell.
- LC edge `-b 64 --object-type lc`, two-state: mean dMOS +0.011, worst -0.070 (bah).
- Two-state (coarser only) with exact distortion on narrow bands (WB=0, pen 0.25): W=8 -1.50%, W=16 -1.80%, vs chain-only two-state -1.20%. Estimated extra cost with the centre quantization reused: about +2% (W=8) and +3% (W=16) Ir on top of the nudge's ~3%, so W=8 sits at the +5% gate and W=16 is over. Not built.
- Knobs on the two-state nudge (vs `codex/lc-mask-compact`): nudge cost 0/1/2/4 quarter bits = -1.15/-1.15/-1.09/-1.00%; `sf_smooth` 0.4/0.6/0.8 = -0.93/-1.15/-0.81%. Current values are the optimum. PNS-delta nudge not tried.

## Round 5: footprint/perf trim and a built narrow-band distortion prototype
- Pure refactor of #94 (nudge fills every coded band's scalefactor so pass 2 stops recomputing the smoothing; absolute scalefactors derived on the fly): same bitstream (decoded md5 identical to the previous head on 5 clips x 7 modes, plus mono and HE follow-up builds), text +608 B over `codex/lc-mask-compact` (was +1100), instructions +2.2/+2.7/+2.6/+2.5% on sandman/velvet/21-classic/fms (was +2.6..+3.1%). Local only until #95/#96 benchmarks finish (a push cancels them).
- Narrow-band distortion built in the encoder (bands <= 8 lines, exact error at the formula and one-step-coarser scalefactors, integer local cost in quarter bits): BD -1.43% vs -1.15% for the nudge alone, but **+11..+13% instructions** (about 4x the estimate: two quantizer calls plus a per-line error pass for every narrow band), text +1.2 KB. Dead end under the gates; branch discarded.

## Round 8: the 24 kbps-per-channel floor failed the CI mos gate on speech; floor now 28000; #101 merged
- The generalized gate (any channel count, >= 24 kbps per channel) failed `mos` on `7cc9822` in ABR (1 cell past -0.10) and CBR (3 past -0.10, worst -0.18): all 16 kHz mono speech at exactly 24 kbps per channel. Local corpus was music only, so the check missed it; the claim "no cell below -0.062 from 24 kbps/ch up" was wrong for speech.
- Local reproduction on the CI speech source (TCD-VOIP, 24 clean 16 kHz and 24 kHz clips plus 24 degraded VoIP clips): at 24 kbps mean MOS -0.016 (ABR) / -0.008 (CBR), worst -0.084 / -0.097; VoIP set -0.025 mean, worst -0.117. At 28000 or 32000 those streams equal the no-nudge base. 24 kHz speech at 28 kbps: mean +0.003 / -0.001, worst -0.037 / -0.087.
- CI cells at >= 28 kbps per channel on the same head were all inside the gate (24k_mono_28k +0.009 ABR / +0.014 CBR, worst -0.08; 24k_mono_32k +0.003 / +0.011; 48k_stereo_56k +0.013), and they do not change when the floor moves, so the floor was set to 28000 (one constant). Benchmark on `4ed511a`: Consolidated Report success, worst cells -0.07 (velvet 96k ABR) and -0.08 (24k_mono_28k CBR), footprint +1.03% (amd64 +0.75%), throughput -0.7%.
- #101 (group scratch reuse, -112 B text, -848 B stack frame) merged into #94's branch after: merge-result decoded md5 identical to `4ed511a` on 25 modes (above and below the floor, VBR, a transient clip), array shrink checked group-local (`target[sfb]`, `sfb < sfbn <= NSFB_LONG`).
- Lesson: validate the rate floor on the benchmark's own speech corpora (TCD-VOIP is git-cloneable), not music alone.

## Verdict
Per-band feature predictors that replace the search: dead end; the sf-delta nudge above is the part of the search that fits the gates. Remaining lead: add a cheap distortion term (distortion only is -3.00%) within the ~1.7% instruction headroom (+0.3 to +2.5% vs #89, no instruction or footprint cost measured because they do not pay). The exact search at ±1 is the best quality found (-3.26%) but is far outside the throughput gate. Untested lead: an approximate search (table-based bit estimate, cheaper distortion, ±1, one-sided) to get under +5%; the profile suggests 5-10x is needed.
