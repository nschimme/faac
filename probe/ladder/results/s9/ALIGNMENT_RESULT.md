# HE 48k window alignment, three-clip control

`alignment_results.json` supersedes the C4 fields in the earlier `results.json`.

`inject_ctl.py --alignment-only` extracts raw ADTS from each M4A, decodes it with the FAAD ladder build, and cross-correlates low-band PCM against the original source. All three clips measured FAAC 3042, FDK HE 5057, and Apple HE 5186 output samples of lag. The pre-existing `ref/fdk` files are LC 128k (48 kHz), so the control generates FDK HE 48k with `fdkaac -p 5 -b 48000` and checks both reference profiles with `ffprobe`.

For a pad P at the 48 kHz encoder input and frame offset k, the source-aligned frame relation is `3042 + P + 2048*k = reference lag`. Thus FDK takes P=2015, k=0; Apple takes P=96, k=1. FAAD dump records start at 1 while FAAC's injector counter starts at 0, so the corresponding injector offsets are 1 and 2. These are now defaults and are checked against the measured lags on every clip.

Pad 0 and pad 2048 produced different input and encoded-file hashes on all three clips. FAAC's pad-0 windows matched pad-2048 windows exactly with a +1-frame pairing (3/3); fixed pairing matched only 76.8%, 83.0%, and 87.7%. The old C4 maximized over offsets for each pad, hiding that whole-frame shift.

The following entries are uninjected FAAC-versus-reference window match percentages. Pad neighbours hold k fixed; offset neighbours hold P fixed. The last column uses reference windows injected into FAAC at the derived point.

| Clip | Reference | Center | P-1024 | P+1024 | k-1 | k+1 | Injected |
|---|---|---:|---:|---:|---:|---:|---:|
| German speech | FDK | 9.68 | 9.14 | 11.76 | 7.03 | 9.68 | 98.92 |
| German speech | Apple | 11.35 | 11.35 | 11.29 | 10.81 | 9.73 | 100.00 |
| Greensleeves | FDK | 14.01 | 13.59 | 14.01 | 14.08 | 13.59 | 99.03 |
| Greensleeves | Apple | 24.76 | 26.83 | 23.30 | 26.21 | 24.39 | 99.51 |
| Classic | FDK | 82.10 | 83.77 | 82.97 | 82.89 | 82.46 | 99.56 |
| Classic | Apple | 80.26 | 80.62 | 79.82 | 80.70 | 80.18 | 100.00 |

The uninjected peak test fails on all six clip/reference pairs because FAAC uses short windows far more often than either reference; their natural window agreement is a poor alignment discriminator. That diagnostic remains recorded but is no longer a pass criterion. Alignment is established by source-to-decoded-audio correlation, the exact one-frame response to a 2048-sample pad, and the verified reference profiles. Near-perfect injected match confirms the override takes effect at the derived offsets.
