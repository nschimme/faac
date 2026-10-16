# Brief: train a lightweight scalefactor predictor to replace the search (FAAC)

Repo: https://github.com/nschimme/faac (owner Nils). Goal of the project: improve FAAC high-bitrate LC quality at 44.1/48 kHz toward and beyond Apple's encoder.
Read first: `probe/ladder/EXPERIMENTS.md` on branch `claude/trusting-hamilton-uiadf9` (measured dead ends; log every experiment there when done, with result, branch/PR and verdict).

## The task (Nils's words, 2026-10-16)
"Adapt #84 to log data and then train a lightweight model that predicts the scalefactor rather than does a search for it. We might need cheap feature engineering too. The same footprint and performance gate applies."

## Gates that decide whether it lands (from faac-benchmark `compare_results.py`)
- Throughput: fails if the 95% CI lower bound of candidate/baseline instruction count exceeds 1.05 (warns above 1.02). Budget is therefore about +2-5%, not the +10-20% in older notes.
- Footprint: fails at +2048 bytes (and +0.5%) of `.text+.rodata+.data` of the release libfaac.so (`size`). Tables and code both count.
- Quality: BD-rate must not regress by more than +0.50%; Nils also watches the worst clip x rung cell against -0.10 MOS.
- VBR output must stay bit-identical; HE-AAC, mono and 5.1 must stay unchanged. Gate any new behaviour to ABR/CBR stereo LC (see `QuantizeSetMaskTune` gating in `libfaac/frame.c` on PR #89).
- FMA stays on (no -ffp-contract=off). Anything that decides by float comparisons must be robust to last-bit differences (s390x CI compares decoded audio with a tolerance).
- Do not read Apple or FDK sources. FFmpeg (LGPL) may be read.

## Base state
- Upstream master is FreewareAdvancedAudio/faac (remote `upstream`), at abcc332. nschimme/faac master trails it. PRs go to nschimme/faac with a base branch that mirrors upstream master plus the benchmark workflow: `claude/upstream-base`.
- PR #89 (head `claude/project-thread-0yqymb`, Nils is taking it over) retunes the masking target: -3.4% BD-rate, +1.3% instr, +1344 B. Stack on it, and measure against it.
- Branch `claude/sfsearch-on-89` = #89 + the exact search from PR #84 (Viterbi over scalefactor candidates sfac-2..sfac+2 per band, cost = distortion/allowed + lambda*real Huffman bits, `band_spectral_bits` and `sf_delta_bits` in huff2.c). Diff also in `/mnt/project-files/sfpredict/sfsearch_on_89.patch`. Search is on for ABR/CBR >= 48 kbps per channel (`SF_SEARCH_BITRATE`).

## What I measured (49 clips, ABR 96/128/160/192, 48 kHz, ffmpeg decode, Zimtohrli, own harness)
- #89 alone vs upstream base: -3.39% BD-rate [-3.93, -2.81].
- Exact search on top of #89 (branch above): a further -2.56% BD-rate [-3.08, -2.04], worst cell -0.046 (velvet @96), dMOS +0.011/+0.010/+0.004/+0.000 at 96/128/160/192. Cost is ~+180% instructions as in #84. So the ceiling for a predictor is about -2.5%; the gain sits mostly at 96-160 kbps and vanishes at 192.
- Earlier finding from #80/#84-#87: per-band rate depends on the largest quantized value (codebook class cliffs), which is why a histogram rate model lost most of the gain.

## Suggested plan
1. Rebuild the harness (below). Build baseline = #89 head and search = `claude/sfsearch-on-89` (meson release builds in separate dirs).
2. Logger: in the search loop (`assign_band_codebooks`, `libfaac/quantize.c`), dump one row per coded band: block type, band index, width, group size, rate per channel, target level vs band energy, peak/avg ratio, quantized max/nonzero count/count at max at the formula scalefactor, spectral bits there, neighbour wants, previous band's choice, and the label (chosen offset -2..+2) plus the per-candidate local costs. Keep it env-gated and out of the final PR.
3. Generate data on a training split (e.g. even-indexed clips), train a tiny model (decision tree, a few boosted stumps, or a small linear scorer) with sklearn or numpy, and evaluate on odd-indexed clips and on the 44.1 kHz corpus. Judge by BD-rate in the real encoder, not by classification accuracy.
4. Port the model as a handful of constants and branches; at most one extra quantizer call for bands whose prediction is nonzero. Re-check instructions (callgrind on a 6 s clip at -b 160) and footprint (`size`).
5. Verify the whole ladder: LC 48 kHz 96-320, 44.1 kHz 128-256, plus VBR/HE/mono bit-identity (compare `ffmpeg -i out.m4a -f md5 -`; do not compare the m4a bytes, the header embeds the build hash).
6. Open each measured variant as its own PR against nschimme/faac (base `claude/upstream-base`, or the #89 branch if stacking), assign and request review from nschimme, and log the experiment in EXPERIMENTS.md.

## Harness (files in /mnt/project-files/sfpredict/tools)
- Corpus: `git clone` nschimme/PMLT2014 and nschimme/SoundExpert (music; ~2 GB; use GIT_LFS_SKIP_SMUDGE=1), then `mkcorpus.sh RATE OUTDIR` makes the 49 centred 10 s stereo clips at 48000 and 44100. Github gist and archive hosts may be blocked; git clones work.
- `python3 -m venv`, `pip install zimtohrli soundfile scipy numpy ffmpeg-python meson`; clone nschimme/faac-benchmark and symlink it to `/tmp/claude/fb` (the scripts import `phase2_mos` and `scripts/bd_rate.py` from there). Needs ffmpeg.
- `score.py TAG FAAC_BIN CORPUS_DIR` (env RUNGS=96,128,160,192; CLIPS=regex; HARNESS_ARGS) writes `/tmp/claude/h/TAG.json`; `compare_bd.py BASE_TAG CAND_TAG` prints BD-rate with bootstrap CI, worst cell and per-rung dMOS/dBR; `apple_score.py` and `vs_apple.py` compare with Apple's streams (in branch `claude/trusting-hamilton-uiadf9`, `probe/ladder/ref/apple-s11-367cf8dc/lc`). One 49-clip x 4-rung evaluation takes about 2 minutes on 4 cores.
- Pitfall: the faac CLI refuses to overwrite an output unless `--overwrite`.

## Caveats
- CI's gist table uses a 4-clip subset (sandman, velvet, 21-classic, fms), where the gap to Apple is larger than on the 49-clip average. Report both.
- My numbers are 49-clip local; CI is the arbiter. Nils weighs averages over a single worst clip as long as the -0.10 gate holds.
