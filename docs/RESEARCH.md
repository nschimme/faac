# FAAC Historical & Competitive Research Summary

## 1. User Complaint Analysis (HydrogenAudio, Doom9, FAAC Archives)

### High-Frequency "Shimmer" or Collapse
- **Issue**: Users reporting metallic "ringing" or "swishing" sounds in high-frequency content (cymbals, harpsichord) at mid-range bitrates (96-128 kbps).
- **Cause**: Often linked to poor bit allocation in high-frequency bands or "spectral hole" artifacts where bands are zeroed out too aggressively.
- **Secret**: Adaptive quantization rounding. Reducing the rounding bias (e.g., from 0.4054 to ~0.30) in high-frequency bands can help preserve low-level energy and reduce ringing.

### Pre-echo Artifacts on Percussive Transients
- **Issue**: Pre-echo or "smearing" before sharp attacks (castanets, drums).
- **Cause**: Failure to trigger short window switching (block switching) early enough or lack of frequency-selective transient detection.
- **Secret**: Frequency-selective transient detection. Modern encoders look for energy changes in specific frequency ranges to trigger short blocks more reliably.

### Stereo Image Instability
- **Issue**: Soundstage "wandering" or collapsing in complex stereo mixes.
- **Cause**: Coarse Intensity Stereo (IS) or Mid/Side (M/S) switching decisions.
- **Secret**: Refined psychoacoustic thresholds for joint stereo decisions.

## 2. Modern AAC-LC Encoder "Secrets" (FDK-AAC, Apple AAC)

### Adaptive Quantization Rounding
- Instead of a fixed `0.4054` rounding bias (standard magic number), modern encoders use tonality-aware adjustments.
- For noisy/high-frequency bands, a lower bias (~0.30) reduces metallic artifacts.

### Refined ATH Scaling
- The Absolute Threshold of Hearing (ATH) should be scaled according to the bitrate. At low bitrates (VoIP), more aggressive ATH masking can save bits for more critical bands.

### Scalefactor Capping
- Preventing excessively large scalefactor jumps (clamping to -60..60 delta) is mandatory for standard compliance but must be integrated into the quantization loop to avoid step-size errors.

## 3. Evaluation & Stack Ranking

### Tier 1: High Velocity (Minimal change, maximum lift)
1. **Fix Scalefactor Clamping**: Mandatory for stability and quality. Ensures encoder and decoder are in sync.
2. **Adaptive Quantization Rounding**: Simple logic change in `quantize.c` with high MOS gain for high-frequency content.
3. **Refined ATH Scaling**: Optimizing masking thresholds for different bitrates.

### Tier 2: Standard Alignment
1. **Standard-compliant Scalefactor Encoding**: Full alignment with ISO/IEC 14496-3 Section 4.6.2.
2. **Frequency-selective Transient Detection**: Improving block switching logic in `blockswitch.c`.

### Tier 3: Strategic Shifts
1. **Bit Reservoir Control**: Large architectural change to improve VBR/ABR consistency.
2. **TNS (Temporal Noise Shaping) Expansion**: Improving transient handling via more sophisticated TNS filtering.
