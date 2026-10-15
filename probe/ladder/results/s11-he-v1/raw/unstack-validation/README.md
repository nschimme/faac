# Upstream-master rebase stream equivalence

This audit compares a clean build at upstream `01e7aa7f078095a69e8423578f8abab8ae61ec70` and four independently based feature builds (noise, limiter, four-envelope tier, and their combination) against their frozen measured references. It re-encodes all 49 48 kHz stereo corpus clips at 32/48/64/96 kb/s, requires `FAACEncoder(profile="he")` to force `--object-type he-aac-v1`, asserts ffprobe profile `HE-AAC`, strictly decodes each stream with the faac-benchmark FDK decoder, converts decoded PCM to 48 kHz stereo, then compares ordered AAC packet payload hashes and converted PCM SHA-256.

The audit completed 980/980 rows with exact packet and PCM identity: 196 for each of the five builds. It did not calculate MOS. Historical MOS is eligible for reuse only where both exact identities match; reference MOS and source keys are stored per row. Limiter rows use the historical candidate-equivalence ledger, whose 32 kb/s entries intentionally compare to the baseline (`comparison_variant=baseline_default`) while higher rates compare to the limiter candidate.

`results.json` contains source heads and bases, commit patch IDs, source-diff hashes, frontend/library hashes (including linked `libfaac.3.dylib` and `libfaam.3.dylib`), FDK binary hash, input WAV hashes, measured actual payload rates, profile, and per-row reference provenance. Temporary AAC and PCM files were deleted after hashing to keep the workspace bounded. `summary.json` is the count/mismatch summary; `run.log` contains the progress and final exit. The runner is `validate_upstream_rebases.py` and runs from this repository with:

```sh
NUMBA_DISABLE_JIT=1 ../faac-benchmark/.venv/bin/python \
  .cache/he-v1-feature-study/unstack-validation/validate_upstream_rebases.py
```

No quality result is inferred from the clean baseline build alone. A MOS result can be inherited from the frozen historical row only for exact packet and decoded-PCM matches, as recorded in `results.json`.
