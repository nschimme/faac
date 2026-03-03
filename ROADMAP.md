# FAAC Roadmap

## 1. High Priority: Audio Quality & Specialization (IoT/NVR/VoIP)
- **CBR/ABR Bandwidth Capping**: Implement aggressive frequency bandwidth caps (3.5kHz - 5.5kHz) for low-bitrate encoding (<24kbps per channel) to eliminate metallic ringing in security camera and VoIP streams.
- **Adaptive Quantization Rounding**: Implement tonality-aware magic number adjustment. Reduce rounding bias from 0.4054 to ~0.30 for noise-like high-frequency bands to remove "metallic shimmer".
- **Short-block TNS (Temporal Noise Shaping)**: Enable and tune TNS for short blocks to suppress pre-echo artifacts in transient-heavy signals (e.g. glass breaking, gunshots for NVR).
- **Bit Reservoir Control**: Implement functional bit reservoir to allow stable bitrate distribution while providing extra bits for sharp transients in surveillance scenarios.

## 2. Medium Priority: Performance & Optimization
- **Fixed-point Transition**: Research transform-only hybrid fixed-point pipeline for extreme low-power embedded cores (Ingenic T31X/MIPS).
- **Trellis-based Huffman pass**: Implement Trellis-based codebook selection to globally minimize bit cost, providing higher quality at fixed bitrates.

## 3. Library Size Optimization
- **Modular Compilation**: Support disabling unused modules (DRM, TNS, PNS) via Meson to minimize binary footprint for memory-constrained IoT devices.
- **LTO and Symbol Control**: Enable aggressive size-optimized builds for integration into minimal NVR/IoT firmware.

## 4. Low Priority: Compatibility & Maintenance
- **Resampler and Downmixer integration**: Official support for input preprocessing.
- **DRM Testing**: Comprehensive verification of Digital Radio Mondiale mode.
- **HE-AAC Support**: Long-term research into Spectral Band Replication (SBR) integration.
