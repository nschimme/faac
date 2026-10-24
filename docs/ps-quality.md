# HE-AAC v2 quality and AUTO selection

Encoder PS is experimental and disabled by default in the release build. Enable
it with `-Dencoder-ps=true`. These measurements describe the implementation;
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

## Packed carrier FFT evaluation (2026-10-23)

The carrier packs the two real channels into one complex FFT and synthesizes
only the unique bins. Hops with a mono magnitude below 0.001 of the stereo
energy target use separate channel FFTs and the original full-spectrum
calculation. This avoids amplifying transform rounding near cancellation.
The fallback reconstructs windowed input from existing history and delayed
samples; it needs no persistent backup of the input.

The baseline here is the preceding optimization with separate channel FFTs,
cached overlap normalization, shared stereo-energy measurements, and cheaper
PS analysis and Huffman sizing. These are incremental results against that
baseline, not against an earlier release.

Unmodified faac-benchmark Zimtohrli and coherence scorers compared all 49
48 kHz music clips at 12, 16, and 24 kb/s. Worst predicted-MOS change was
-0.0000062; the largest coherence-error increase was below 0.00000001.
Four representative clips at both 32 and 44.1 kHz had identical scores at
all three bitrates. Eight synthetic stress inputs covered cancellation,
hard panning, quadrature and silence. The unguarded packed implementation's
0.0393 MOS loss on exact-antiphase audio disappeared with the fallback;
the guarded version's worst stress-input change was -0.0000248 MOS.
These are objective metric results, not listening-test claims.

Fallback occurred in 125 of 88,049 music hops (0.142%). Native arm64 Apple
clang 21 release/LTO timing on the concatenated 469.56-second music corpus,
with eight alternating-order trials after warmup, measured full-encoder
wall-time reductions of 5.4%, 5.0%, and 7.4% at 12, 16, and 24 kb/s.
The amd64 GCC 13.2.1 LTO shared-library footprint gate measured
`.text + .rodata + .data` at 102,744 bytes for the previous optimization and
103,528 bytes for the guarded packed FFT: +784 bytes (+0.76%). Per encoder,
`PsCarrier` persistent storage drops by 4,096 bytes because the right-channel
spectrum is removed; the fallback uses a 4,096-byte local spectrum buffer.
The footprint metric counts the shared library sections, not stack or per-handle
runtime memory. Section measurements and binary hashes are in
`/tmp/faac-ps-packing/footprint.json`.

Raw encodes, decoded audio, scorer manifests, static binaries, paired metrics,
and timing trials are under `/tmp/faac-ps-packing`; `summary.json` records
binary and manifest hashes. Native tests passed (66, one optional skip), and
the PS tests passed ASan/UBSan. Carrier-reference tests retain the original
near-cancellation, antiphase and transition cases with energy bounds.

### Subsequent changes preserving encoded output

PS emission now counts bits while writing instead of repeating the entire
sizing pass, combines the fixed header fields into one write, and reads only
the Huffman length byte during sizing. Carrier preparation blends the
time-domain downmix directly into the spectral carrier and skips that work
when its blend weight is zero. Sample mixing uses float. Removing the
`psOriginal` scratch frame saves another 8,192 bytes per encoder.

Against the guarded packed FFT above, all 207 native comparison encodes were
byte-identical: the previous 49 music clips, eight sample-rate boundary clips,
eight stress clips, and four additional mono/near-mono/blend/transition inputs,
each at 12, 16, and 24 kb/s. The carrier-blend regression test also checks exact
sample equality with the preceding buffered calculation across all three blend
regions and history transitions. Native tests passed (66, one optional skip),
and the PS test passed with amd64 GCC 13 LTO.

Native arm64 Apple clang 21 release timing on the same concatenated music
corpus, with 16 alternating-order measured trials per bitrate after warmup,
showed incremental wall-time reductions of 1.0%, 0.9%, and 1.7% at 12, 16, and
24 kb/s. A matched amd64 GCC 13.2.1 release/LTO build with
`default_library=shared`, PS enabled, and frontends/decoder disabled measured
`.text + .rodata + .data` at 103,496 bytes before and 103,448 after: a 48-byte
reduction. These section totals are from this separately configured pair;
the 8,192-byte saving is per-encoder allocation, not shared-library data.
Comparison hashes, binaries, trials and section measurements are in
`/tmp/faac-ps-followup`.
