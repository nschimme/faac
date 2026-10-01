# Plan: HE core long-block side-info efficiency (handoff for a cloud agent, 2026-10-01)

Self-contained brief. Read it fully, then `NEXT_PLAN.md` §0-S9, §4 (method rules), §5 (dead ends), §6 (gotchas), and
`LADDER_RESULT.md` "Stage S9", "Stage S6-X", "Stage S3-E0" before running anything. Bootstrap per NEXT_PLAN "S4
session facts" (faac-benchmark venv + 49-clip corpus, `faad-ladder-dump` branch build, fdk-aac build for `fdkaac`).

Out of scope: merging anything, upstream PRs, throughput/footprint work, SBR payload changes, LC.

## Why this, and the bar

FAAC's HE-AAC 48 kbps stereo (24 kHz core, 24 kbps/ch) trails fdk-aac by +0.073 raw MOS (+1 % bytes) and Apple by
+0.080 (+5.8 % bytes). S6 put the gap in the core; S9 showed it is not a decision to copy: the references' windows lose
inside FAAC (−0.25 / −0.22) and their M/S is ≈ 0. Dumps from 2026-09-24 point at coding efficiency instead: on long CPE
frames FAAC spends ~550–700 side bits vs fdk ~260–320 (16–20 % of the frame budget). Sources:
- ch1 (the IS channel) flips band by band between spectral, intensity and PNS
  (e.g. `aaII6IIII4I3I44444I22NNIINIINIII`): 145–232 section bits + 160–186 bits of IS-position sf.
- fdk: M/S, ~40 spectral bands per channel, ~5 monotone sections (`8888855555222222111111000`): 42–70 section bits,
  81–106 sf bits.
- ch0: 81–107 section bits, PNS interleaved in the high bands, sf jumps.
That analysis covered 3 clips; the scripts were never committed. Regenerate it on all 49.

