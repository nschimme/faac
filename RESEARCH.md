# TNS Research & Competitive Analysis Report

## Historical Context & User Complaints
Based on audits of HydrogenAudio and Doom9 forums, FAAC's TNS implementation has historically been a weak point compared to modern encoders like FDK-AAC and Apple AAC.
- **High-frequency "shimmer"**: Users reported metallic artifacts at mid-range bitrates (64-128kbps). This is often due to poor temporal noise shaping allowing quantization noise to spread into silent regions near transients.
- **Pre-echo on transients**: Since TNS was explicitly disabled for short windows in FAAC, percussive transients (castanets, glockenspiel) often exhibit audible "shmuck" before the hit.
- **Speech clarity**: At low bitrates (16-40kbps), speech signals benefit significantly from TNS to preserve the temporal envelope of voiced/unvoiced transitions.

## Modern Encoder Benchmarking (Secrets & Heuristics)
Modern high-performance encoders (FDK-AAC, Apple AAC) employ several "secrets" for TNS:
1. **Short Window TNS**: Crucial for transient-heavy segments. Modern encoders enable TNS on short blocks but with lower order (e.g., max order 7) to minimize bit overhead.
2. **Adaptive Prediction Gain**: Instead of a fixed threshold, they use a signal-dependent threshold. Speech signals often trigger TNS with lower prediction gains due to their highly structured nature.
3. **Frequency-Aware Band Limits**: TNS is often limited to frequencies where the ear is most sensitive to temporal smearing (> 2kHz), but modern encoders sometimes extend this for speech to cover the 1-2kHz range.

## "Golden Triangle" Filter Applied
- **Audio Fidelity**: Enabling TNS on short windows is expected to provide a significant MOS lift (ViSQOL) for VoIP and Music High datasets by eliminating pre-echo.
- **Computational Efficiency**: Levinson-Durbin recursion for order 7 (short blocks) is computationally inexpensive.
- **Minimal Footprint**: The code changes required to enable and tune TNS are minimal (~50-100 LoC).

## Strategic Roadmap & Updated TODO

### Tier 1: High Velocity (Immediate MOS Gains)
- [ ] Fix bug in `libfaac/tns.c`: `windowSize` is incorrectly set to `BLOCK_LEN_SHORT` for long windows.
- [ ] Enable TNS for `ONLY_SHORT_WINDOW` in `TnsEncode`.
- [ ] Align TNS default behavior in CLI (`frontend/main.c`) with GUI (`frontend/maingui.c`)—enable by default for VBR/ABR.

### Tier 2: Standard Alignment (ISO/IEC 14496-3 Compliance)
- [ ] Implement coefficient compression logic (ISO/IEC 14496-3 Section 4.6.8.3).
- [ ] Fine-tune `tnsMinBandNumber` and `tnsMaxBands` based on updated sample rates and profile indices.

### Tier 3: Strategic Shifts
- [ ] Adaptive TNS activation thresholding based on block energy and prediction gain ratio.
- [ ] Multi-filter support per window for complex spectra.
