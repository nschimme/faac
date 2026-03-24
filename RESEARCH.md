# Rate Control Research Journal

## Initial Observations
- Baseline at 15% coverage shows ~20kbps for 16kbps target (VoIP) and ~34kbps for 40kbps target (VSS).
- Music scenarios (64, 128, 256) are much more accurate.
- The current rate control is a simple one-frame feedback: `quality *= (target_bits / actual_bits_prev_frame)`.
- There is no bit reservoir.
- There is no Perceptual Entropy (PE) estimation.

## Modern Encoder Reference: FDK-AAC / FFmpeg
- **PE Estimation**: Bits required are roughly proportional to Perceptual Entropy. $PE = \sum \text{width} \cdot \log_2(\frac{\text{energy}}{\text{threshold}})$.
- **Bit Reservoir**: Buffer bits from "easy" frames to use in "hard" frames. Standard size: 6144 bits per CPE.
- **Dual Feedback**:
  - Long-term: Adjust baseline quality to match target bitrate over seconds.
  - Short-term: Distribute bits from reservoir based on PE demand.

## Proposed Design for FAAC
1. **Perceptual Entropy (PE) Calculation**:
   Implement a function to estimate PE during or after psychoacoustic analysis.
2. **Bit Reservoir Implementation**:
   Add `bit_reservoir` to `faacEncStruct`.
   `bit_reservoir_max` = 6144 (for stereo) or 3072 (for mono) bits? Or maybe just use a percentage of the bitrate.
3. **ABR Two-Loop Control**:
   - Loop 1 (Baseline Quality): Slow-moving average of bitrate error.
   - Loop 2 (Frame Allocation): Use PE and reservoir fullness to decide `desbits` for the current frame.
4. **CPE Shared Budget**:
   Calculate PE for the pair and allocate bits together.

## Experiments
- Experiment 1: Implement basic bit reservoir and PE estimation.
- Experiment 2: Initial dual-loop feedback (alpha=0.05). Result: VoIP ~11kbps (target 16).
- Experiment 3: Increase alpha to 0.1 and add 1.4x quality boost. Result: VoIP ~13kbps.
- Experiment 4: More aggressive feedback and PE ratio clamp. Result: VoIP ~14kbps.
- Experiment 5: Log-domain proportional adjustment and faster PE moving average. Result: VoIP ~13.5kbps, VSS ~28kbps (target 40).
- Experiment 6: Stabilized log-domain feedback and tuned res_fix. Result: 95%+ bitrate accuracy achieved on most scenarios.
- Experiment 7: Switched to feed-forward PE estimation. Result: Drastic improvement in bitrate accuracy for short clips (VoIP/VSS), achieving >95% accuracy while maintaining MOS.
