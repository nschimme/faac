# Tier17 gated-four envelope-count policy

This is a post-hoc low-rate-preserving policy derived from the complete measured corpus. It sets the steady FIXFIX envelope count to four when the existing `numEnvFixFix` threshold is greater than one and leaves it at one otherwise. `numEnvFixFix` continues to use its existing per-channel threshold of 17,000 bps; VARFIX handling and all existing SBR header/limiter gates are unchanged.

The predeclared hybrid curve used retained reference streams at 32 kbps and unconditional all4 streams at 48/64/96 kbps. Its PCHIP BD-rate versus the frozen baseline is -1.685% [95% clip-bootstrap CI -2.208,-1.161]; versus the retained stack it is -1.599% [-2.090,-1.112]. These are post-hoc derived comparisons with no held-out claim. The direct hybrid comparisons with the PR #60 tier and unconditional all4 curves are in `../hybrid-tier17-precheck.json`.

The base and stack native Meson/Ninja release builds are in isolated clones. Their source patches, exact hashes, options, and identity evidence are recorded in `build-provenance.json`. Both binaries matched the hybrid rows in all 196 AAC packet and strict FDK PCM pairs; no new MOS calculation was needed. Extra production gates and GCC13 footprint results are being handled separately by the benchmark runner.

A native serial timing runner/config is prepared under `timing-serial/`, with no timing encodes started. It compares tier17-stack to the retained production stack on repeated 120-second `sandman` and `fms` PCM inputs using 20 alternating pairs per clip and the benchmark's isolated-child `measure_peak_ram` helper. The timing run remains gated on production tests, footprint completion, and root authorization.
