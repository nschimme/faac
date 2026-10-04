# HE-AAC v2 quality and AUTO selection

The encoder PS option is enabled by default; disable it with
`-Dencoder-ps=false`. These measurements describe the
optional implementation; they do not establish a patent or licensing conclusion.

The evaluation used the sibling faac-benchmark Zimtohrli MOS scorer and stereo
coherence scorer on 49 music clips at each sample rate, forced HE v1/v2, and
ABR targets of 8, 12, 16, 20 and 24 kb/s total. FFmpeg and an independent
AAC decoder binary produced 2,940 scored pairs. Bitrate comparisons use
achieved AAC payload bitrate, excluding container overhead.

| Input rate | Mean bitrate-adjusted crossover bracket | AUTO PS ceiling (total) |
|---|---|---|
| 32 kHz | 12–16 kb/s | 12 kb/s |
| 44.1 kHz | 12–16 kb/s | 12 kb/s |
| 48 kHz | 16–20 kb/s | 16 kb/s |

At the selected boundary, mean equal-bitrate PS MOS deltas were +0.058/+0.095,
+0.512/+0.513, and +0.098/+0.145 respectively (FFmpeg/independent decoder).
Interpolation covered 42, 27 and 43 clips respectively. Results below the
measured HE v1 bitrate floor were retained separately and not extrapolated.
Mean coherence error decreased at all three boundaries.

These are cohort heuristics, not universal cutoffs. FFmpeg's 95% bootstrap
interval narrowly includes zero at 32 kHz/12 kb/s and 48 kHz/16 kb/s; some
clips favor HE v1 below the selected ceiling. A four-clip CBR screen supported
the same target-rate crossover direction, but a full CBR corpus was not scored.
AUTO uses PS only for MPEG-4 stereo at these three sample rates, from
8 kb/s through the listed ceiling. VBR retains the existing quality-based
AUTO policy; its quality-value crossover has not been measured.

Synthetic identical, hard-panned, antiphase, independent and quadrature signals
were decoded through FFmpeg, FAAD2, FAAD3 and an independent decoder, in
M4A and ADTS at all three rates. All 168 cases decoded successfully. Dynamic
pan controls also exercised ABR, CBR, VBR and reduced SBR analysis density.
The mono carrier preserves energy for exact antiphase input rather than
canceling it. The analysis uses 10 coarse parameter bands, with QMF statistics
approximating the lowest hybrid bands; PS remains lossy. Quadrature phase
reconstruction differs between decoders. Odd input lengths can round by one
sample because the container uses the half-rate core clock.

Native enabled/disabled, mono-only, reduced-density and ASan/UBSan builds are tested.
The shared decoder-table move preserved decoded PCM in 53 stream comparisons;
72 forced LC/HE v1 elementary-stream comparisons were byte-identical with the
baseline. No subjective listening, CPU or footprint improvement is claimed.
