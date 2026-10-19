# FAAR resampler recovery diagnosis

**Status:** `staged-short` is the default backend. The work is focused on standard audio rates that commonly occur in music, video, broadcast, and consumer devices. The legacy single-stage polyphase path remains a fallback for unplanned rate pairs and is selectable as a comparison build.

## FAAC/qaac-priority follow-up

The broad common-rate matrix below is useful characterization, but it is not yet the priority matrix for FAAC's AAC input path. qaac documents that resampling occurs for an explicit output rate, a source rate unavailable to AAC, or when its encoder downsamples for the selected bitrate. Its default is to keep a supported source rate, and its default resampler is libsoxr ([qaac sample-rate conversion](https://github.com/nu774/qaac/wiki/Sample-rate-conversion), [qaac rate options](https://github.com/nu774/qaac/blob/master/options.cpp)).

Prioritize these routes for production decisions:

- 44.1↔48 kHz for explicit cross-rate delivery.
- 88.2→44.1 kHz and 96→48 kHz for half-rate downsampling.
- 88.2/96→44.1/48 kHz where a delivery rate or bitrate policy requests it.
- 176.4/192 kHz input to AAC-supported rates no higher than 96 kHz, especially 176.4→88.2/44.1 and 192→96/48 kHz.

The current SoX comparison uses SoX's `rate` effect, not libsoxr, so it does not establish parity with qaac's default converter. Before claiming FAAR is the best FAAC choice, derive exact automatic target rates from qaac's advertised format/bitrate combinations and compare directly against libsoxr on those routes. The 16-route matrix below remains evidence for general common-rate conversions, not that qaac-specific comparison.

## Current candidate and common-rate results

The default production backend is `staged-short`; the staged-short planner handles the measured common routes; the legacy single-stage polyphase path remains a fallback for unplanned pairs. The current desktop-double shared library is 104,728 bytes. FAAR uses paired complex transforms from `libfaar/fft_real.c`; the former Ooura implementation has been removed; no SoX rate-conversion code is linked into FAAR.

Ten alternating 30-second stereo trials per engine and conversion were run, with one warmup. Values below are processing CPU medians in seconds, shown as **FAAR / SoX**. SoX used its CPU `rate -v -b 99.5` path.

| Conversion | High FAAR / SoX | Good FAAR / SoX | Fast FAAR / SoX |
|---|---:|---:|---:|
| 32→44.1 kHz | 0.04208 / 0.05530 | 0.03379 / 0.05342 | 0.03286 / 0.05234 |
| 32→48 kHz | 0.04229 / 0.04279 | 0.03355 / 0.04500 | 0.03540 / 0.04535 |
| 44.1→48 kHz | 0.05066 / 0.06212 | 0.04016 / 0.06344 | 0.03919 / 0.06220 |
| 44.1→88.2 kHz | 0.04210 / 0.04371 | 0.03286 / 0.04526 | 0.03198 / 0.04720 |
| 44.1→96 kHz | 0.06966 / 0.08610 | 0.06143 / 0.08868 | 0.06068 / 0.08699 |
| 48→32 kHz | 0.03070 / 0.04532 | 0.02874 / 0.04466 | 0.02989 / 0.04660 |
| 48→44.1 kHz | 0.04981 / 0.06554 | 0.03854 / 0.06266 | 0.03859 / 0.06376 |
| 48→88.2 kHz | 0.06839 / 0.08884 | 0.05742 / 0.09322 | 0.05643 / 0.08798 |
| 48→96 kHz | 0.04673 / 0.04836 | 0.03386 / 0.04723 | 0.03394 / 0.04730 |
| 88.2→44.1 kHz | 0.03641 / 0.03891 | 0.02381 / 0.04017 | 0.02402 / 0.04080 |
| 88.2→96 kHz | 0.10631 / 0.12185 | 0.07655 / 0.12352 | 0.08041 / 0.11979 |
| 96→44.1 kHz | 0.05765 / 0.07561 | 0.04267 / 0.07326 | 0.04758 / 0.07826 |
| 96→48 kHz | 0.03749 / 0.04253 | 0.02694 / 0.04509 | 0.02627 / 0.04340 |
| 96→192 kHz | 0.08809 / 0.09317 | 0.06862 / 0.09474 | 0.06835 / 0.09520 |
| 192→48 kHz | 0.06578 / 0.06855 | 0.03990 / 0.06706 | 0.03891 / 0.06734 |
| 192→96 kHz | 0.07261 / 0.08189 | 0.05140 / 0.08565 | 0.05041 / 0.08301 |

