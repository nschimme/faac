# Missing-harmonic detector probe

This diagnostic starts from `99ce2b0980180194f9ccd1993a786208d277cf3e` and compares a source-tonality detector with static noise-floor controls. It scores 49 stereo 48 kHz clips at 32/48/64/96 kbps using strict FDK decode before Zimtohrli MOS, actual AAC packet bitrate, and per-clip PCHIP BD-rate. All 1,764 streams passed strict in-tree FAAD3 and FFmpeg validation. The 196 unset-candidate controls matched baseline AAC packets and FDK-decoded PCM.

| Variant and reference | Mean BD-rate | Median | 95% clip-bootstrap CI | Packet / PCM ties |
|---|---:|---:|---|---:|
| Tone 30, ratio 4 vs baseline | -0.555% | 0.000% | [-1.610%, +0.236%] | 21 / 21 |
| Midpoint-only vs baseline | -0.643% | 0.000% | [-1.713%, +0.167%] | 22 / 22 |
| Tone 30, ratio 4, static floor 18 vs static18 | -0.473% | 0.000% | [-1.500%, +0.329%] | 21 / 21 |
| Midpoint-only, static floor 18 vs static18 | -0.578% | 0.000% | [-1.644%, +0.243%] | 22 / 22 |
| Max-energy, static floor 18 vs tone30/ratio4 floor18 | +0.992% | +0.122% | [+0.236%, +2.200%] | 21 / 21 |

The two detector variants do not demonstrate a gain against matched static18: both confidence intervals cross zero. The max-energy envelope ablation regresses against the corresponding detector. The most adverse per-clip BD-rate was +7.829% (`girl.16b48k.wav`) for tone30/ratio4 at floor18; the max-energy ablation's worst was +22.511% (`glk.wav`). The lowest paired MOS delta was -0.0931 (`9-Have-big-expensive-car.441.16b48k.wav`, 32 kbps) for max-energy versus tone30/ratio4 at floor18.

The detector chooses a strongest-tonality QMF bin, compares original and source-mapped tonality, and requires same-bin energy concentration. The midpoint-only variant restricts that bin to within one QMF bin of the decoder's insertion midpoint. These are exploratory heuristics, not a standards-calibrated missing-harmonic detector. No timing, ROM, or memory test was run, and no candidate is promoted.

Provenance: source diff SHA-256 `0b376cebccbdd63294f08eafe0f79bd8b9727d7c461391ce09bfd8b622509ad8`; release frontend SHA-256 `c0dc1e7420c70657307af24d866786c3d15e37fee919834d43cd37157d7529b9`. Full raw rows, configuration, matched comparisons, decoder results, gate data, source patch, and reproduction runners are archived in this directory.
