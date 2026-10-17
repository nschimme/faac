# Handoff: LC at high bitrates vs Apple (2026-10-16)

**Scope: LC only.** HE-AAC is at parity with Apple and FDK on the maintainer's base. nschimme/faac#78 (SBR two-region master table, unit test, lifted stop cap at 44.1-48 kHz) was closed without merging on 2026-10-17; its branch `sbr-two-region-table` still exists. Do not reopen it or open a replacement from this handoff unless the maintainer asks.
This probe branch records research. **Never merge it.** Production changes go to `nschimme/faac` as small separate PRs from the base branch (`he-v1-retune`, which moves; LC output on it is bit-identical to master).
**Read [EXPERIMENTS.md](EXPERIMENTS.md) first: every experiment tried so far, with results and verdicts, so you don't repeat dead ends.**
Then read `results/s11-he-v1/lc-high-bitrate-vs-apple/` (PREREG.md, RESULTS.md).

## 1. Target and current gap
Beat Apple AAC at LC, high bitrates. 49 clips, 48 kHz stereo, ABR, packaged Apple LC references 96/128/160/192 kbps (no 256/320 Apple streams exist).

| FAAC vs Apple, BD-rate (positive = FAAC needs more bits) | FAAD2 | FDK decoder |
|---|---|---|
| 128/160/192 | +6.89 % [+4.15, +9.78] | +6.96 % [+4.24, +9.82] |
| 96/128/160/192 | +3.79 % | +4.01 % |
FAAC is 10.3 % better than FDK at 128-192. Mean MOS at equal nominal rung: FAAC - Apple = -0.016 / -0.014 / -0.014 at 128/160/192; at 192k Apple wins 47 of 49 clips. Worst clips: velvet, take_your_finger_frin_my_head, fms, Girl_In_The_Fire.
Band split at 192k (16 clips, FAAD2, `bands_lc.py`): the deficit is 6-12 kHz (-0.015) and 12-16 kHz (-0.019); 0-6 kHz and 16 kHz+ are at parity. It is a proxy (band-limited MOS), not proof.

