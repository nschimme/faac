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

## Iterative Improvement Process

To further improve quality for specific IOT environments:
1. **Capture Representative Samples**: Record audio from actual NVR installations (silence, speech, background noise, sharp transients).
2. **Offline Benchmarking**: Use the `--no-*` flags to isolate the impact of each feature on specific artifacts.
3. **PE Weighting**: The `BitAllocation` function in `libfaac/util.c` can be tuned to better map Perceptual Entropy to target bit counts for your specific bitrate range.
4. **A/B Testing**: Compare the outgoing AAC against FDK-AAC using standard tools (like PEAQ or MUSHRA tests) to identify remaining gap areas.

## Standards Compliance

These improvements utilize existing tools defined in the AAC standard to maximize quality without introducing compatibility issues with standard decoders.

## Next "Low Hanging Fruit"

Given the current IOT/NVR constraints, the following items are recommended for future work:
1. **Huffman Sectioning (Band Merging)**: Further grouping adjacent bands into larger Huffman sections can save header bits, improving efficiency by another 2-4%.
2. **LTP (Long Term Prediction)**: While computationally more expensive, LTP can significantly improve quality for tonal signals (like sirens or consistent motor hums) at low bitrates.
