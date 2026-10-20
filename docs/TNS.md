# Short-window TNS and transient measurements

This branch adds short-window Temporal Noise Shaping (TNS) for AAC-LC. The long-window path and HE-AAC behavior are preserved. The change is a focused short-filter port; it does not adopt the historical long-filter rewrite.

The selected policy uses at most one filter per short window, order at most 2, 4-bit coefficient resolution, reverse direction, and a minimum prediction gain of 2 both before and after coefficient quantization. The filter starts at the first short scalefactor-band edge at or above 2750 Hz. The start band is derived from the sample-rate table and latched at initialization. This policy was frozen before holdout and full-corpus scoring.

## Measured tradeoff

**This candidate is not an established overall quality win.** MOS and the new pre-echo score are separate measurements; no combined score or audibility threshold was fitted. At equal MOS, the candidate's measured BD-rate costs on the two target clips were:

| Sample rate | eig | fatboy_30sec |
| --- | ---: | ---: |
| 44.1 kHz | +6.304% | +18.078% |
| 48 kHz | +4.878% | +12.600% |

Positive values mean more bits are needed to match baseline MOS. Fits used actual achieved bitrates, monotone PCHIP interpolation, only the shared MOS interval, and no extrapolation. Pre-echo improved at every measured bitrate rung, but that does not cancel the MOS cost.

On the 77-clip 48 kHz, 128 kb/s corpus, eligible pre-echo events were found in **36 clips** (965 matched events); **41 clips were N/A** because they had no eligible reference events. Of the 36 covered clips, **28 improved and 8 worsened**. Mean positive pre-echo excess across covered clips changed from 5.7813 to 3.9698 dB (paired per-clip mean delta −1.8116 dB). This is descriptive coverage, not a claim that all clips benefit. Mean MOS delta across all 77 clips was −0.002587, with the largest loss −0.097342 on fatboy_30sec.

The independent eight-clip holdout had eligible events in only **3 clips**: castanets (23 events), Bachpsichord (1), and glk (4). All three had lower measured pre-echo excess with the candidate. The other five were N/A, not wins. The candidate was frozen before this holdout; no tuning used these results.

For the original reported eig/fatboy material, the candidate lowered the benchmark's measured pre-echo excess relative to baseline at both 44.1 and 48 kHz. These measurements do not independently reproduce or validate the reported ABX outcomes. On the same four target cases, candidate pre-echo remained higher than Apple and FDK references; cross-encoder bitrate was not equal in every case. Attack-centroid and stereo-coherence metrics are reported separately and did not show a consistent transient-timing improvement.

## Validation and costs

- The candidate and its cleaned source form produced identical encoded output on the selected cases. No-TNS, long-only, and HE control output matched baseline in the tested controls.
- FFmpeg, FAAD3, and FDK decoded the tested LC streams. PNS-off FFmpeg/FAAD3 comparisons exceeded 134 dB SNR. Twelve canonical sample rates from 8 to 96 kHz were checked for output identity and decoder rate/channel/frame behavior; this is conformance evidence, not quality validation at each rate.
- Matched amd64 GCC 13.5 release/LTO measurement increased `.text + .rodata + .data` from 81,024 to 83,864 bytes: **+2,840 bytes (+3.505%)**.
- Native ARM64 paired timing measured a **+1.58% median candidate/baseline ratio** on the percussive LC throughput input. This is one machine's indicative result, not an amd64 performance gate; other inputs varied.

The transient scorer used for these measurements is documented in [faac-benchmark PR #113](https://github.com/nschimme/faac-benchmark/pull/113). It measures reference-selected pre-event excess independently from MOS, stereo coherence, and attack timing. The full raw rows, hashes, commands, and per-clip summaries are in the investigation artifacts under `.cache/abx-transients/tuning-evidence/`; those raw artifacts are not included in this repository. In particular, see `bitrate-validation/SUMMARY.md`, `full48k128/SUMMARY.md`, and `holdout-combined/SUMMARY.md` there.
