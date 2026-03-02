# Standard-Aligned Quality Improvement Strategy for IOT/NVR

The following features have been added to improve perceived audio quality in FAAC, following industry standards (ISO/IEC 13818-7 and 14496-3).

## New Quality Features

1. **Bit Reservoir**: Redistributes bits from simple frames to complex transients using a standard-aligned Perceptual Entropy (PE) estimation.
   - *Tuning*: Set level (0-10) to control bit redistribution aggressiveness. 0=Off, 5=Default.
2. **Frequency Spreading**: Enhances the psychoacoustic model with inter-band masking in the energy domain, aligning with standard masking models.
   - *Tuning*: Level-based control (0-10) scales the masking slopes (~17-20dB/bark).
3. **TNS for Short Blocks**: Reduces pre-echo artifacts in sharp transients (e.g., door clicks).
   - *Tuning*: Adjust `DEF_TNS_GAIN_THRESH` in `libfaac/coder.h` to change TNS sensitivity.
4. **Enhanced Block Switching**: Smoothly adjusts sensitivity (1.5-3.0 threshold) based on sample rate. While standard encoders often use Perceptual Entropy (PE) for switching, this energy-change heuristic is a standard alternative optimized for low-CPU environments to prevent pre-echo artifacts.
5. **Adaptive Quantization Bias**: Dynamically adjusts rounding (0.405 -> 0.38) for upper frequencies to reduce ringing, aligning with standard AAC-LC optimizations.
6. **Conservative M/S Decision**: Refined mid/side decision logic to ensure stable stereo imaging and bit efficiency.
7. **ATH Suppression**: Industry-standard Absolute Threshold of Hearing (ATH) model (Terhardt approximation) suppresses inaudible noise to save bits.
8. **Improved PNS**: Perceptual Noise Substitution now uses tonality detection to identify noise-like bands more accurately, improving efficiency at low bitrates.

## Architectural Efficiency: Unlocking Higher Quality

To enable advanced features like LTP or Trellis-based Huffman without exceeding the 20% CPU/Size overhead limit, we must refactor the core pipeline to reduce redundancy.

### 1. MDCT-based Psychoacoustic Model (PAM)
**Priority: High (Enabler)**
- **Research**: Currently, FAAC performs a redundant FFT for the psychoacoustic "brain" (`PsyBufferUpdate`) and an MDCT for coding. By deriving masking thresholds directly from the MDCT coefficients, we can eliminate the FFT entirely.
- **Estimated Impact**: 30-40% CPU reduction.
- **Implementation Strategy**:
  - **Phase 1 (Single Path)**: Send windowed input directly to the MDCT engine.
  - **Phase 2 (Energy Estimation)**: Group the 1024 MDCT coefficients into AAC Scale Factor Bands (SFB) and calculate energy $E_b = \sum X_k^2$.
  - **Phase 3 (Masking Curve)**: Use a simplified triangular spreading function across SFBs in the MDCT domain.
  - **Phase 4 (SMR Calculation)**: Allocate bits based on the Signal-to-Mask Ratio ($SMR_b = E_b / Thr_b$).
- **Constraints**: Trigger window switching on energy transients to handle MDCT time-domain aliasing; avoid `sqrt()` calls by working in the squared energy domain.

### 2. Modular Compilation (Size Optimization)
**Priority: Medium**
- **Research**: FAAC's binary size can be further reduced for embedded IOT environments by making non-essential components optional via Meson.
- **Opportunities**:
  - Add `meson_options.txt` toggles for **PNS**, **TNS**, and **M/S Stereo** logic.
  - While DRM is already optional, further decoupling can reduce the instruction cache footprint, leaving more room for video encoding algorithms.

### 3. Fixed-Point Transition
**Priority: Medium**
- **Research**: Transitioning from floating-point (`faac_real`) to fixed-point is critical for IOT cores like the Ingenic T31X that may have limited or no advanced FPU.
- **Pros/Cons**:
  - **Fixed-Point Transform**: Lower complexity, significant gain in the heaviest part of the pipeline.
  - **Full Fixed-Point Pipeline**: Highest CPU efficiency but requires careful scaling to prevent precision loss and clipping artifacts.
- **Impact**: Highly dependent on hardware; on cores without hardware floating-point support, this is the single largest "unlock" available.

## Iterative Improvement Process

To further improve quality for specific IOT environments:
1. **Capture Representative Samples**: Record audio from actual NVR installations (silence, speech, background noise, sharp transients).
2. **Offline Benchmarking**: Use the `--no-*` flags to isolate the impact of each feature on specific artifacts.
3. **PE Weighting**: The `BitAllocation` function in `libfaac/util.c` can be tuned to better map Perceptual Entropy to target bit counts for your specific bitrate range.
4. **A/B Testing**: Compare the outgoing AAC against FDK-AAC using standard tools (like PEAQ or MUSHRA tests) to identify remaining gap areas.

## Next "Low Hanging Fruit" (Stack Rank)

Refactors that "unlock" quality are now weighted highest:

1. **MDCT-based PAM Implementation**: (CPU Unlock) Reduces transform redundancy.
2. **Modular Meson Options**: (Size Unlock) Enables fitting into tighter IOT flash/RAM constraints.
3. **Fixed-Point Transform Refactor**: (CPU Unlock) Targeted optimization for ARM/Ingenic.
4. **Huffman Codebook Selection Optimization**: Improves coding efficiency by 3-7%.
5. **LTP (Long Term Prediction)**: High quality for tonal signals, enabled by CPU savings from step 1.

## Standards Compliance

These improvements utilize existing tools defined in the AAC standard to maximize quality without introducing compatibility issues with standard decoders.