**The bar (user, 2026-10-01): MOS per byte of library code.** A change needing more than ~1 KB of code must show
≥ +0.015 MOS in CI; SBR time deltas + coupling (#579) was held back at +2.5 KB for ~+0.01. So measure the ceiling
first, cheaply, and stop early if it can't clear the bar.

## Already dead here (do not repeat)
- IS-contiguity forcing (09-24, knobs PROBE_IS_GAP/FORCE/SIGN/PANQ): FORCE from sfb 20 +0.008 (6/0) and cut ch1 sections
  231 → 157, but ic_hi (stereo coherence error above 1.5 kHz) +4.4 %, worst ×1.47; GAP4 +0.004, ic_hi +8.5 %;
  SIGN hold ic +13 %; PANQ −0.015/−0.092. **Any rule must also report stereo coherence**, not just MOS.
- Optimal sectioning is already shipped (#562): the writer's section DP is optimal for a given class string, so savings
  must come from changing the class/sf decisions, not from re-sectioning.
- Within-chain sf smoothing doesn't compress side info (project "long blocks lose at low rate"); the flip-flop BETWEEN
  chains was never attacked.
- `--no-pns` −1.1, long-only −0.42, `--joint 1` (all M/S) −0.73, the references' windows/M-S copied (S9).
- Real M/S with a quantizer reference (`PROBE_MS_QREF`) won at LC 80k (+0.039) but was a wash at HE 48 (+0.008).

## Base and alignment
- Encoder: this branch's probe libfaac, static build, with `FAAC_SF_SMOOTH=0.6 FAAC_BS_DROPRATIO=12
  FAAC_SBR_FREQ_SCALE=3` (= what ships on FreewareAdvancedAudio/faac master for HE 48k), `-b 48`, HE-AAC v1,
  49 clips, 48 kHz stereo. Slope pair for bits adjustment: the same base at `-b 40` and `-b 56`.
- References: fdk `fdkaac -p 5 -b 48000` (build per S4 facts; `ref/fdk` is LC 128k, not HE), Apple `ref/apple_he48k`.
- HE alignment (48 kHz output): raw ADTS lags FAAC 3042, fdk 5057, Apple 5186; `scripts/s9/inject_ctl.py` has it.
  Only needed if you compare per frame against a reference; the plan below mostly works on FAAC's own streams.

## Tools that exist
- `FAAD_DUMP=<f> FAAD_LADDER_DUMP=1 faad -o x.wav x.aac` (branch `faad-ladder-dump`): per ICS window sequence, groups,
  max_sfb, per band codebook (`band_cb`: 0 zero, 1–11 spectral, 13 PNS, 14/15 intensity), `band_sf` (scalefactor, or
  IS position / PNS energy on those bands), `band_ms`, quantized lines, TNS. `parse_dump.py` parses it.
- `reemit_main.c` + `libfaac/reemit.c`: drives FAAC's real bitstream writer from a parsed dump, one frame at a time,
  no psy/MDCT/rate loop. So a dump you edit (relabel bands, change sf) becomes a valid stream with exact bit counts.
- HE: `scripts/s3/he_splice.py` / `he_control0.py` re-emit the core and splice the original SBR tail bit for bit
  (Control 0 passes at HE 32k and 48k, S3-E0/S6). Use it so edited cores keep FAAC's own SBR.
- `score_clip.py` (faac-benchmark) aligns by cross-correlation; MOS is zimtohrli. Stereo coherence: the faac-benchmark
  phase3 `coherence_error` (ic) and a > 1.5 kHz variant (ic_hi); add a small wrapper if it isn't exposed.

## Phase 0 — accounting (offline, no MOS). Deliverable: a table on 49 clips.
For FAAC, fdk and Apple HE 48k streams, from the FAAD dumps, per long CPE frame and per channel, count bits by
category: section data, scalefactors, IS positions, PNS energies, M/S mask, TNS, spectral, other (global gain, ICS
info). Count them exactly — either instrument the writer used by `reemit` (preferred: one counter per category, probe
only) or a Python counter that follows ISO 14496-3 and the spec Huffman tables.
- **Control A:** re-emitting FAAC's own dump gives frame lengths equal to the original stream's, every frame, 49/49
  (or the spliced HE stream PCM-identical, as Control 0).
- **Control B:** your per-category counts sum to the frame's payload length on every frame.
Report: mean bits per long frame by category for each encoder, the same for short frames, the share of FAAC's
frames that are long, ch1 class-switch count per frame, and the class strings of 3 typical frames per encoder.
Confirm or correct the 09-24 figures. If FAAC's side-info excess on long frames is < 5 % of the frame budget on the
49-clip mean, stop and report: there is no lever worth the bar here.

## Phase 1 — free-bits ceiling (offline, no MOS). Pre-register before computing.
Define at most four relabeling rules that make FAAC's own class string more coherent, applied to FAAC's dump
(edit `band_cb`/`band_sf`/quantized lines), then re-emit to get exact bits. Candidates, in this order:
1. ch1: an isolated spectral run of ≤ N bands (N = 1, 2) inside an intensity run → intensity (IS position from the
   band's L/R energy ratio, from the dump's dequantized lines; lines zeroed).
2. ch1: an isolated intensity run of ≤ N bands inside spectral runs → spectral (requires lines: take them from a FAAC
   encode with IS disabled from that band, or skip this rule if it can't be made exact).
3. ch0/ch1: an isolated PNS band between coded bands → coded at its neighbours' book, or a lone coded band between
   PNS bands → PNS (energy from the band).
4. sf: clamp jumps between adjacent coded bands to ±K (K = 6, 10) where the writer allows.
For each rule: bits saved per long frame (mean, median), share of frames touched, and the free ceiling = MOS the
saved bytes would buy at FAAC's own slope (per clip, from the 40/56 pair), as if the change cost no quality.
- **Control:** the identity rule (no edit) saves 0 bits and is PCM-identical.
- **Pre-registered stop:** if the best rule's free ceiling is < +0.015 MOS (49-clip mean), stop and report HE core
  closed under the bar. If ≥ +0.015, go to Phase 2 with the rules that clear it.

## Phase 2 — real price of each rule (audio, MOS, coherence).
Decode each Phase 1 stream (edited core + FAAC's own SBR spliced) and score 49 clips against the source. Bits-adjust
against the unedited re-emit (K0, which must equal FAAC's stream PCM 49/49) with the 40/56 slope; the saved bits are
not re-spent here, so the bits adjustment credits them. Report adj mean, median, W/L, bytes %, worst 3, ic and ic_hi
vs K0.
- **Pass:** adj ≥ +0.010, W > L, no clip < −0.05, ic_hi not worse than +5 % (mean) and no clip worse than ×1.25.
- Rules that pass are the encoder candidates. If none pass, stop and report which rule lost what.

## Phase 3 — encoder rule (rate loop on).
Implement the best passing rule in the encoder (`stereo.c` IS/M-S decision, or `quantize.c` class/sf decision) as a
probe env knob, unset = byte-identical to the base on 49/49 encodes (PCM) and at LC. Bracket its strength (≥ 3 values
+ neutral), HE 48k and HE 32k (32k must not get worse by > 0.005), plus 16k/24k mono speech spot checks (CI has those
rows). Report the code size with the CI metric (LTO-linked `libfaac.so` .text+.rodata+.data, GCC 13 — non-LTO numbers
mislead). A knob that clears +0.015 at HE 48k, or +0.010 at well under 1 KB, goes into this probe PR as a patch;
do not open an encoder PR yourself — the main session takes it to CI.

## Deliverables and rules
- A dated section "Stage S10-LB" in `LADDER_RESULT.md`: commands, controls, tables, verdicts per phase.
- Scripts under `scripts/s10/`, small JSON results under `results/s10/`, `results/s10/prereg.md` written before each
  phase's numbers. No audio or dumps committed.
- Do not edit NEXT_PLAN.md. Commits end "(never merge)", authored as the repo identity, no AI trailers.
- Push only your own branch; open the probe PR against `claude/trusting-hamilton-uiadf9`.
- Report negative results plainly and stop at the first pre-registered stop. Do not explain a failed control away.
