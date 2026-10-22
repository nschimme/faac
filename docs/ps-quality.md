# HE-AAC v2 quality and AUTO selection

The encoder PS option is enabled by default; disable it with
`-Dencoder-ps=false`. These measurements describe the implementation;
they do not establish a patent or licensing conclusion.

## Low-rate PS bit allocation

At explicit total bitrates through 16 kb/s, PS uses one envelope per AAC frame
and ten frequency parameter bands. The analyzer sums adjacent pairs of its
normative twenty hybrid parameter bands before estimating IID, ICC, and phase.
IID retains fine amplitude quantization, with five phase bands. Higher explicit
bitrates and VBR retain two envelopes and twenty bands (eleven phase bands).

The first envelope can use temporal Huffman coding against the preceding
transmitted frame. Two alternating reference slots keep sizing passes and core
rate-control retries from changing that preceding reference. SBR header frames
and band-mode changes use self-contained frequency coding; disabled ICC and
phase parameters reset their cached references to the decoder defaults.

This addresses PS overhead starving the mono AAC core. Decoder dumps of the
previous encoder (`bc603460`) at 48 kHz/16 kb/s showed PS consuming 134 bits/frame
on `fms`, 227 on `velvet`, and 329 on `21-classic` (20%, 33%, and 45% of the
stream). FDK HE v2 used 14, 49, and 44 PS bits/frame respectively. The fix saves
side-information bits rather than raising the total bitrate or removing phase.

## Benchmark measurements (2026-10-22)

The sibling faac-benchmark supplies the unmodified stereo Zimtohrli scorer and
phase-3 coherence error metric. Lower coherence error is better. Each row below
uses the same 49 music clips; 32/44.1 kHz copies use the corresponding resampled
corpora. Profiles are forced, not inferred from AUTO.

These are nominal target rows; use achieved AAC payload bitrate and per-clip
quality curves for efficiency comparisons. Container and ADTS header bytes are
excluded from payload rate. FDK ADTS is muxed with measured gapless metadata
before scoring so encoder priming/padding cannot trigger a duration penalty.

| Input rate / target | HE v1 MOS | HE v2 MOS | HE v1 coherence error | HE v2 coherence error |
|---|---:|---:|---:|---:|
| 32 kHz / 12 kb/s | 2.7114 | 2.7509 | 0.21867 | 0.11795 |
| 44.1 kHz / 12 kb/s | 2.5019 | 2.8227 | 0.33372 | 0.12125 |
| 48 kHz / 12 kb/s | 2.3163 | 2.7529 | 0.37078 | 0.11696 |
| 48 kHz / 16 kb/s | 2.9850 | 3.0920 | 0.18312 | 0.11827 |

At 48 kHz/16 kb/s the prior HE v2 scored 2.6243 MOS and 0.13048 coherence
error, at 16.774 kb/s mean payload. The new encoder scores 3.0920 and 0.11827
at 16.117 kb/s. The original three diagnostic clips now score 3.179 (`21-classic`),
3.092 (`sandman`), and 3.543 (`fms`). The results are per-clip tradeoffs, not a
claim that PS wins on every input.

Efficiency reports use faac-benchmark's `scripts/bd_rate.py::bd_rate_curve`:
per-clip PCHIP interpolation of log payload rate against MOS, integrated only
over shared quality ranges. Dominated rungs are removed by that implementation;
missing, invalid, and non-overlapping curves are counted rather than extrapolated.
Paired/bootstrap reporting resamples clips, not repeated decoder rows.

## Rate efficiency and stereo gates

The 8/12/16 kb/s ladders each contain 49 clips per input sample rate. Negative
BD-rate means fewer AAC payload bits for the same MOS. The values below are
means of per-clip fits, not a fit to cohort-average MOS.

