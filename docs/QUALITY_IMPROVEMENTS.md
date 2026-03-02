# Standard-Aligned Quality Improvement Strategy for IOT/NVR

The following features have been added to improve perceived audio quality in FAAC, following industry standards (ISO/IEC 13818-7 and 14496-3).

## New Quality Features

1. **Bit Reservoir**: Redistributes bits from simple frames to complex transients using a standard-aligned Perceptual Entropy (PE) estimation.
   - *Tuning*: Set level (0-10) to control bit redistribution aggressiveness. 0=Off, 5=Default.
2. **Frequency Spreading**: Enhances the psychoacoustic model with inter-band masking in the energy domain, aligning with standard masking models.
   - *Tuning*: Level-based control (0-10) scales the masking slopes (~17-20dB/bark).
3. **TNS for Short Blocks**: Reduces pre-echo artifacts in sharp transients (e.g., door clicks).
   - *Tuning*: Adjust `DEF_TNS_GAIN_THRESH` in `libfaac/coder.h` to change TNS sensitivity.
4. **Enhanced Block Switching**: Improved sensitivity for low sample rates (8-22kHz) common in IOT.
5. **Adaptive Quantization Bias**: Dynamically adjusts rounding to reduce ringing in upper frequencies.
6. **Conservative M/S Decision**: Better bit allocation in stereo modes by avoiding unstable side-channels.

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
1. **Improved PNS (Perceptual Noise Substitution)**: FAAC's current PNS is very basic. Identifying noise-like bands and using the PNS tool more effectively can save significant bits at low bitrates without perceived loss.
2. **Huffman Codebook Selection Optimization**: Refining how spectral lines are grouped into sections can improve coding efficiency by 3-7%.
3. **LTP (Long Term Prediction)**: While computationally more expensive, LTP can significantly improve quality for tonal signals (like sirens or consistent motor hums) at low bitrates.
