# S12 protocol addenda (apply to every item's prereg)

## 2026-11-16, acceptance rule for PR candidates (relayed from the owner via the parent session)
- Deliverable: one small PR-ready unit per gate-clearing change, `<item>/pr/<name>.patch` + `PR_BODY.md`; the parent opens PRs.
- A change found on the 48 kHz subset becomes a PR candidate only after validation on the whole faac-benchmark dataset
  (all scenario families in its config: 16/24 kHz mono speech incl. TCD-VOIP, 32/44.1/48 kHz stereo; LC/HE-v1/HE-v2
  rungs as configured), with the benchmark's own runner/compare scripts (`compare_results.py`) and metrics: P1 MOS,
  overall MOS, stereo fidelity, transient metrics, bitrate error, and its gates (per-clip MOS -0.10, BD-rate, footprint,
  throughput). The three-decoder check stays for the targeted scenarios.
- A target-cell win that fails any gate or loses P1 / overall MOS on the full dataset is a negative.
- Rate- or sample-rate-scoped changes prove bit-exactness outside scope instead of re-scoring untouched scenarios.
- Constants are picked on the full dataset, never on the clips that exposed a gap; each PR_BODY states which data
  picked each constant, and leads with the full-dataset table, then the Apple/FDK comparison.
- Stage 1 also compares master against the gist's FAAC column (a7d2f755) at several scenarios (MOS, bitrate error).
