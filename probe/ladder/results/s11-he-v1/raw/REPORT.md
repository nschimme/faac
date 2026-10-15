# HE-AAC v1 encoder feature study

Encoder base: `upstream/master` at `99ce2b0980180194f9ccd1993a786208d277cf3e`.
Benchmark checkout: `adef778d6f96ea6ce24448fde962f73dc09d012f` (the exact
commit is recorded in each configuration JSON). Decoder inventory used the
original checkout's in-tree FAAD3 at `333fe311`; the encoder base does not
contain that decoder.

The original checkout's tracked files are unchanged. Luna handled mechanical
inventory, probe implementation, runner construction, and verification.

## Initial screening and controls

Eighteen one-factor settings were screened on the benchmark's four music gate
clips at 32/48/64/96 kbps total. Every stream forced HE-AAC v1 and its profile
was independently checked. Strict FDK decoding preceded the benchmark's
Zimtohrli audio-mode MOS scoring. Actual rate uses AAC packet bytes divided by
input duration. BD-rate uses the benchmark's PCHIP helper, with one ladder per
clip; negative BD-rate means fewer bits for equal measured quality.

The promising settings were then tested on all 49 stereo 48 kHz music-corpus
clips, at the same four rates. Unset probes reproduced baseline AAC packets
and decoded PCM for all 196 full-corpus controls. MP4 version metadata was
excluded from packet identity because probe builds embed a dirty version tag.

## Measured results

| Setting | Full-corpus mean BD-rate | 95% clip-bootstrap interval | Mean MOS delta at 32/48/64/96 kbps |
|---|---:|---|---|
| Static noise level 18 | -0.607% | [-1.012%, -0.292%] | +0.00517 / +0.00335 / +0.00366 / +0.00412 |
| Limiter gain 3, minimal header in one-envelope streams | -0.540% | [-0.946%, -0.195%] | 0 / +0.00569 / +0.00433 / +0.00573 |
| Inverse filtering OFF | +0.229% | [-0.331%, +1.063%] | inconclusive aggregate result |
| Inverse filtering LOW | +0.145% | [-0.237%, +0.794%] | inconclusive aggregate result |

These are small metric gains, not a claim of a large perceptual improvement.
The static-noise result is a retuning of the existing constant, **not an
adaptive noise-floor implementation**. It is retained as a control while the
adaptive estimator is evaluated separately.

