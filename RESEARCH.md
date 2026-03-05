# FAAC Historical & Competitive Research Summary

## 1. Historical Pain Points (HydrogenAudio, Doom9, SourceForge Audit)
- **High-Frequency "Shimmer":** FAAC often exhibits a metallic "ringing" or "shimmer" at mid-range bitrates (96-128 kbps). This is typically due to spectral holes and sub-optimal quantization rounding that fails to preserve low-level high-frequency components.
- **Pre-echo on Transients:** Without a bit reservoir, FAAC is forced to encode percussive transients (e.g., castanets, drums) within the fixed bit budget of a single frame. This leads to audible pre-echo artifacts as the quantization noise is spread across the entire block.
- **Low Bitrate Speech (16/40 kbps):** At low bitrates, FAAC's quality collapses compared to modern HE-AAC or even tuned LC-AAC encoders. The lack of bit-sharing between frames makes it particularly inefficient for speech, which has high temporal variability.
- **Stereo Instability:** In complex soundstages, FAAC's Mid/Side (M/S) stereo decision logic can be unstable, leading to "swimming" or shifting stereo images.

## 2. Competitive Benchmarking: Architectural "Secrets"
Modern encoders (FDK-AAC, Apple AAC, Fraunhofer) utilize several key strategies:
- **Bit Reservoir:** The most critical missing component in FAAC. It allows the encoder to "save" bits from simple frames (silence or steady-state) and "spend" them on complex frames (transients). ISO/IEC 14496-3 allows a reservoir of up to 6144 bits for a single channel (SCE) or 12288 bits for a pair (CPE).
- **Adaptive Quantization Rounding:** Instead of a fixed magic number (0.4054), modern encoders vary rounding based on frequency and signal characteristics to preserve texture.
- **Improved TNS (Temporal Noise Shaping):** Effective use of TNS is vital for transients, especially in speech, to shape quantization noise under the signal envelope.

## 3. The "Golden Triangle" Analysis
- **Audio Fidelity:** A bit reservoir is expected to provide the highest MOS lift by significantly reducing pre-echo and improving transient clarity.
- **Computational Efficiency:** The overhead of a bit reservoir is minimal—mostly bookkeeping of bit counts and a slightly more iterative quantization loop.
- **Minimal Footprint:** Implementation requires minimal structural changes (adding a few fields to the encoder state and updating the rate control logic).

## 4. Standard Alignment
- **ISO/IEC 14496-3 Section 4.6.2 & 4.6.3:** Bit reservoir control is a standard-compliant method for rate control. It does not change the bitstream format but changes how many bits are allocated per frame.
- **ADTS Header:** `adts_buffer_fullness` should be correctly set to reflect the reservoir state (though many decoders ignore it, 0x7FF signifies VBR/No reservoir).