The selected High plan uses 4097 DFT taps for 32→48 kHz; the other High plans are unchanged. The current-hash 10-trial matrix has FAAR below SoX on all 16 routes for all three presets. A 60-round follow-up on the same library measured 0.04296 s FAAR / 0.04538 s SoX for 32→48 and 0.06737 s / 0.06798 s for 192→48. In paired rounds, FAAR's median processing time was 4.93% lower for 32→48 (bootstrap 95% interval: 2.18–7.16% faster) and 1.21% lower for 192→48 (0.04–3.01% faster). The 192→48 result is a close measured win. Results are from an AMD Ryzen 9 7940HS and should not be generalized to other CPUs without measurement. All runs produced deterministic output and exact expected frame counts. See the [60-round paired follow-up](measurements/faar-recovery-2026-10-18/staged-short-multirate-native/high-edge-routes-60trial-summary.json) and its raw run files.

### Response and transient evidence

On these routes, selected passband gain probes at frequencies up to 0.45 × the lower of the input and output rates differ from ARDFTSRC by at most 0.021 dB for High, 0.021 dB for Good, and 0.143 dB for Fast. All are within the 0.25 dB comparison target for these probes. High, Good, and Fast tone, alias, impulse, and count results cover all 16 routes. Eight common upsampling routes were checked for images; the least image attenuation was -108.35 dB for High, -107.13 dB for Good, and -67.25 dB for Fast. The updated 32→48 High plan retains selected passband gain within 0.001 dB of ARDFTSRC through 0.495 × the lower rate; its image attenuation is -108.35 dB. At 0.499 × the lower rate, it differs from ARDFTSRC by 5.68 dB in the transition region and is closer than the previous 5493-tap plan. These probes do not constitute a perceptual quality test or a full transition-band qualification. Deep alias probes above 0.49 × input rate reached at least 155.89 dB for High, 118.02 dB for Good, and 70.69 dB for Fast.

Setup memory varies by preset: High uses about 0.97–7.47 MB, Good 0.29–0.45 MB, and Fast 0.18–0.33 MB of measured live heap. The High 192→48 plan is the largest. The current shared library is 104,728 bytes with 91,587 ELF text bytes. The pinned SoX CPU shared library is 2,302,080 bytes with 643,850 text bytes, but includes SoX's broader library support; this is context, not a kernel-only comparison. A 30-trial experiment with a 131,072-point FFT measured 0.06746 s for FAAR versus 0.06718 s for SoX and increased live heap to 15.0 MB; that plan was rejected, and the selected 192→48 plan stays at 65,536 points.

Native-double staged-short, float staged-short, default staged-short, explicit staged-short, generic staged, and polyphase comparison builds passed all six Meson test groups. The default build selects `staged-short`. The checks cover streaming/API behavior, coefficient and FFT paths, quality probes, and rate-matrix fingerprints. See [`common-rates-current-summary.json`](measurements/faar-recovery-2026-10-18/staged-short-multirate-native/common-rates-current-summary.json) and the raw benchmark and signal artifacts beside it.

## Remaining validation

The staged-short planner is the default and has measured plans for 16 common conversions among 32, 44.1, 48, 88.2, 96, and 192 kHz. The legacy single-stage path remains a compatibility fallback for pairs without a staged plan. Remaining work is to broaden endpoint/transient and perceptual validation, measure the full current-hash response matrix, and assess setup memory on constrained targets. Good/Fast have probe results against the same ARDFTSRC reference, but no separately agreed preset acceptance masks. Embedded arithmetic and on-device timing remain deferred.

## Earlier recovery measurements

The following measurements describe the earlier long-polyphase staged baseline,
not the current short-bank candidate. The severe 20 kHz gain defect in the earlier staged run came from an incorrectly
rate-scaled second low-pass cutoff. The reconstructed staged baseline uses the
original 0.49 cycles per input sample cutoff and passes the existing local
regression groups. No test threshold was relaxed. This restores the baseline;
it does not make that backend competitive.

