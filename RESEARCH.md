# FAAC Modernization Research - Comprehensive Report

## 1. Quality Analysis (Current Iteration)
- **Baseline SHA**: `4074e67196684113f72cb6d292ff6a05b33ef6f8`
- **Initial Candidate Avg MOS Delta**: -0.023
- **Current Candidate Avg MOS Delta**: -0.0007 (approx parity)
- **Target Expected MOS improvement**: +0.050

The modernization efforts have successfully recovered the massive quality loss seen in the initial candidate (-0.023) by fixing the broken rate control and enabling TNS. However, we have only achieved parity with the baseline, not the expected improvement.

## 2. Bitrate & Performance
- **Bitrate Accuracy**: -4.8% to -7.3% (Improved from -10.6% and -15.1%).
- **Throughput Hit**: -7.5% (Within 10% limit).
- **Library Size**: +5.71% (Within 10% limit).

The primary bottleneck for quality is still the conservative bitrate utilization. The encoder is leaving quality on the table by under-spending the bit budget.

## 3. Stack-Ranked Next Steps (to reach +0.05 MOS)
1.  **Priority #1: Eliminate Bitrate Undershoot**: Adjust the iterative loop to hit within ±2% of target. Every 1% of bitrate recovered roughly translates to 0.005-0.01 MOS improvement at these ranges.
2.  **Priority #2: Correct AQR Interaction with Energy**: The environmental noise regressions (`Shinsho_pool`) suggest that AQR bias reduction might be too aggressive for broadband noise that has high energy but low tonality.
3.  **Priority #3: TNS Order Scaling**: Currently TNS uses a fixed max order. Scaling this based on available bits could improve efficiency for short blocks.

## 4. Features for Further MOS Improvement
- **Psychoacoustic Spreading Function**: Implementing a simple convolution-based spreading function to better model simultaneous masking across bands.
- **Enhanced M/S Decision Logic**: Current Mid/Side stereo decisions are purely energy-based. Using tonality to favor M/S for tone-heavy signals could save bits for better quantization.
- **PNS Threshold Calibration**: Fine-tune the Perceptual Noise Substitution thresholds to more accurately identify noise bands, preventing "metallic" artifacts in high-frequency regions.
