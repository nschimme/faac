# Iterative Quality Improvement Strategy for IOT/NVR

The following features have been added to improve perceived audio quality in FAAC, particularly for security and surveillance use cases.

## New Quality Features

1. **Bit Reservoir**: Redistributes bits from simple frames to complex transients.
   - *Tuning*: Adjust `bitres_des` in `libfaac/frame.c` to change how aggressively bits are saved.
2. **Frequency Spreading**: Enhances the psychoacoustic model by accounting for inter-band masking.
   - *Tuning*: Modify the spreading constants (currently `0.3` and `0.2`) in `libfaac/quantize.c`.
3. **TNS for Short Blocks**: Reduces pre-echo artifacts in sharp transients (e.g., door clicks).
   - *Tuning*: Adjust `DEF_TNS_GAIN_THRESH` in `libfaac/coder.h` to change TNS sensitivity.
4. **Enhanced Block Switching**: Improved sensitivity for low sample rates (8-22kHz) common in IOT.

## Iterative Improvement Process

To further improve quality for specific IOT environments:
1. **Capture Representative Samples**: Record audio from actual NVR installations (silence, speech, background noise, sharp transients).
2. **Offline Benchmarking**: Use the `--no-*` flags to isolate the impact of each feature on specific artifacts.
3. **PE Weighting**: The `BitAllocation` function in `libfaac/util.c` can be tuned to better map Perceptual Entropy to target bit counts for your specific bitrate range.
4. **A/B Testing**: Compare the outgoing AAC against FDK-AAC using standard tools (like PEAQ or MUSHRA tests) to identify remaining gap areas.