The measured staged chain is still much slower than the pinned SoX CPU path and
does not preserve the broad passband of the wide SoX configuration. On common
30-second stereo inputs, staged FAAR took 0.205, 0.164, and 0.536 CPU seconds
for 96→44.1, 48→44.1, and 44.1→48 kHz. SoX `rate -v -b 99.5` took 0.087,
0.079, and 0.072 seconds. The staged chain is therefore about 2.4×, 2.1×, and
7.4× slower on those cases. ARDFTSRC took 0.062, 0.045, and 0.044 seconds.
Timing boundaries and inputs are recorded in the measurement JSON files.

The architectural cause is clearer than an individual loop optimization:

- SoX plans a sharp DFT anti-alias stage and then a short fractional-delay
  polyphase stage. At 96→44.1 kHz it uses 601 DFT taps followed by 20 taps per
  phase at 147/320. At 48→44.1 it interpolates by two before the same short
  phase bank. The second bank is not another sharp output-Nyquist low-pass.
- The FAAR staged experiment combines a same-rate DFT prefilter with a
  127-tap-per-phase bank. That bank has roughly 6.35× the taps per output of
  SoX's 20-tap bank, and its phase table is about 152,400 bytes versus 23,520
  bytes. Phase-zero symmetry folding saves only about 0.34% of its work.
- FAAR's current real FFT supports power-of-two lengths only. Its measured
  4096-point real forward/inverse pair takes about 34.8 μs, compared with
  18.4 μs for Ooura's implementation and 11.0 μs for the separately licensed
  FFTW reference. A direct rational-length FFT plan on this backend is not a
  viable next step.
- Upsampling still uses the generic 511-tap path and dominates the staged
  44.1→48 kHz run.

## Historical candidate comparisons

The long-polyphase staged experiment and early short-bank candidates below are retained as historical diagnostics. They explain why the planner moved to staged DFT plus short phase banks; their timings and quality summaries do not describe the current candidate. KBSRC remains an independently measured algorithm comparison. ARDFTSRC is a quality and performance reference, and its GPL FFTW dependency is not part of FAAR. The 16-route snapshot above is the applicable benchmark set.

The SoX CPU code is not imported into FAAR. The inspected pinned fork contains GPL diagnostics hooks in its CPU path; no SoX implementation source is linked into the candidate. See the [SoX license audit](faar-sox-rate-audit.md).

## Verification scope and limits

The recovery tools run quality groups independently so a fingerprint failure
does not suppress the signal-response groups. The checks cover FAAR's native
frame-count, chunk/capacity, drain, endpoint, dither, and channel behavior;
cross-engine comparisons cover only shared numerical probes. SoX, KBSRC, and
ARDFTSRC do not expose FAAR's streaming API contract, so their passing those
numerical probes is not evidence they satisfy that contract. Fingerprints are
regression checks, not quality proof.

The signal set includes wanted-tone gain and residual, phase, alias rejection,
two-tone products, multitone/noise, impulses, and endpoint diagnostics. High
upsampling images have a separate 187-pair sweep. The 20-tap phase-clock
correction restores impulse symmetry on four selected conversions, but this is
not complete transient or perceptual certification over every rate, preset,
and signal. The proposed float32 numerical floor applies only to wanted-tone
residual measurements. It does not relax gain, alias, or image rejection limits.

Memory figures distinguish FAAR arena plus mmap allocation accounting from
whole-process Massif peaks, which include driver input and output. KBSRC's
coefficient setup can spawn workers even when processing is configured for one
thread. Binary-size numbers for whole reference executables include unrelated
code and are not library comparisons.

## Next implementation and promotion gates

On the current 16-route candidate, Good and Fast beat the pinned CPU SoX path on all measured routes. High also measured faster on all 16; 60-round paired follow-ups cover its two closest routes. Selected passband gain probes meet the 0.25 dB ARDFTSRC comparison on High, Good, and Fast. This is a common-route engineering result, not a universal speed claim or perceptual certification. Broader perceptual checks and setup-memory assessment on constrained targets remain open; the desktop-only scope does not include embedded timing.

## Reproduction data

Raw inputs, manifests, and generated binaries remain under `/tmp/faar-recovery`.
The checked-in machine-readable snapshot is in
[`docs/measurements/faar-recovery-2026-10-18`](measurements/faar-recovery-2026-10-18/), including the current native-matched candidate results.
The scripts and exact reproduction commands are documented in
[`tools/faar_recovery/README.md`](../tools/faar_recovery/README.md). The native
HA scorer remains separate and untouched.
