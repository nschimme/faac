# FAAR resampler verification tools

`test_faar_tuneout_matrix.c` adapts selected portable checks from Tuneout
Resampler 0.9.91 C1R3 to FAAR's float, interleaved, create/process/flush API.
It checks 357 ordered rate pairs across the three presets, exact output counts,
chunk-size invariance, multichannel isolation, equal-rate bit identity, and
two independently calculated high-precision impulse coefficients.

The Tuneout-derived test file and rate fixture retain Apache-2.0 terms
and attribution in [LICENSE](LICENSE) and [NOTICE](NOTICE); this tooling is not
linked into the production library.

`test_faar_quality.c` separately checks streaming, reset-independent API
invariants, flush behavior, boundary modes, endpoint impulses, passband gain,
alias energy, and dither sequence stability. The public API has no reset
operation, so reset behavior is not claimed.

The selected staged-short plans for common AAC rates are listed in the
[libfaar documentation](../../docs/libfaar.md#common-conversion-plans). The
rate-matrix invariance checks do not establish measured audio
quality for every listed conversion; quality comparisons must be reported for
the specific rates and presets actually measured.

`tools/compare_faar_sox_vulkan.py` compares the FAAR CLI with SoX's CPU path and
the Vulkan `fast`, `precise`, and `reference` profiles on the same deterministic
float32 input. It reports output counts, sample error, and process wall/CPU
time. Use a native SoX executable when recording CPU throughput; running SoX
through a container wrapper includes container startup in wall time. The
container setup and run instructions are in [docker/sox-vulkan](../../docker/sox-vulkan/README.md).
These sample-error figures are diagnostic and do not replace the ARDFTSRC
quality gate.

The SoX rate source license audit, including the GPL diagnostics and Vulkan
files excluded from FAAR, is documented in [faar-sox-rate-audit.md](../../docs/faar-sox-rate-audit.md).
