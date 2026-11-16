# HE-AAC v2 high-rate collapse: diagnosis on master (no fix)

Prereg: `PREREG.md`. Encoder: FAAC master 3a656f7 built with `-Dencoder-ps=true` (the default build has no HE-v2), `--object-type he-aac-v2`.
Raw rows: `rows_screen.jsonl` (ABR, 22 clips, FAAD2), `rows_vbr.jsonl` (VBR, 4 clips, FAAD2/FFmpeg/fdkdec).

## ABR screen, 22 clips (first 20 alphabetical + mof + 18vr_), FAAD2

| nominal kbps | 96 | 128 | 160 | 192 | 256 | 320 |
|---|---|---|---|---|---|---|
| mean MOS | 3.638 | 3.636 | 3.636 | 3.636 | 3.636 | 3.636 |
| mean realized kbps | 95.5 | 107.1 | 107.5 | 107.6 | 107.7 | 107.7 |
| largest per-clip drop from 160k | -0.012 | -0.000 | +0.000 | -0.001 | -0.000 | -0.001 |

## VBR push past the ABR ceiling, 4 clips, three decoders

| clip | -q | kbps | FAAD2 | FFmpeg | fdkdec |
|---|---|---|---|---|---|
| 08 velvet.wav | 400 | 82.5 | 3.075 | 3.286 | 3.285 |
| 08 velvet.wav | 1600 | 114.0 | 3.094 | 3.312 | 3.312 |
| 08 velvet.wav | 3000 | 125.9 | 3.089 | 3.315 | 3.316 |
| 08 velvet.wav | 5000 | 131.6 | 3.087 | 3.313 | 3.312 |
| 13 fatboy_30sec.wav | 400 | 100.2 | 3.486 | 3.506 | 3.520 |
| 13 fatboy_30sec.wav | 1600 | 132.0 | 3.502 | 3.532 | 3.550 |
| 13 fatboy_30sec.wav | 3000 | 139.0 | 3.500 | 3.527 | 3.542 |
| 13 fatboy_30sec.wav | 5000 | 140.8 | 3.499 | 3.528 | 3.543 |
| 18vr_.wav | 400 | 85.4 | 2.468 | 2.576 | 2.586 |
| 18vr_.wav | 1600 | 116.0 | 2.475 | 2.578 | 2.588 |
| 18vr_.wav | 3000 | 130.8 | 2.472 | 2.579 | 2.588 |
| 18vr_.wav | 5000 | 138.8 | 2.472 | 2.579 | 2.587 |
| mof.wav | 400 | 78.7 | 2.533 | 2.580 | 2.582 |
| mof.wav | 1600 | 109.0 | 2.531 | 2.575 | 2.578 |
| mof.wav | 3000 | 123.4 | 2.532 | 2.577 | 2.578 |
| mof.wav | 5000 | 133.0 | 2.529 | 2.580 | 2.581 |

## Findings
- **Not reproduced on master.** Over 96-320 kbps ABR the mean MOS is flat (3.636-3.638) and no clip drops by more than 0.05 from 160k. mof and 18vr_ (the gist outliers, 1.59 and 1.87) score 2.53 and 2.47 at every rung, which is their level at 96k.
- **Master never spends the requested rate.** HE-v2 ABR saturates at about 107 kbps realized for every request from 128k up (bitrate error about 58 % at 256k, 66 % at 320k). In the gist, FAAC a7d2f755 realized the target (0.4 % error up to 256k, 9.6 % at 320k) and collapsed there.
- **More core bits do not break master either.** VBR reaches 109-141 kbps (q1600-q5000) with MOS flat to within 0.03 on all four clips under all three decoders; no decoder reported an error.
- **The gist build cannot be rebuilt.** a7d2f755 is not in nschimme/faac, FreewareAdvancedAudio/faac (both heads are 3a656f7), any advertised ref, or GitHub commit search; it is probably a CI merge commit. The mechanism of its collapse (a code path that only runs when an HE-v2 core is given above about 140 kbps) cannot be tested here.
- Side observation: FAAD2 scores FAAC HE-v2 on velvet about 0.22 lower than FFmpeg and fdkdec (3.09 vs 3.31); the other three clips differ by 0.01-0.12. PS decoding differs between decoders; this matters for any HE-v2 claim.

## Verdict
Under the prereg rule (MOS drop > 0.5 from 160k to 256k under all three decoders, or a decoder error) this is **not an encoder defect on master**. What master does have is an undocumented HE-v2 ABR ceiling near 107 kbps, which the benchmark will report as bitrate error rather than as lost quality.
No fix candidate. Re-open only if the owner supplies the a7d2f755 binary or source, or if a master change lifts the HE-v2 rate ceiling; at that point re-run `rows_screen` at 192-320.
