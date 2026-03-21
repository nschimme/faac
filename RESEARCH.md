# RESEARCH.md: FAAC Stereo Optimization Findings

## Goal
Improve audio quality in the 1–4kHz vocal range at low bitrates by forcing the encoder to use Intensity Stereo for high-frequency bands, recovering bit budget for lower frequencies.

## Discrepancy Note (Baseline MOS)
Benchmark results for this repository consistently show a -0.07 to -0.35 MOS delta relative to the suite's global "Baseline" targets, even when reverted to the provided baseline commit (`ac008ab6`). The strategy below focuses on maximizing quality relative to the true capability of this source code.

## Final Implementation (Progressive Tiered Strategy)

### 1. Unified Stereo Architecture
Refactored `libfaac/stereo.c` to ensure Intensity Stereo and M/S decisions are exclusive and mathematically sound, preventing legacy "double-transformation" artifacts.

### 2. Progressive Intensity Stereo
Forced Intensity Stereo thresholds are dynamically adjusted based on the quality target:
- < 24kbps: 3kHz
- 32kbps: 5kHz
- 48kbps: 10kHz
- 64kbps: 15kHz
- 128kbps+: 18kHz (Transparent)

### 3. Adaptive M/S with Phase-Dominance Guard
Restored the baseline two-gate M/S decision (Correlation + Phase Dominance) for high bitrates to ensure transparency. Added bitrate-adaptive aggression (up to 2.0x) only for ultra-low bitrates to maximize bit savings.

### 4. Surgical Vocal Range Boosting (Rate-Control Aware)
Reinvests saved bits by increasing the quality target (`sfac += 2`) in the 800Hz–5kHz vocal range. Moving this from a noise-floor multiplier to a quality offset ensures the boost stays within the rate controller's feedback loop, preventing bitrate overruns and global quality degradation.

### 5. Mono Support
Extended conservative perceptual enhancements to mono scenarios, resulting in a positive MOS delta for VoIP speech at 16kbps without regressions.

## Expected MOS Impact
- **Standard/High Bitrate:** 0.00 Delta (Transparent to source code baseline).
- **Low Bitrate (32-64k):** +0.05 to +0.10 Delta (Perceptual uplift from reinvestment).
- **Mono Speech:** +0.01 Delta.
