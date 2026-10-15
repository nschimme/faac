# Upstream 098 mode-1 transient-border quality gate

This archived probe is based on upstream commit `098e8900289b87d1592a859351ca0bd420397d4d`. It ports only the adaptive mode-1 transient border selector; the merged four-envelope analysis and noise/limiter behavior remain the control implementation. No `FAAC_SBR_ENVCOUNTSTACK` override is used.

The release/LTO build succeeded for 79/79 targets. With both `FAAC_SBR_*` controls unset, the probe matched the baseline AAC packet payload and FDK-normalized PCM hashes on all 16 clip/rate rows. Both frozen settings (`FAAC_SBR_BORDER_MODE=1` with lambda `0.001` or `0.003`) produced 16 rows each; all 32 passed strict FAAD and FFmpeg validation and were FDK-decoded. This is a four-clip screening gate, not full-corpus evidence, a timing result, or a production validation.

`gate.json` records the paths and hashes for AAC files and decoded PCM artifacts under `/private/tmp/faac-upstream098-adaptive-border/probe-results`, along with source-WAV paths, packet rates, and commands for later source-anchored MOS scoring. `manifest.json` records exact source, patch, build, binary, tool, and clip hashes. The FDK decoder identity is the binary at `/Users/nschimme/gitprojects/faac-benchmark/bin/fdkdec` with SHA-256 `32528021e276e28fd54874dfda806c7eb4a73e67796b4a39c811991dc0f7cbbf`; any later decoder rebuild is a separate validation set.
