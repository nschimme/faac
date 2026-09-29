# Plan: close FAAC's MOS gap to Apple and fdk-aac (handoff, 2026-09-29)

You are picking up a research programme on the FAAC AAC encoder (`nschimme/faac`).
The goal is to reach the perceptual quality (MOS) of Apple's AAC encoder, the
primary reference, and fdk-aac, the secondary one, without giving up FAAC's small
size and speed. This file is self-contained. Read it fully, then
`probe/ladder/LADDER_RESULT.md` from "## Apple-aligned ladder" onward, before
running anything.

Out of scope: throughput and footprint optimisation (another agent owns it);
upstream (`knik0/faac`) PRs; merging any probe branch.

## 0. Status after Stage S2 (2026-09-29, second session) — read this first

Details: `LADDER_RESULT.md` → "Stage S2". Branch with everything: `claude/trusting-hamilton-uiadf9`
(probe merged with master ec72dfc; block-switch probe knobs added). It supersedes `fdk-ladder`.

**Gap now (Apple 128k refs vs FAAC, 49 clips, bits-adjusted):** master +0.021 (Apple 37/11);
master + #595 + #599 **+0.007** (31/18). fdk ≈ par at LC ≥ 128k.

**Open PRs on nschimme/faac:** #595 (sf smoothing; workstream A closed, no gate, no change),
#599 (LC drop-out ratio 12 in blockswitch; +0.0061 vs master, HE bit-identical),
#596 (LFE forced long, spec fix), #597 (no empty M/S mask). #595 + #599 were measured together
only locally (+0.0138 vs master); CI benchmarks each alone.

**Closed this round:** A (VBR-44.1k BD is the 64k rung folded into the LC ladder by
bd_rate.py — a faac-benchmark issue; speech is noise; 5.1 is a scorer artefact on a synthetic sine).
B: extra smoothing above 6 kHz (≤ +0.0004) and low-band offsets (peak +0.002 at L2/L3) are dead
on the #595 base. Window rise ratio, no-hysteresis, no-drop-outs, and any drop ratio on HE are dead.

### Next steps, in order

1. **D — M/S, on top of #595 + #599** (ceiling ≈ +0.013 clean, covers the +0.007 left).
   Build a probe base = master-based probe with `FAAC_SF_SMOOTH=0.6 FAAC_BS_DROPRATIO=12`
   (LC). Re-dump it on the +64 inputs (`scripts/s2/b0_prepare.py` pattern) and first rerun the
   reverse single-swap arm rMS (Apple's M/S mask into FAAC decisions, step1, F2/G2 pattern)
   and the SF+MS pair, to get the current oracle. Controls K0/KF first. Then characterise where
   Apple uses M/S and FAAC does not (band region, L/R correlation, energy ratio) and screen a
   rule in `stereo.c` offline before the encoder.
2. **C, second pass — windows** (velvet still 61 % short vs Apple 25 %; Greensleeves 58 % vs 11 %).
   Candidates not yet tried: level recovery after an attack (reset/decay of `level` after a
   transient), the ±2 sub-block context span (`FAAC_BS_PREVS/NEXTS`), and a minimum-energy
   floor on rises (`FAAC_BS_MINE`). Sweep with `scripts/s2/sweep.py` on top of #595 + drop 12;
   same pre-registered rule (≥ +0.005, W>L, no clip < −0.05, bracket centre). Watch girl,
   Last_Of_The_Mohicans, liberate and take_your_finger — they are the sentinels.
3. **E — other rates and HE-AAC** (largest absolute gap). **The Apple refs now exist** (2026-09-29,
   see `probe/ladder/ref/README.md`): `ref/apple_lc64k`, `ref/apple_lc96k`, `ref/apple_he32k`, 49 clips
   each, same clip names as `ref/apple` (128k). Made with `afconvert -f m4af -d aac@48000 -b N` (LC) and
   `-d aach@48000 -b 32000` (HE); the 128k settings were recovered as `-d aac -b 128000` (PCM-identical
   re-encode of one clip). Realised rates overshoot the request (64k→72k, 96k→106k, HE 32k→38.5k):
   compare bits-adjusted, and re-derive the FAAC slope pair around each rung (e.g. 56/72k, 80/112k, HE 28/40k).
   **Do the HE prerequisites first (E0), before any arm:**
   - E0.1 Alignment. Apple HE priming is 2112 in afinfo, probably core-rate samples (HE `valid frames`
     are core-rate too). Decode with FAAD_LADDER_DUMP and find the true input offset by cross-correlating
     the decoded Apple PCM against the source (`mdct_offset.py` pattern); expect something other than +64
     at 48k. Same for FAAC's own HE stream (FAAC pairs core frame n with Apple frame n+k; find k).
   - E0.2 Controls on the HE core: Control 0 (re-emit of the Apple HE core, bit-exact PCM through FAAC's
     writer; SBR payload passes through untouched), KA (step1 at all-Apple core decisions equals the
     re-emit within the LC-style 97-99 % integer match) and KF (step1 at FAAC's own core decisions =
     normal FAAC PCM). Only after all three pass, run arms A/F/reverse/W.
   - E0.3 Rate-loop caveat: at HE the FAAC core runs at the lowered core rate; the SBR payload is
     Apple's in these controls, so score core-only effects with the SBR held identical (or compare
     FAAC-core+FAAC-SBR vs Apple only at the end).
   - LC 64k/96k need only the known +64/+1 alignment; rerun K0/KF, then A/F/W. Note at HE the block
     switcher wants MORE shorts than LC (any drop-ratio raise lost −0.02…−0.03 at 32k).
