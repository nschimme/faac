# RESEARCH: FAAC Modernization - Quality Regression Analysis

## 1. Failure Analysis (Candidate SHA: 00846c38)
The modernization bundle (Phase 1) implemented the Bit Reservoir and 2-pass Rate Control, but the initial benchmark shows a widespread MOS regression (-0.023 Avg) driven by a significant bitrate undershoot (-10.6% Δ).

### Determined Bottlenecks:
1. **Conservative Inter-frame Rate Control**: The quality adjustment logic is too dampened (`0.25` scaling factor), causing the encoder to take too long to recover from a low-quality state when the signal complexity increases.
2. **Unidirectional Intra-frame RC**: The current 2-pass loop only triggers a re-quantization to *reduce* quality when bits exceed the reservoir. It never attempts to *increase* quality within a frame if bits are significantly under-budget, relying solely on inter-frame propagation.
3. **Heuristic Over-sensitivity**: ATH scaling boosts (1.25x-1.5x) in the speech range for <=16kHz modes may be causing bit-starvation in other bands by consuming too many bits for speech-related frequencies, leading to overall MOS loss in complex VoIP/VSS signals.

## 2. Proposed Architectural Refinements
To reach the 3:1 win-to-regression ratio and achieve the MOS target, the following plan will be executed:

### Phase A: Rate Control Robustness
* **Bolder Feedback**: Increase the inter-frame quality adjustment scaling from `0.25` to `0.5` to track signal complexity faster.
* **Intelligent Starting Quality**: If the reservoir is nearly full (>80%), increase the initial quality factor for the next frame to proactively utilize available bits.
* **Pass 2 Upscaling**: Authorization to implement an "Up-pass" in the RC loop: if `actual_bits` < 70% of `desbits` and `pass == 0`, increase quality and re-quantize.

### Phase B: Perceptual Fine-tuning
* **AQR Calibration**: Tighten the AQR PAPR threshold from `2.0` to `2.5` to ensure rounding bias reduction only applies to truly non-tonal regions.
* **ATH Scaling Normalization**: Reduce the 1.25x ATH boost to `1.15x` and evaluate if the speech-band specific boost (300Hz-4kHz) is too aggressive for high-MOS targets.

## 3. Targeted Ratio Goals
* **Target**: 3:1 ratio of Wins (Significant+New) to Regressions.
* **Current Status**: 114 Wins / 270 Regressions (0.42:1).
* **Requirement**: Reduce regressions to < 40 or increase wins to > 800. The primary path is reducing regressions by stabilizing bitrate accuracy to > 95%.
