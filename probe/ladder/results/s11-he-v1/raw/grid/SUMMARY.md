# HE-AAC v1 SBR grid and resolution probe

This probe starts from `99ce2b0980180194f9ccd1993a786208d277cf3e` and tests three temporal-grid policies and two low-resolution heuristics. The 49-clip full corpus uses 32, 48, 64, and 96 kbps HE-AAC v1. The measured variants are late FIXVAR, center VARVAR, short-envelope low resolution with a 2:1 flatness bound, and any-duration low resolution with the same bound. Every one of the 196 unset candidate rows matched baseline at AAC-packet and strict FDK-decoded PCM level.

| Variant | Mean BD-rate vs baseline | 95% clip-bootstrap CI | Median |
|---|---:|---:|---:|
| Late FIXVAR | +0.588% | [+0.282%, +0.938%] | +0.281% |
| Center VARVAR | -0.084% | [-0.227%, +0.060%] | -0.097% |
| Short-envelope low resolution (2:1) | +0.022% | [-0.071%, +0.140%] | +0.000% |
| Any-duration low resolution (2:1) | -0.036% | [-0.135%, +0.066%] | -0.075% |

Only late FIXVAR is a measured regression: its interval is above zero. The other intervals cross zero, so this corpus does not show a quality gain or loss for those settings. The synthetic transient stream demonstrated the selected FIXVAR and VARVAR border patterns and mixed per-envelope resolution flags; the flat/noise stream selected low resolution while stationary noise remained FIXFIX. FDK, FAAD3, and FFmpeg accepted all four synthetic streams. Full-corpus strict validation also passed for all 1,176 encoded streams in each decoder.

The low-resolution selector applies a spectral-flatness gate and compares exact frequency-delta Huffman costs as a proxy. It does not include all time-delta opportunities in that selection estimate; the measured BD-rate includes actual whole-stream payload size. No timing, production ROM, or peak-memory gate was run, and no setting is promoted.

The full-corpus worst/best per-clip BD-rate values were: late, -1.330% on `cst.wav` / +4.851% on `27-last-song-drums-and-trampets.441.16b48k.wav`; center, -1.413% on `take_your_finger_frin_my_head.16b48k.wav` / +1.412% on `hrp.wav`; short20, -0.689% on `24-Greensleeves-Korean-male-speech.441.16b48k.wav` / +1.849% on `girl.16b48k.wav`; flat20, -0.807% on `27-last-song-drums-and-trampets.441.16b48k.wav` / +1.041% on `Can't Wait Until Tonight (Dry Wurlitzer Mix).16b48k.wav`.

See [full results](full/RESULTS.md), [gate results](gate/RESULTS.md), [direct comparisons](full/direct-comparisons.json), [strict full-corpus decoder results](full/interop-results.json), [run configuration and hashes](full/grid-config.json), [synthetic selection results](synthetic/results.json), [runner](run_grid.py), and [source patch](grid-probe.patch). The gate report title was corrected in the result artifact; the experiment runner itself was not edited.