## 2. What the first screen found (existing CLI switches, BD-rate vs default, 128/160/192)
| arm | FAAD2 | FFmpeg | FDK decoder |
|---|---|---|---|
| `--joint 1` (M/S only, **no intensity stereo**) | **-1.58 %** [-3.0, -0.5] | -1.58 % | **-1.56 %** [-3.1, -0.4] |
| `--no-pns` | +5.25 % | +5.14 % | not run |
| `--no-pns --joint 1` | +3.78 % | +3.69 % | not run |
| `--no-tns` | -0.05 % | -0.02 % | not run |
| `-c 20000` | -0.61 % (spans 0) | -0.68 % | -0.52 % (spans 0) |
- Intensity stereo costs about 1.6 % at these rates (about a quarter of the gap). PNS is worth about 5 %: it saves more bits than it costs, so a smarter PNS rule may be worth more than turning anything off.
- **Worst-clip check for `--joint 1`** (the project's CI gate fails any clip x rung with MOS past -0.10): 0 of 147 cells below -0.10, but the worst is -0.092 (`24-Greensleeves-Korean-male-speech`, 128 kbps), close to the gate. Mean MOS at equal rate moves only +0.0015, so the BD-rate gain comes from a tiny, broad shift.
- Defaults at 192k: LC + TNS + Mixed joint stereo (dynamic M/S and IS) + PNS, bandwidth 18,750 Hz.
- Already closed in earlier stages (see LADDER_RESULT.md S8-L/S9/S10): scalefactor feature fits, window/M-S decision copying, fitted rules, per-line RD, path-search/DP, long-only windows. Do not repeat without new causal evidence.

## 3. Recommended next steps, in order
1. **Intensity stereo, properly.** (a) Run `--joint 1` vs default at 96 kbps and at 256/320 kbps, FAAC vs FAAC (no Apple reference needed), to find where IS starts and stops costing quality; that decides the gating rate. (b) Read the IS decision in `libfaac/stereo.c` and see why it loses in 6-16 kHz at high rates. The simplest candidate is a bitrate-gated default (no IS above a per-channel rate); a better IS decision is the harder one. (c) Preregister, run on the 49 clips, then check **worst clip x rung per rung against the -0.10 gate before reading any mean**, then confirm on a held-out set. Keep it a small PR to `nschimme/faac`.
2. **Explain the remaining ~5 points** (everything intensity stereo does not): full-corpus band-by-band attribution at 192k and 160k, black-box (decoded PCM and bitstream statistics only), comparing allocation and quantizer noise between FAAC and Apple in 6-16 kHz. Candidate areas, none measured: the PNS decision at high rates (it is worth 5 %, so a better rule may be worth more), scalefactor smoothness across 6-16 kHz, band-limiting (a wider cutoff alone was neutral).
3. **Coordinate before editing the quantizer:** the maintainer said other agents own the loudness exponent, the peak weight (PEAKW), and the scalefactor +/-1 search. Ask before touching those.
4. If the screen results hold, also measure VBR and CBR (the CI runs ABR, CBR and VBR) and a second independent corpus.

## 4. Rules the maintainer set (follow them)
- **No `__attribute__` tricks** for size or speed.
- **No third-party codec code.** FDK, FFmpeg and Apple are black-box binaries or streams for measurement only; never read, copy or paraphrase their source into commits or PR text. Justify changes from the ISO spec, the encoder's own output and measurements; do not cite other decoders as authorities.
- Do not open PRs or push to non-designated branches unless asked. Probe branch: `claude/trusting-hamilton-uiadf9`. Base for experiments: `nschimme/faac` `he-v1-retune`; re-fetch before branching.
- Report decoder-dependence honestly: the FDK decoder was the independent check that shrank every HE gain. LC gains so far hold under all three decoders.
- **Lesson from #78:** a mean BD-rate gain can hide a worst-clip loss that the CI gate rejects. Always list the worst clip x rung cells (and VBR/CBR) before claiming a result.

## 5. Facts that save time
- Decoders: FAAD2 287c00c (pinned by faac-benchmark; its two-region SBR table bug is fixed in nschimme/faad2#2 but irrelevant to LC), FFmpeg 6.1.1, `fdkdec` (faac-benchmark `scripts/fdkdec` on libfdk-aac 2.0.0), in-tree `libfaad` branch d7fe6f7.
- CI gates (faac-benchmark): per-clip MOS (-0.10), BD-rate, bit-exact / footprint (`.text+.rodata+.data` of `libfaac.so`, amd64 gcc 13 LTO, reproducible with `size -A`), throughput (skipped without timing samples), haze/attack gates. Footprint is a warning, MOS regressions fail.
- Speed: use callgrind instruction counts; LC-relevant hot spots: fft 26 %, faacEncEncode 15 %, BlocQuant 13 % (HE profile; LC similar minus SBR).
- Pitfalls hit: copied git worktrees share the original gitdir; ninja can miss edits made within the same second; output dirs are named `out[_TAG][_lc]`; FDK-decoder rescoring returns NaN rows if the encoded path is wrong; the container can restart mid-run, so push often and run long jobs detached (`setsid nohup`).

## 6. Rebuilding the measurement setup (scratch paths are not persistent)
1. Clone `nschimme/faac-benchmark` at `bdce18f7123f51c31795afe9fb4571c18f507a77` (master works once its dangling `output` symlink is gone). venv with `requirements.txt` (zimtohrli, scipy, soundfile).
2. Corpus: GitHub archive downloads return 403 here. Clone `nschimme/PMLT2014`, `nschimme/TCD-VOIP`, `nschimme/SoundExpert` with git; link as `data/temp/PMLT2014-PMLT2014`, `TCD-VOIP-harte2015tcd`, `SoundExpert-SoundExpert`; put empty valid zips at `data/temp/{PMLT2014,TCD-VOIP,SoundExpert,FFmpeg51}.zip`; run `setup_datasets.py`. 40/49 WAVs match the Apple manifest hashes after rewriting the `LIST/ISFT` tag to `Lavf62.12.100` (`results/s11-he-v1/scoreboard-refresh-098e890/fixhdr.py`); the 9 resampled SoundExpert clips (bah bas cst fms glk hrp lob mof qrt) do not match, and the maintainer said to assume they do.
3. Apple references: `ref/apple-s11-367cf8dc/lc/<rate>k/<clip>.m4a`.
4. Build: FAAD2 (cmake), libfdk-aac v2.0.0 (autotools, static) + `nu774/fdkaac`, `scripts/build_fdkdec.sh` (`PKG_CONFIG_PATH` to the fdk prefix, `FAAC_SRC` to a faac checkout with built `libfaam.a`), and the faac tree under test with meson (release).
5. Scripts in `results/s11-he-v1/lc-high-bitrate-vs-apple/`: `ladder.py` (`PROFILE=lc TAG=... RUNGSTR=128,160,192`; arm names such as `lcv_j1` map to flags and hard-code the scratchpad path in `S=`, so edit it), `rescore_fdk.py` / `rescore_fdk_ref.py` (FDK-decoder scoring), `bd_lcv.py` (PCHIP BD-rate with clip bootstrap using faac-benchmark `scripts/bd_rate.py`), `bands_lc.py` (band-split MOS).
6. Held-out: only speech exists (TCD-VoIP, `results/s11-he-v1/crossover-per-rung-098e890/heldout_list.txt`, dual-mono). There is no independent music set; building one is itself a useful task.

## 7. Methodology that worked
Preregister (PREREG.md committed before scoring), screen cheaply, require the BD-rate CI to exclude 0 under FAAD2, FFmpeg and the FDK decoder, check worst clip x rung cells against the -0.10 gate, check bit-exact scope of any code change with payload md5 over a sample-rate x bitrate matrix, and keep production PRs small with a unit test where possible. State every limit in the PR body.

## 8. Index (all under `results/s11-he-v1/`)
`lc-high-bitrate-vs-apple` (this target), `scoreboard-refresh-098e890` (HE and LC vs Apple/FDK on frozen master), `rebaseline-he-v1-retune-e40abd0`, `crossover-per-rung-098e890` (HE crossover research, held-out, FAAD2 root cause), `reference-gaps-367cf8dc` (historical).
