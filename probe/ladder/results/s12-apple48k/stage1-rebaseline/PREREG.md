# S12 Stage 1 prereg: re-baseline FAAC master vs Apple and FDK on `ref/apple-48k` (written before scoring)

Why: every gap number in HANDOFF.md (+3.8 to +6.9 % BD-rate vs Apple) came from the deleted <=10 s, 49-clip set. The
leaderboard (faac-benchmark 9f5f102, gist 31b0bfcf) now shows FAAC master at or above Apple's mean MOS at LC 128/160
and within 0.003 at 192/256/320. This stage measures the gap again on the 77 full-length clips Apple was encoded from.
No encoder change. Research only.

## Inputs
- Corpus: faac-benchmark 9f5f102 `setup_audio`, 77 clips, 48 kHz stereo. Stage 0 `hash_table.md` lists clips whose
  sha256 does not match `src_sha256`. Every headline is reported twice: all clips, and matched clips only.
- Encoders: FAAC `master` (hash in stage0/BUILD.md, meson release); Apple = packaged streams (afconvert CBR `-s 0`);
  FDK = fdkaac on libfdk-aac 2.0.0 with the benchmark's flags for each mode.
- Decoders: FAAD2 (benchmark pin), FFmpeg 6.1.1, fdkdec. Scorer: the benchmark's own MOS (Zimtohrli) path, its P1,
  and its sample-rate handling (HE 32k Apple streams declare 32 kHz output; scored as the benchmark scores them).

## A. LC 48 kHz stereo, 128/160/192/256/320
- FAAC in ABR (`-b`), CBR and VBR exactly as the benchmark invokes each; FDK likewise; Apple has one stream set (CBR)
  and is the reference in all three comparisons. VBR rungs: the FAAC/FDK quality settings the benchmark maps to these
  nominal rates; if the benchmark has no mapping, pick settings from bitrate alone on 10 clips (no MOS read) so the
  mean realized rate lands within +-5 % of each nominal rung.
- Rate axis: realized kbps = 8 x sum(packet bytes) / source duration. Never nominal.
- Order of reading (fixed):
  1. Worst clip x rung table, FAAC minus Apple MOS at equal nominal rung, per decoder and mode, with the realized-rate
     difference beside each cell; count cells below -0.10 (the CI gate) and below -0.05.
  2. BD-rate (per-clip PCHIP, benchmark `bd_rate`) FAAC vs Apple and FAAC vs FDK over 128-192, 192-320 and 128-320,
     mean over clips with a clip-bootstrap 95 % CI (10 000 resamples, seed 0); clips without a valid curve counted and
     reported, not imputed.
  3. Mean MOS and mean realized kbps per rung; per-clip win counts per rung.
- Call: "FAAC trails X" in a range only if the BD-rate CI lies wholly above 0 under all three decoders; "FAAC leads"
  only if wholly below 0 under all three; otherwise "not resolved". Same rule for each mode separately.

## B. HE-v1 32/40/48/64 and HE-v2 24/32/48 (48 kHz stereo input)
- FAAC master (`--object-type` as the benchmark), FDK, Apple. Mean MOS and P1 per rung, per decoder, plus the
  bottom-5 clips per rung. BD-rate with CI over the HE-v1 rungs and over the HE-v2 rungs as in A. ABR only unless the
  benchmark scores HE in other modes too (then all modes it scores).
- Gist values to reproduce (HE-v1 P1, FAAD2 path of the benchmark): Apple 32k 2.677, 40k 2.872, 48k 3.368; FAAC
  3.118/3.356/3.720; FDK 2.928/3.406/3.836. A local value is "reproduced" within +-0.05.

## C. Band-split MOS at 160/192/256/320, all clips, FAAD2
- Method of `bands_lc.py`: 8th-order Butterworth zero-phase split of reference and decoded into 0-2, 2-6, 6-12, 12-16,
  16+ kHz; benchmark MOS per band. Per band: mean FAAC-Apple with clip-bootstrap CI.
- The old attribution ("deficit is 6-16 kHz, 0-6 kHz at parity") survives at a rung if the 6-12 and 12-16 kHz bands
  together carry >= 50 % of the summed negative band deltas and their CIs lie below 0, while 0-6 kHz CIs include 0 or
  favour FAAC. It survives overall if it holds at >= 3 of the 4 rungs. Band MOS is a proxy, not proof.

## Not done here
No arms, no code. Results go to `RESULTS.md` with raw per-clip rows (`rows_*.jsonl`). HANDOFF sections 1-2 are
rewritten from these numbers afterwards.