| Input rate | BD-rate vs FAAC HE v1 | Usable clips | BD-rate vs FDK HE v2 | Usable clips |
|---|---:|---:|---:|---:|
| 32 kHz | -7.20% | 48/49 | -15.83% | 47/49 |
| 44.1 kHz | -17.34% | 48/49 | -22.64% | 49/49 |
| 48 kHz | -18.67% | 48/49 | -18.77% | 48/49 |

Every mean BD-rate bootstrap interval is below zero. Exclusions are only invalid
monotone fits or absent shared quality ranges; the machine-readable report
retains the counts. Encoder bitrate floors prevent some nominal low-rate points
from being achieved, so no extrapolation is used to fill those gaps.

Stereo coherence is a separate gate. Around 12 kb/s at equal achieved payload
rate, mean coherence error decreases versus HE v1 by 0.0730 at 32 kHz and
0.1470 at 44.1 kHz. Versus FDK it decreases by 0.0395, 0.0337, and 0.0407 at
32, 44.1, and 48 kHz respectively. All these coherence-delta 95% bootstrap
intervals are below zero.

At 48 kHz/16 kb/s, the equal-payload comparison against FDK gives +0.291 MOS
and -0.0377 coherence error, using all 49 clips. Against HE v1, the corresponding
MOS point estimate is +0.054 (45 overlapping clips); its 95% interval crosses
zero. The 32 kHz/12 kb/s MOS point estimate is also small and inconclusive.
These boundary results support retaining the existing AUTO thresholds while
keeping the improved low-rate rate-quality curves.

[Machine-readable results](ps-quality-results.json) include per-clip BD-rates,
cohort MOS/coherence/payload means, matched-bitrate intervals and coverage,
validation results, and raw-artifact hashes. [Tool instructions](../tools/README.md)
show how to reproduce the comparisons. Raw encodes, WAVs, manifests and full
reports from this evaluation are under `/tmp/faac-hev2-quality-results`.

## AUTO and compatibility

AUTO remains MPEG-4 stereo PS from 8 kb/s through 12 kb/s at 32/44.1 kHz and
16 kb/s at 48 kHz. VBR retains the existing quality-based AUTO policy. Explicit
HE v2 can be evaluated outside those boundaries. Very low targets can hit an
encoder bitrate floor; measured payload rates, rather than requested targets,
are used in rate-quality fits.

The mono carrier preserves energy for hard-panned and antiphase input. Odd
input lengths can round by one sample because the container uses the half-rate
core clock. The PS writer, API, and frontend tests cover both parameter modes,
ICC/phase transitions, temporal deltas, byte offsets, repeated sizing/writing,
and strict M4A/ADTS decoding at all three sample rates.

Native, encoder-PS-disabled, mono-only, reduced-density, and ASan/UBSan builds
are checked. Eighteen forced LC/HE v1 ADTS comparisons against `bc603460` are
byte-identical. Six forced HE v2 VBR comparisons have identical decoded PCM.
The final production payload matches all 441 scored low-rate streams across
three sample rates and all three ladder rungs.

## Reproducing FDK ADTS comparisons

The comparison harness can measure FDK payload bitrate without ADTS headers and
score gapless playback. Build the small delay query helper from this checkout:

```sh
cc $(pkg-config --cflags fdk-aac) tools/ps_fdk_info.c \
  $(pkg-config --libs fdk-aac) -Wl,-rpath,$(pkg-config --variable=libdir fdk-aac) \
  -o /tmp/ps_fdk_info
```

Then pass `--encoder-kind fdk=fdk --fdk-info /tmp/ps_fdk_info --faam PATH` to
`tools/ps_quality_ab.py`. For each FDK configuration, the helper queries
`nDelayCore` and `frameLength`; the harness derives encoder delay and padding
from those values, the ADTS frame count, and source PCM length, then asks FAAM
to mux gapless metadata. It retains the raw `.aac`, writes a gapless `.m4a`,
and records AAC payload and ADTS transport bitrates separately. FDK encoding
uses `aac-enc -r <bps> -t <5|29> -a 1 input.wav output.aac`.
