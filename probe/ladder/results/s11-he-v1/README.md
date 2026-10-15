# Stage S11 HE-v1 study package

This package makes the bounded HE-v1 investigation readable without the originating ignored cache. It contains copied metadata only: reports, result/config/provenance JSON, source patches, small scripts, logs, traces, and text. No audio, encoded streams, decoded PCM, executable binaries, libraries, object files, or build trees are included.

- `manifest.json` maps each source-cache-relative path to its package-relative path and records exact source byte count and SHA-256. Raw copied files are byte-identical to the cache sources.
- `raw/` preserves the original cache-relative hierarchy so report links and family names remain recognizable. Absolute filesystem paths embedded inside some original JSON are preserved as provenance fields; they are not package asset links. Audio/binary assets are deliberately absent.
- `excluded_inventory` in the manifest lists each omitted source-relative file and size. The source tree had 25,384 files / 25,528,130,164 bytes. The package includes 513 metadata files / 78,487,968 bytes and excludes 24,871 audio/build/binary files / 25,449,642,196 bytes. Twelve omitted WAV inputs exceeded 20 MB each; no report/config/result file exceeded 20 MB.
- `LADDER_RESULT.md` → Stage S11 summarizes the full experiment families, controls, results, decoder/scorer provenance, retention, and current status.
- [Errata](ERRATA.md) corrects a few generated narrative fields while keeping the raw subtree immutable; use it with the [production summary](PRODUCTION-RESULTS.md).
- `NEXT_STEPS.md` proposes follow-up experiments toward the probe programme's Apple/FDK goals, with explicit controls and stopping rules.

## Study family map

| Family | Package directory | Coverage |
|---|---|---|
| Baseline features and original stack | `raw/` root, `raw/noise/`, `raw/limiter/`, `raw/stack/`, `raw/gate/`, `raw/full/` | Original noise/limiter feature probes, stacked results, quality summaries, controls, publication snapshots. |
| Adaptive noise and inverse filtering | `raw/adaptive/`, `raw/invf/`, `raw/localnoise/` | Single-band and two-band adaptive variants, full rows, gates, strict decoders, controls. |
| Harmonic detector | `raw/harmonic/` | Detector probes, synthetic traces, full-corpus results, controls and interop. |
| Grid, resolution and frequency tables | `raw/grid/`, `raw/tables/` | Candidate ladders, structural/synthetic checks, full/gate rows and interop. |
| Envelope count and tier17 | `raw/envelope-count/` | 1/2/4 full quality, fixed-count attribution, production validation, footprint/timing, controls. Audio and binaries omitted. |
| Cross-frame policy | `raw/crossframe-border/` | Mode2/future-pcm0 full and gate quality, direct tier17 comparisons, configs and per-clip data. |
| Transient-specific border | `raw/transient-border/` | Historical mode1/fixed split/fixed8; fresh merged-baseline fixed8 and adaptive mode1 full49; cost, smoke, decoder equivalence, CI evidence. |
| Cross-family checks | `raw/unstack-validation/`, `raw/tools/`, `raw/extra-checks*` | Rebase comparisons, reusable validators, extra interop checks. |

The immutable raw JSON may include local paths to omitted media. Use the colocated hashes, actual bitrate/MOS/BD-rate fields, and report text as the portable evidence; input audio is not needed to read the recorded results. All 31 source patches are included. The 68 historical Python scripts are preserved, but 67 contain absolute `/Users`, `/private/tmp`, or `/tmp` paths and are not standalone cloud runners. To rerun a study, retrieve and hash-check its recorded source/tool versions and audio inputs, map those historical paths into isolated local directories, and adapt a copy of the runner. Preserve the archived script and configuration; write rerun results to a new directory. Required audio corpus and reference streams are omitted by design.

## Cloud rerun path mapping

Use these mappings when reconstructing a run in a cloud workspace:

| Historical path | Cloud workspace path |
|---|---|
| `/Users/nschimme/gitprojects/faac/.cache/he-v1-feature-study/<relative-path>` | `probe/ladder/results/s11-he-v1/raw/<relative-path>` for archived metadata; write new outputs under a separate run directory. |
| `/Users/nschimme/gitprojects/faac-benchmark/data/external/audio/<relative-path>` | `<benchmark-checkout>/data/external/audio/<relative-path>` after retrieving the corpus asset and verifying the recorded `input_sha256`. |
| Other `/private/tmp/...` or `/tmp/...` paths | Create equivalent disposable directories under the cloud job's temporary root, then pass them through environment variables or a small wrapper. |

The package does not include the audio corpus, generated streams, decoded PCM, or build products, so a cloud rerun must fetch those inputs and rebuild the recorded source revision. Keep the raw historical runner, configs, and reports unchanged; make path substitutions in a copy or wrapper, verify input/tool/source hashes before execution, and store outputs outside `raw/`. Compare rerun results against the archived numeric rows only when corpus, metric/scorer, encoder, and decoder provenance match.