4. **Optional small PR (faac-benchmark, not faac):** keep the 44.1k VBR q76 rung out of the LC
   BD-rate ladder, or use piecewise-linear BD; it produces a spurious +1 % mean.

### Reference sets verified (2026-09-29, after the Mac agent's commit 2f50e92)
- `ref/apple_lc64k`, `ref/apple_lc96k`: 49/49 LC, 48 kHz stereo; same ~0.05 s end padding as
  `ref/apple`. `ref/apple_he32k`: 49/49 HE-AAC (SBR), 48 kHz out; ~0.12 s longer than the source
  (priming/padding in core-rate units — E0.1). The FAAD dump decoder parses the HE core
  (velvet: 237 core frames, 0 concealment; Apple's HE core is 54 % short on velvet).
- **(Superseded by S3: this run encoded FAAC 64k as HE-AAC; see LADDER_RESULT "Stage S3-D".)** - **LC 64k pipeline verified end to end:** `LADDER_REF=apple_lc64k LADDER_RATE=64 LADDER_SLOPE=56,72`
  → g2_prepare → g2_make → g2_controls: **Control 0 49/49 exact, KA 49/49, KF 49/49 (PCM)**.
  LC 96k uses the same code path (`LADDER_REF=apple_lc96k LADDER_RATE=96 LADDER_SLOPE=80,112`); not yet run.
- G2 scripts now take `LADDER_REF` (ref subdir, default `apple`), `LADDER_RATE` (default 128) and
  `LADDER_SLOPE` (slope anchors, default `112,144`; baselines are named `base<rate>`).
  Use a separate `LADDER_WORK` per rate. `scripts/g/control0.py` runs Control 0 for any LC set
  (`LADDER_PRIME`, default 2112). It is LC-only: for HE, E0.2 needs an SBR-passthrough variant.
- `g2_controls.py` KF now falls back to decoded PCM when ADTS bytes differ (the #597 empty-M/S-mask
  case; 4/49 at 64k, 1/49 at 128k) and records `KF_note`.

### Session facts that save time
- Linux env: `export PATH=/opt/venv312/bin:$PATH` (py3.12 venv with zimtohrli+visqol);
  scorer `/opt/faac-benchmark/scripts/score_clip.py`; FAAD dump decoder
  `/tmp/faad-ladder-dump/build_faad/frontend/faad`. Rebuild with `probe/ladder/jules_setup.sh`
  in a fresh container (datasets: GitHub archive zips 403 through the proxy — clone
  nschimme/{pmlt2014,tcd-voip,soundexpert} and zip them into faac-benchmark `data/temp/`;
  `setup_datasets.py` needs Python 3.12).
- Frame alignment for FAAC block-switch dumps vs the Apple dump is **f+2** (inject offset),
  while step1 uses `FAAC_STEP1_OFFSET=1`.
- Block-switch probe knobs (all unset = production): `FAAC_BS_RATIO`, `FAAC_BS_DROPRATIO`,
  `FAAC_BS_SMOOTH`, `FAAC_BS_MINE`, `FAAC_BS_PREVS`, `FAAC_BS_NEXTS`, `FAAC_BS_NOHYST`,
  `FAAC_BS_DUMP=<file>` (per frame: frame ch psy desire final attackmask level eng[24]).
- One encode+score pass over 49 clips at 128k ≈ 2 m 45 s; two serial scorers side by side were
  fine on 15 GB. Don't `pgrep -f` for a pattern contained in your own wait-loop command line.
- KF compares ADTS bytes in `g2_controls.py`; after the master merge one clip differs only by the
  empty-M/S-mask bug (#597) with identical PCM — compare PCM.
- **Disk:** a G2 work dir is 4–7 GB per rate; the session allowance filled once (tool output is
  lost with ENOSPC; deletes still work). Delete work dirs you are done with.
- The `scripts/s2/` files hard-code `/home/user/...` work paths; parametrise before reuse.

---

## 1. Where things are

| Thing | Location |
|---|---|
| Probe branch (never merge) | `fdk-ladder` on `nschimme/faac`: all ladder code, the Apple refs, scripts, results. This file lives there. |
| Apple 128k references (49 clips, LC, 48 kHz stereo) | `probe/ladder/ref/apple/*.m4a` |
| fdk references (5 clips) | `probe/ladder/ref/fdk/*.m4a` |
| Linux bootstrap | `probe/ladder/jules_setup.sh` (builds the probe libfaac, `reemit_tool`, the FAAD3 dump decoder from branch `faad-ladder-dump`, faac-benchmark with its corpus, zimtohrli). Controls reproduced on Linux: see "## Linux reproduction (Jules)". |
| Benchmark harness | `https://github.com/nschimme/faac-benchmark`; per-clip scorer `scripts/score_clip.py <src.wav> <decoded-or-m4a>` (zimtohrli, prints `MOS: x`) |
| Candidate encoder change under test | PR nschimme/faac#595, branch `sf-smooth` (one commit on `master`) |
| CI benchmark | Opening or updating a PR on `nschimme/faac` runs `.github/workflows/benchmark.yml`: ABR amd64, CBR arm64 and VBR arm64 over the whole ladder, bits-adjusted vs base, BD-rate, throughput, footprint. The per-rate reports are in the job logs, step "Generate Per-Rate Control Report". |

**Known CI plumbing bug:** when a per-rate gate fails, the job stops before
`upload-artifact`. The "Consolidated Report" then prints "No result pairs found"
and `cases_*.md` never uploads. Read the per-rate logs instead
(`gh run view <run> --log | grep 'Generate Per-Rate'`). Fixing this with
`if: always()` on the upload steps is a welcome small PR, separate from the
research.

## 2. What is established (LC 128k, 48 kHz stereo, 49 clips unless stated)

"adj" means bits-adjusted MOS: per clip, slope = (MOS144 − MOS112) / log2(bytes144/bytes112)
from FAAC's own 112/144k encodes, and adj Δ(X vs Y) = ΔMOS − slope·log2(bytesX/bytesY).

1. **Apple leads FAAC-128 by +0.042 adj.** fdk is roughly at par at LC ≥ 128k.
   HE-AAC is where FAAC trails both most, by −0.07 to −0.13 (older measurement).
2. **The ladder method** (the only method that has worked): decode a reference
   stream to its exact decisions (FAAD_LADDER_DUMP → `parse_dump.py`), re-emit
   them with FAAC's own writer (control: bit-exact PCM), then run FAAC's MDCT and
   quantizer at those decisions (`FAAC_STEP1`, "step1"). Swap one decision at a
   time between the reference and FAAC.
   - With Apple aligned (+64 samples input pad, +1 frame), FAAC's quantizer at
     ALL of Apple's decisions (arm "A") equals Apple (adj vs Apple −0.006) and
     reproduces 97–99 % of Apple's integers. **Apple's lead is entirely in
     DECISIONS, not the quantizer.**
   - fdk is different: its integers aren't plain rounding (61–79 % match), and
     its scalefactors are tuned to its own line rounding. Transplanting fdk
     decisions into FAAC is a mismatch. **Work against Apple; fdk parity follows
     from the same levers or comes later.**
3. **Which decisions carry it** (arm "F" = all-FAAC decisions; A−F = +0.034 adj):
   - Forward (put FAAC's decision into all-Apple): scalefactors lose 101 % of A−F,
     windows 75 %. Reverse (put Apple's into all-FAAC): scalefactors +0.017 (50 %),
     windows +0.020 (58 %).
   - **Apple windows + Apple scalefactors, with FAAC's own band types, M/S and TNS,
     recover the ENTIRE gap (+0.034, 45 wins / 4 losses).** Band types, bandwidth
     and TNS need no change.
   - Apple windows alone, with FAAC re-deciding everything else (core-inject arm
     "W"), are bimodal. They get ~100 % on bas/Changes/mof/trumpet, 17 % on
     velvet, and lose on Last_Of_The_Mohicans (−0.23) and girl.16b48k (−0.46).
   - FAAC uses short windows on 84–99 % of blocks on transient/speech clips, vs
     Apple's 8–25 %. Across the 49 clips: 31 % short at LC and 59 % at HE for FAAC,
     vs 4–10 % for the references.
4. **The scalefactor difference** (FAAC − Apple, same-layout coded bands): FAAC is
   finer below 2 kHz and 1–1.4 steps coarser above 6 kHz, with a 2.5–4-step
   per-band spread. The pattern is the same in normal long blocks and in blocks
   where Apple goes long and FAAC short.
   - Fit: FAAC sf − Apple sf ≈ 0.61·(sf − mean of coded ±1 neighbours); R² 0.09.
     With a per-band tilt and level vs global gain, R² reaches 0.21.
   - Spectral features, 5 clips: relative neighbour energy, peak/avg and flatness
     give R² 0.12 (0–2k) rising to 0.47 (>12k), with a coefficient of 0.5 → 3.1
     steps/decade that grows with frequency.
5. **PR #595 (the neighbour-smoothing rule, strength 0.6, long blocks only):**
   - Offline (step1): +0.010 adj, 49/0. Tilt alone loses.
   - In the encoder with the rate loop on: +0.0066 at 128k (46/3), bytes unchanged.
   - CI BD-rate (negative is better):

     | Ladder | ABR | CBR | VBR |
     |---|---:|---:|---:|
     | 32k LC | −1.80 % | −1.85 % | −1.54 % |
     | 44.1k LC | −1.90 % | −1.85 % | +1.07 % (median −0.21 %) |
     | 48k LC | −1.62 % | −1.45 % | −1.29 % |
     | 48k HE | −0.66 % | −1.00 % | −0.91 % |

   - Throughput is −2 %, owned by the other agent.
   - The rule barely helps long blocks survive (+0.006 of the +0.022 Apple's own
     scalefactors give at Apple windows). Smoothing is a broad independent gain,
     not the whole allocation story.

## 3. Workstreams, in priority order

For each: state the ceiling as a share of the remaining gap, pre-register a
decision rule BEFORE measuring, run controls, then measure on all 49 clips.

### A. Finish #595 (short)
- **VBR 44.1k:** mean +1.07 % but median −0.21 %. Find the outlier clips in the
  VBR per-rate log (the "Top Regression" table and the per-scenario rows). Encode
  those clips locally with `master` and `sf-smooth` at the affected `-q`, score
  them, and explain.
- **Low-rate 16 kHz mono speech:** 16k_mono_20k is 2 wins / 6 losses. The worst,
  R_02_COMPSPKR_FA, is −0.10.
- **5.1 "6_Channel_ID":** in CI this is a SYNTHETIC six-sine file, because
  faac-benchmark's download returns 404 and it falls back to `aevalsrc` sines. CI
  shows −0.11 at 96k. The local zimtohrli scorer saturates at 4.9998 on both.
  Treat it as a pure-tone edge case, not surround music. Check whether the loss is
  real in CI's scorer before designing around it.
- Only if a class of loss is real and systematic: gate the rule (e.g. by channel
  config or bandwidth), re-measure, and push to #595. Do NOT change strength 0.6
  without a new bracketed sweep.

### B. The rest of the scalefactor allocation (ceiling ≈ +0.010 adj at 128k)
The Apple-scalefactor oracle is +0.017; the rule gets about +0.007 in the encoder.
- Refit on top of #595. Dump the #595 encoder (probe env `FAAC_SF_SMOOTH=0.6`)
  vs Apple and fit the residual.
- Candidate terms: a frequency-dependent smoothing strength; peak/avg or
  relative-energy terms above 6 kHz; level relative to the frame. Constant
  offsets and tilts are DEAD.
- Screen offline with step1 (`probe/ladder/scripts/r/r_make.py` + `r_run.py`
  pattern). Fit on even clips and test on odd ones (and vice versa). Known-answer
  arm K0 must be PCM-identical to the normal encode.
- Promote to an encoder change only if the offline arm reaches ≥ +0.005 adj with
  wins > losses and bytes within ±12.5 %. Then measure with the rate loop on,
  then CI.

### C. The window decision (ceiling ≈ +0.020 adj; +0.022 on the 14 window-heavy clips with #595)
- Goal: go long where Apple does on velvet/bas/Changes/Shinsho/take_your_finger,
  but stay short on girl.16b48k and Last_Of_The_Mohicans.
- Step 1: per frame, dump FAAC's block-switch detector inputs (attack energy
  ratios, PE, whatever `libfaac/blockswitch.c` uses). Label frames by
  (FAAC short, Apple long) and by the W-arm per-clip outcome. Find a feature or
  threshold that separates the good-long frames from the girl/Mohicans frames.
- Step 2: sweep block-switch thresholds with #595 on, 49 clips, bits-adjusted.
  The control is the threshold at its current value reproducing `master`. The
  older all-long losses (−0.10 to −0.16) came from the pre-#595 allocation and
  must be re-checked, not assumed.
- Watch HE too: FAAC is 59 % short at HE vs 6–8 % for the references.

### D. M/S, after B and C (ceiling ≈ +0.013 on clean clips)
- Apple codes M/S on 87–92 % of bands vs FAAC's 50 %.
- Apple's M/S alone LOSES inside FAAC (−0.023). Paired with Apple's scalefactors
  it lifts recovery from 66 % to 85 % on clean clips.
- Revisit only once FAAC's scalefactor curve is closer to Apple's.

### E. Other rates and HE-AAC (largest absolute gap; needs the user)
- Everything above was measured at LC 128k. Rerun the swap matrix (A, F, the
  reverse arms, W) at LC 64k/96k and on the HE-AAC 48k core.
- **Apple references can only be made on macOS (afconvert).** **Done 2026-09-29**: the 64k/96k/HE-32k sets are in `probe/ladder/ref/`.
  Ask the user only for further rates. Don't try to synthesise Apple streams.
- The HE-core path through the ladder is untested. Do E0 in section 0 (alignment, then Control 0,
  KA, KF) before any arm.

### F. fdk-aac
Deprioritised. Go back to it only if Apple parity is reached, or the user asks.
Never transplant fdk's per-line integers or scalefactor shape into FAAC's rate
loop: both are dead.

## 4. Method rules (each exists because it was broken once)

- **Controls first, every arm.** Known-answer arms must pass before any score is
  read. Examples: reference re-emit = reference PCM; step1 at all-Apple = the KA
  bytes; step1 at FAAC's own decisions = normal FAAC; the injection at FAAC's own
  dump = normal FAAC; a new knob at its neutral value = master. **Compare decoded
  PCM, not file bytes:** the MP4 embeds the git hash, and a `-dirty` tree changes
  16 bytes.
- **Pre-register** the decision rule (numbers) before the run, and apply it as
  written.
- **Bits-adjusted MOS** with the per-clip 112/144 slope. Report wins and losses,
  not just means. Flag clips whose byte ratio is outside ±12.5 % (step1 has no
  rate loop, so bytes drift).
- **All 49 clips for decisions.** The 5-clip set (Severance, 21-classic, velvet,
  24-Greensleeves, 12-German) is for debugging only.
- **Tune at the bracket centre:** a strength or threshold is chosen only when both
  neighbours are worse.
- **Size each lever against the gap** (ceiling / remaining gap) before building.
- **CI is the final judge** for encoder changes: BD-rate across the ladder, all
  three rate-control modes, and the HE rungs.
- Read your own tables. Summaries from earlier agents have had sign errors and
  script bugs.

## 5. Dead ends (do not re-propose without new evidence)

- Constant scalefactor offsets, and fitted per-band tilts.
- 3GPP-style masking spreading/tonality allocation (best +0.004).
- Per-line RDO; hole avoidance; PNS dead-zone; noise islands.
- KBD window shape.
- Any fdk decision transplant into FAAC's rate loop.
- Forcing windows alone with FAAC's old allocation.
- Injecting a reference's scalefactor *shape* inside FAAC's rate loop
  (`FAAC_CORE_INJECT` `sf`/`win,sf`/`class`): it loses on every clip (up to −2 MOS).
  Use step1 (absolute scalefactors, no rate loop) for scalefactor swaps.
- Blanket "go long" (girl and Mohicans prove it).

## 6. Gotchas

- **Apple alignment:** prepend 64 zero samples to the input (Apple delay
  2112 = 2 frames + 64), and pair Apple frame n+1 with FAAC frame n.
  `FAAC_STEP1_OFFSET=1` for Apple. `FAAC_CORE_INJECT_OFFSET=2` for Apple and 1 for
  FAAC's own dump. fdk: delay 2048, no pad.
- `FAAC_CORE_INJECT` needs C records with a ` g=` group token. Build them from
  FAAD_LADDER_DUMP with `probe/ladder/scripts/h/h_conv.py`. Apple codes max_sfb
  46 vs FAAC's 44 on long blocks, so set `FAAC_CORE_INJECT_LOOSE_SFB=1`.
- The probe libfaac on `fdk-ladder` has the smoothing hook OFF by default
  (`FAAC_SF_SMOOTH` unset = 0), so every Stage E–H control still reproduces. Set
  `FAAC_SF_SMOOTH=0.6` to match PR #595.
- The step1 origin file (`FAAC_STEP1_ORIGIN`) marks per band whether the FAAC
  path (1) or the reference path (0) prepares the spectrum. Scalefactor-only
  rewrites of FAAC's own decisions keep origin 1.
- The Stage G/H/R scripts (`probe/ladder/scripts/{g,h,r}`) still hard-code
  `/tmp/ladder_g`, `/tmp/ladder_h`, `/tmp/ladder_s` and macOS paths
  (`/Users/nschimme/...`, `/private/tmp/claude-501/...`). Jules ported the Stage
  E/F ones to `LADDER_WORK` and env vars; do the same for these first. The
  intermediate data (dumps, `*_plus.wav`, `.ci`) was local only. Regenerate it
  with `g2_prepare.py` (the ported version) before running H or R scripts.
- `.gitignore` excludes `*.m4a`; new reference streams need `git add -f`.
- afconvert without `@48000` silently lowers the output rate at ≤96k LC and at HE.
- The faac CLI won't overwrite `-o` unless you pass `--overwrite`. Delete outputs
  first.
- Serial MOS scoring only: parallel ViSQOL/zimtohrli pools have OOM'd hosts.

## 7. Reporting and style

- Append a dated section per stage to `probe/ladder/LADDER_RESULT.md` (commands,
  control results, tables, verdict against the pre-registered rule). Commit
  scripts and small JSON results to `fdk-ladder`, with messages ending
  "(never merge)". No audio in git.
- Encoder changes: branch from `nschimme/faac` `master`, one focused commit, and
  a PR on `nschimme/faac` so CI benchmarks it.
  - Commit subject: `area: imperative summary` (e.g. `quantize: ...`). Then a
    short "why" paragraph and `- ` bullets.
  - No Co-Authored-By or other AI trailers.
  - PR body: an intro paragraph; a bold line saying whether output changes; then
    `## Changes` with the numbers.
- Stop and ask the user when you need: Apple references at new rates or modes;
  anything touching `knik0/faac`; any decision that trades MOS against size or
  speed.
