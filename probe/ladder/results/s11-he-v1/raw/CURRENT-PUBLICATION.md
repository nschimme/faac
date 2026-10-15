# Current HE-v1 feature PR publication snapshot

This snapshot records the current branch layout and validation state supplied by the root agent on 2026-10-14. The historical 99ce2b09-based measurement archives remain immutable evidence for the experiments; current PR branches are independently based on `master` so their builds and CI evaluate without stacked PR dependencies.

| PR | Feature | Head | Base/relationship | Current validation status |
|---:|---|---|---|---|
| [#63](https://github.com/nschimme/faac/pull/63) | Local SBR noise-floor handling | `150eefa` | Independent feature branch based on `master` | Root reports 980 exact identity checks passed; current build/CI/benchmark validation was still running at snapshot time. |
| [#64](https://github.com/nschimme/faac/pull/64) | Gated limiter gain | `b27a…` | Independent feature branch based on `master` | Root reports 980 exact identity checks passed; current build/CI/benchmark validation was still running at snapshot time. |
| [#66](https://github.com/nschimme/faac/pull/66) | HE-v1 envelope-count tier policy | `d0a…` | Independent feature branch based on `master` | Root reports 980 exact identity checks passed; current build/CI/benchmark validation was still running at snapshot time. |
| [#68](https://github.com/nschimme/faac/pull/68) | Combined feature candidate | `b29a…` | Independent branch based on `master`; combines only features intended to ship together | Root reports 980 exact identity checks passed; current build/CI/benchmark validation was still running at snapshot time. |

The 99ce2b09 study and its stacked historical sources are retained to preserve the exact provenance of completed MOS, timing, and footprint experiments. Current upstream-base PR quality inherits those measurements only where the requested 980 stream/PCM identities establish byte-for-byte equivalence. CI and benchmark runs reported as active here must be updated after terminal results; this snapshot does not claim they passed.