The unconditional limiter header scored -0.672% mean BD-rate but caused a
notable 32 kbps clip regression. The isolated candidate retains the original
header when `numEnvFixFix == 1`; it matches baseline at 32 kbps and the tested
limiter stream at 48/64/96 kbps. All 196 candidate outputs matched their
expected AAC packets and strict FDK PCM, so their quality records are inherited
from exact tested streams. Noise level 18 likewise matched all 196 tested
noise-probe outputs. Their combined effect was later measured on the full
corpus; see [Stacked static noise and limiter](#stacked-static-noise-and-limiter).

The additional-harmonic concentration heuristic did not demonstrate a gate
gain. It lacks original-versus-patched tonality comparison, so this result does
not reject a proper missing-harmonic detector. Interpolation, smoothing, and
limiter-band experiments are recorded in the gate report; none was retained.
The base already selects stereo coupling and envelope delta coding by bit
cost, so these were not reimplemented.

## Verification and footprint

All 336 gate streams and 1,372 full-corpus streams passed strict FAAD3 and
FFmpeg decoding; FDK acceptance was mandatory before every MOS score. AAC-LC
packet identity passed 8/8 candidate-versus-baseline checks at 128 kbps. All
16 forced-HE CBR smoke streams passed FDK, FAAD3, and FFmpeg (48 decoder
acceptances). These CBR checks establish acceptance, not CBR quality.

Matched GCC 13.3.0 x86_64 release/LTO shared-library measurements:

| Build | `.text + .rodata + .data` | Delta | `.bss` |
|---|---:|---:|---:|
| Baseline | 76,184 B | — | 20,552 B |
| Noise level 18 | 76,184 B | 0 B | 20,552 B |
| Gated limiter gain 3 | 76,328 B | +144 B | 20,552 B |
| Noise level 18 + gated limiter gain 3 | 76,328 B | +144 B | 20,552 B |

The four builds were rebuilt from identical absolute source paths to avoid
assertion-string and path differences affecting LTO layout. Their binary and
section hashes are in [matched GCC 13 results](stack/matched-gcc13/footprint.json).

Native Apple ARM64 serial timing used the benchmark's `measure_peak_ram`
helper, 12 alternating pairs for each of two clips at forced HE 64 kbps for
the isolated candidates:

| Candidate | Median duration delta | Mean duration delta and 95% bootstrap interval |
|---|---:|---|
| Noise level 18 | -2.682% | -1.234% [-4.527%, +2.132%] |
| Gated limiter gain 3 | +0.707% | +1.701% [-0.238%, +3.802%] |

Both intervals cross zero. These short timing probes establish neither a
speed gain nor performance neutrality. No native amd64 timing, CI verdict,
or listening-test result is claimed. The isolated stack adds the same measured
144 B ROM as the limiter alone; BSS is unchanged. The encoder base registers
no Meson tests.

## Reviewable sources and evidence

- Noise control branch: `he-v1-noise`, worktree
  `/private/tmp/faac-he-v1-noise`, one-file committed change.
- Limiter candidate branch: `he-v1-limiter-gain`, worktree
  `/private/tmp/faac-he-v1-candidate`, one-file committed change.
- Diagnostic probes: `/private/tmp/faac-he-v1-probes`.
- Patches: [noise.patch](noise.patch), [limiter.patch](limiter.patch).
- [Gate results](gate/RESULTS.md) and [full screening results](full/RESULTS.md).
- Exact candidate quality: [noise](noise/RESULTS.md), [limiter](limiter/RESULTS.md).
- [Footprint sections and hashes](footprint.json),
  [GCC release build log](gcc13-release.log), and [extra checks](extra-checks.json).
- Configuration, raw rows, provenance, and decoder outcomes are beside each
  report; timing rows are in each candidate's `timing/` directory.
- Reproduction tools are in [tools](tools/); they use the sibling benchmark
  virtualenv and record their exact inputs. Baseline and candidate binary paths
  are explicit in the tools/configuration. No sibling benchmark files were
  edited. Retained winners are now published as draft PRs; diagnostic probes remain uncommitted.

## Adaptive comparison prompted by the static-floor question

The static sweep isolated one cheap parameter change. Its improvement did not
establish that a constant is preferable to a content-dependent estimator.

An additional probe, independently based on the same encoder commit, captures
complex QMF samples, estimates second-order covariance prediction gain,
compares original and patched-source tonality, smooths the requested noise
ratio, and transports per-channel/per-envelope noise levels through the
existing payload delay. It retains the existing single noise band. Coupled
noise uses separate level/balance values and generic time-delta coding.

The public [3GPP SBR encoder specification](https://www.etsi.org/deliver/etsi_TS/126400_126499/126404/19.00.00_60/ts_126404v190000p.pdf)
provides the tonality/noise-ratio basis. No external implementation source was
copied. A synthetic tone/white-noise check exposed and corrected singular
covariance handling before the final measurements. Superseded measurements
are excluded from the conclusions.

Seven static/adaptive settings were screened on the four-clip gate. The best
bounded adaptive setting was then compared with static levels 18 and 24 on
all 49 clips and four rates:

| Setting | Mean BD-rate versus original baseline | 95% clip-bootstrap interval |
|---|---:|---|
| Static 18 | -0.607% | [-1.012%, -0.292%] |
| Static 24 | -0.625% | [-1.033%, -0.304%] |
| Bounded adaptive (levels 18–24) | -0.538% | [-0.912%, -0.244%] |

Direct comparison is necessary to distinguish retuning from adaptation:
adaptive versus static 18 was **+0.067% BD-rate** [95% CI +0.011%, +0.142%];
versus static 24 it was **+0.086%** [+0.025%, +0.162%]. Thus this prototype
showed a small aggregate regression against both static controls. It shared
151/196 exact packet identities and 159/196 PCM identities with static 18.
Separate traces confirmed finite estimates and varying levels, but the gate
clips were overwhelmingly clipped at their configured minimum level 18.

Requested SBR allocation sizes measured with native ARM64 Clang headers grew
by 1,164 B mono, 2,236 B stereo, and 8,668 B for eight channels. This is a
`sizeof`-based allocation model excluding the unchanged resampler; it is not
peak RSS, stack, or production ROM. The adaptive probe is not promoted to a
production candidate, and no adaptive timing result is claimed.

All full-run original/unset packet+PCM controls passed. FDK decoding was
mandatory before scoring. All 392 new static-24/adaptive streams also passed
strict FAAD3 and FFmpeg validation.

This result supports the static setting as a low-cost control. Follow-up probes
for frequency-local adaptive noise and joint inverse filtering are documented
below; they did not establish an additional gain over the matched static
controls.

See [adaptive comparisons](adaptive/SUMMARY.md),
[raw full-corpus results](adaptive/results-all/RESULTS.md),
[allocation model](adaptive/state_sizes.json), and
[probe patch](adaptive/adaptive-probe.patch). The exploratory worktree is
`/private/tmp/faac-he-v1-adaptive`, branch `he-v1-adaptive-noise`; no adaptive
code was copied into either narrow candidate or the original checkout.

## Joint inverse-filtering and noise probe

A separate probe compared raw inverse-filtering tonality with an explicit
`10*log10(T)` mapping, at the default noise floor and static floors 24/18. It
also combined both estimators at noise floors 12 and 18. Raw and dB mappings
are exploratory alternatives, not normative calibration. This retains the
existing single noise band and does not test a frequency-local estimator.

The four-clip gate selected baseline-floor and static-24 inverse-filtering
tests, plus joint tests at floor 12. Full results cover 49 clips at 32/48/64/96
kbps. All six newly scored full-corpus variants passed strict FDK decoding
before MOS scoring and strict FAAD3 plus FFmpeg validation afterward: 1,176
streams passed in each decoder. The unset candidate matched baseline AAC
packet and FDK-decoded PCM identity at all 196 full-corpus clip/rate controls.
The four-clip gate has 16 clip/rate controls.

Inverse filtering alone was inconclusive against both its matched static
reference and baseline. Raw mode at floor 12 measured +0.269% BD-rate versus
baseline [95% clip-bootstrap CI -0.282% to +1.075%]; dB mode measured +0.299%
[-0.255%, +1.057%]. At floor 24, raw mode measured +0.275% versus static 24
[-0.294%, +1.086%], and dB mode +0.313% [-0.258%, +1.101%]. The worst
individual-clip regressions were on `glk.wav`: +12.302%, +12.539%, +13.209%,
and +13.354%, respectively. The other clips vary in direction; aggregate
intervals cross zero.

Joint floor-12 modes improved on their corresponding inverse-filter-only
variants: raw joint vs raw invf was -0.580% BD-rate [-0.973%, -0.250%], and dB
joint vs dB invf was -0.455% [-0.859%, -0.137%]. That incremental result did
not establish an improvement against static noise controls: raw joint measured
+0.317% versus static 18 [-0.246%, +1.116%] and +0.338% versus static 24
[-0.235%, +1.159%]; dB joint measured +0.475% [-0.145%, +1.334%] versus
static 18 and +0.496% [-0.134%, +1.371%] versus static 24. Joint-vs-baseline
intervals also cross zero. The worst clip for the joint raw comparison against
static 24 was `glk.wav` at +14.522%; the best was `bah.wav` at -3.515%.
Thus the joint estimator helps relative to inverse filtering alone in this
measurement, but the evidence does not show a gain over the simpler static
noise controls, and the large clip-specific regressions remain material.

The per-rate MOS deltas show the same distinction. Against raw invf-only, raw
joint gained +0.00521 / +0.00279 / +0.00435 / +0.00347 MOS at 32/48/64/96
kbps, with paired 95% intervals above zero at every rate. Against static 24,
the raw joint deltas were -0.00181 / +0.00028 / -0.00117 / -0.00249, and all
four paired intervals cross zero. dB joint gained +0.00473 / +0.00159 / +0.00248
/ +0.00399 over dB invf-only; the 48 kbps interval crosses zero. Against static
24, dB joint deltas were -0.00167 / -0.00217 / -0.00203 / -0.00231, again with
all paired intervals crossing zero. Mean payload-rate differences remain below
0.024% across these comparisons.

The `sizeof` allocation model estimates +2,308 B of incremental encoder state
for stereo. It excludes the unchanged resampler and is not production ROM,
peak RSS, stack, or a shipping-footprint measurement.

No candidate is promoted from this probe. Timing, production ROM, peak memory,
and stack were not measured. See the [full-corpus summary](invf/full/SUMMARY.md),
[gate report](invf/gate/RESULTS.md),
[full-corpus comparisons](invf/full/RESULTS.md),
[machine-readable pairwise BD-rate and MOS results](invf/full/direct-comparisons.json),
[decoder validation](invf/full/interop-results.json),
[run configuration and hashes](invf/full/invf-config.json), and
[state allocation model](invf/state-sizes.json),
[cumulative probe patch](invf/invf-probe.patch). The full row set retains
per-clip MOS, bitrate, AAC packet hash, decoded PCM hash, and artifact paths.

## Frequency-local noise and INVF probe

A separate probe added two frequency-local noise bands, per-channel/per-band
tonality and INVF choices, adaptive noise levels, and coupled level/balance
coding. At the tested 48 kHz stream rates, the one-band selector produced
`nq=1` with QMF edges `[31,54]`; the two-band selector produced `nq=2` with
edges `[31,37,54]`. This validates the measured boundaries for these settings,
not every legal SBR table configuration.

The full-corpus comparison covered 49 clips at 32/48/64/96 kbps. All four
two-band full-corpus variants regressed against their matched one-band static
controls:

| Candidate vs reference | Mean BD-rate (95% clip-bootstrap CI) | Worst clip |
|---|---:|---|
| Two-band static 18 vs one-band static 18 | +0.401% [+0.172%, +0.622%] | `hrp.wav` (+2.554%) |
| Two-band static 24 vs one-band static 24 | +0.403% [+0.179%, +0.622%] | `hrp.wav` (+2.535%) |
| Two-band adaptive 18 vs one-band static 18 | +0.355% [+0.109%, +0.591%] | `9-Have-big-expensive-car.441.16b48k.wav` (+2.596%) |
| Two-band joint 18 vs one-band static 18 | +0.590% [+0.018%, +1.372%] | `glk.wav` (+13.882%) |

Positive BD-rate is worse. Adaptive 18 also regressed +0.374% [+0.138%,
+0.602%] versus one-band static 24; joint 18 regressed +0.611% [+0.027%,
+1.404%]. The joint mode's worst clip against original baseline was `glk.wav`
at +10.202%. The bounded result rejects these tested frequency-local
configurations; it does not rule out all multi-band estimators.

The full results contain 1,176 variant rows, including 392 reused one-band
static-control rows; the four two-band variants have 784 MOS rows scored after
strict FDK decoding. Strict in-tree FAAD3 and FFmpeg accepted all 1,568 unique
streams, including baseline and candidate controls, with zero failures.
Decoder acceptance does not establish PCM identity. No candidate won its
matched comparison, so ROM, timing, RAM, and stack were not measured. No
candidate is promoted.

See the [localnoise summary](localnoise/SUMMARY.md), [full results](localnoise/full/RESULTS.md),
[pairwise comparisons](localnoise/full/direct-comparisons.json),
[decoder validation](localnoise/full/interop-results.json), and
[probe patch](localnoise/source.patch). The runner and stream encodes are
archived under `localnoise/` and `/private/tmp/faac-he-v1-localnoise/`,
respectively.

## Stacked static noise and limiter

The gated limiter and static noise settings were measured together on the same
49 music clips and four HE-AAC v1 rates. The limiter control enables the
extra-2 header only when a frame uses two FIXFIX envelopes; the unset candidate
matches the original header and baseline output at 32 kbps. Static noise levels
18 and 24 were each measured alone and stacked with the limiter.

| Setting | Mean BD-rate vs original | 95% clip-bootstrap interval | Direct BD-rate vs same noise floor |
|---|---:|---:|---:|
| Limiter only | -0.540% | [-0.946%, -0.195%] | — |
| Static noise 18 | -0.607% | [-1.012%, -0.292%] | — |
| Static noise 24 | -0.625% | [-1.033%, -0.304%] | — |
| Stack at noise 18 | -1.107% | [-1.626%, -0.667%] | -0.531% [-0.948%, -0.175%] |
| Stack at noise 24 | -1.117% | [-1.643%, -0.676%] | -0.526% [-0.946%, -0.172%] |

The incremental stack gains over each corresponding static-floor setting have
intervals below zero in this measured corpus. All 196 unset-candidate controls
matched baseline AAC packet and strict FDK-decoded PCM identity. Strict FDK
decoding preceded all scores; strict FAAD3 and FFmpeg each accepted all 1,060
variant streams across the four-clip gate and full corpus.

The production candidate on PR #64 was re-encoded against the measured
noise-18 stack. Its AAC packet and FDK-decoded PCM hashes matched all 196
clip/rate rows, so the measured MOS records are reusable for that candidate.
The matched GCC 13.3.0 release/LTO builds measured 76,328 B ROM and 20,552 B
BSS for both the limiter and full stack, compared with 76,184 B ROM and
20,552 B BSS for baseline and noise-only builds. This establishes footprint
for those exact builds; no CI or performance verdict is claimed.

See the [stack summary](stack/SUMMARY.md), [full results](stack/results-all/RESULTS.md),
[gate results](stack/results-gate/RESULTS.md),
[decoder validation](stack/interop-results.json),
[production equivalence](stack/production-equivalence/production-equivalence.json),
and [matched footprint](stack/matched-gcc13/footprint.json). The isolated
probe and full raw artifacts are archived in `stack/`.

The production stack also received a matched native ARM64 serial timing run:
20 alternating pairs per clip at 64 kbps, using two existing 10-second WAVs
repeated 12 times by direct PCM concatenation to 120 seconds each. Across 40
paired observations, candidate duration averaged +0.445% (median +0.418%;
95% pair-bootstrap CI [-0.470%, +1.288%]) versus baseline. The per-clip means
were +1.334% for sandman (CI [-0.361%, +2.735%]) and -0.444% for fms (CI
[-1.122%, +0.212%]); both intervals cross zero. This establishes no measured
speed gain. The benchmark helper reported pooled median peak RSS of 2,352 KiB
for both builds; mean paired RSS delta was +0.139% and median delta was 0%.
Peak RSS is separate from duration and comes from `measure_peak_ram`'s child
`ru_maxrss` measurement. Inputs, commands, binaries and dynamic libraries are
hashed in [timing config](stack/timing-serial/timing-config.json), and all 80
encodes reported HE-AAC. These native ARM64 results do not establish amd64
performance.

## Frequency-table policy screen

Five one-factor settings were screened on four music clips at 32/48/64/96 kbps.
Every unset candidate control matched baseline AAC packets and strict FDK PCM
at all 16 clip/rate points. The production default scale (3) reproduced
baseline exactly. Scales 1 and 2 and the 16 kHz stop target regressed on the
gate; the 18 kHz target was inconclusive and also showed no screening gain.
None was promoted or measured on the full corpus. This screen covers only the
bounded settings tested.

See the [frequency-table screen](tables/SUMMARY.md),
[gate results](tables/gate/RESULTS.md), and [probe patch](tables/tables-probe.patch).

## SBR grid and resolution probe

Four full-corpus settings explored a constrained late FIXVAR grid, center
VARVAR grid, and two low-resolution heuristics. The runs used 49 stereo 48 kHz
clips at 32/48/64/96 kbps; all 196 unset-candidate controls matched baseline
AAC packets and strict FDK PCM. All 1,176 encoded streams passed strict FAAD3
and FFmpeg decoding.

| Setting | Mean BD-rate vs baseline | 95% clip-bootstrap interval | Median |
|---|---:|---|---:|
| Late FIXVAR | +0.588% | [+0.282%, +0.938%] | +0.281% |
| Center VARVAR | -0.084% | [-0.227%, +0.060%] | -0.097% |
| Short-envelope low resolution, 2:1 flatness bound | +0.022% | [-0.071%, +0.140%] | +0.000% |
| Any-duration low resolution, 2:1 flatness bound | -0.036% | [-0.135%, +0.066%] | -0.075% |

Late FIXVAR regressed; the other intervals cross zero and show no measured
gain or loss. The low-resolution selector used a frequency-delta Huffman-cost
proxy that omits time-delta opportunities; whole-stream actual bitrate is
included in the measured BD-rate. No timing, ROM, or memory gate was run and
no setting is promoted. Per-clip extremes included +4.851% for late FIXVAR on
`27-last-song-drums-and-trampets.441.16b48k.wav` and +1.849% for short-envelope
low resolution on `girl.16b48k.wav`.

See the [grid summary](grid/SUMMARY.md), [full results](grid/full/RESULTS.md),
[decoder validation](grid/full/interop-results.json), and
[source patch](grid/grid-probe.patch).

## Missing-harmonic detector probe

An exploratory detector compared original and source-mapped QMF tonality,
with a same-bin energy-concentration gate. On the 49-clip corpus, tone30/ratio4
at static floor 18 measured -0.473% BD-rate against static18 (95% clip CI
[-1.500%, +0.329%]); midpoint-only measured -0.578% [-1.644%, +0.243%]. Both
intervals cross zero, so neither demonstrates incremental benefit over the
matched static floor. The max-energy envelope ablation regressed +0.992%
[+0.236%, +2.200%] versus tone30/ratio4 at floor18. The most adverse clips
were `girl.16b48k.wav` (+7.829%) for tone30/ratio4 and `glk.wav` (+22.511%) for
the max-energy ablation.

All 196 unset-candidate controls matched baseline AAC packets and strict FDK
PCM; all 1,764 scored streams passed strict FAAD3 and FFmpeg. These settings
remain diagnostic heuristics, not a calibrated detector. No timing, ROM, or
memory test was run and none is promoted. See the
[harmonic summary](harmonic/SUMMARY.md), [full results](harmonic/full/RESULTS.md),
[matched comparisons](harmonic/full/matched-comparisons.json),
[decoder validation](harmonic/full/interop-results.json), and
[source patch](harmonic/harmonic-probe.patch).

## Adaptive SBR envelope count: tier17 production candidate

The selected production policy keeps one FIXFIX envelope below 17 kb/s per
channel and uses four above that threshold; transient VARFIX framing remains
unchanged. Across 49 stereo 48 kHz clips at 32/48/64/96 kbps, the tier17 hybrid
improved over the matching upstream-base ladder by -1.685% mean BD-rate (95%
clip-bootstrap CI [-2.208%, -1.161%], median -1.458%). Against the matching
retained noise+limiter stack it measured -1.599% ([-2.090%, -1.112%], median
-1.299%). Against frozen upstream overall it measured -2.6185%
([-3.2800%, -1.9957%], median -2.2153%). The overall result is post-hoc and
uses no held-out clips; it is not evidence of a global SBR optimum. The earlier
unconstrained adaptive selector did not resolve a gain over fixed-4.

The final narrow production build matched expected AAC packet payloads and
strict FDK-decoded PCM for 196/196 rows in each validated context. LC identity
passed 16/16; HE boundary, CBR, silence/mono/stereo and partial-flush checks
passed strict FDK/FAAD3/FFmpeg validation; the decimation-4 build and smoke
streams also passed. The GCC13/LTO footprint was +96 B ROM over the retained
stack parent, with no BSS change; persistent SBR state increased by 6176 B for
stereo, and local writer scratch increased by 1024 B. These measurements use
the stated GCC13 amd64 build and do not imply a whole-encoder peak stack bound.

Final native ARM64 timing used 20 AB/BA pairs per each of two 120-second clips
at HE 64 kbps. Mean encode duration was +3.680% (95% paired CI
[+0.984%, +7.390%]), indicating slower performance on this measured workload.
This timing is separate from the earlier noise+limiter stack timing.

The candidate is draft [PR #66](https://github.com/nschimme/faac/pull/66),
head `8c5596ff0581df58fdd9a47aac7d8c39f6939752`, stacked on PR #64 at base
`he-v1-limiter-gain`. Its committed five-file patch SHA is
`d109b48eedec1990d3dd76148be1fa8b224192ccc8fac869c17bd39d35d86f34`, matching
the validated source. Base workflow CI remains unverified; this report makes
no merge claim. Transient-specific border placement and cross-frame lookahead
remain pending experiments.

See [tier17 production validation](envelope-count/tier17/production-final/PRODUCTION-VALIDATION.md),
[final timing](envelope-count/tier17/final-production-timing-v3/TIMING-RESULTS.md),
[matched footprint](envelope-count/production-footprint/gcc13/tier17/CLEAN-FOOTPRINT.md),
[quality comparison data](envelope-count/tier17/hybrid-vs-frozen-baseline.json), and
[draft PR #66](https://github.com/nschimme/faac/pull/66).

## Published winners

The static-noise winner is [PR #63](https://github.com/nschimme/faac/pull/63), commit `60bf043e`. The gated-limiter winner is [PR #64](https://github.com/nschimme/faac/pull/64), commit `5d1bd747`, stacked on PR #63 with live base `he-v1-noise`. The tier17 envelope-count candidate is draft [PR #66](https://github.com/nschimme/faac/pull/66), stacked on PR #64. These remain drafts; no merge or base-workflow CI verdict is claimed.

The experiment completion ledger is [EXPERIMENTS.md](EXPERIMENTS.md).
