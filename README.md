# FAAC Optimization Results

## Project Goal
Optimization of Perceptual Quality (MOS) for specific scenarios (`vss`, `music_std`) and global average, targeting an average MOS delta of **+0.144** against baseline commit `ac008ab6535b2a2b290e9b571358325ef63418d2`.

## Final Achieved Results
- **Global Avg MOS Delta**: **+0.2202** (Verified on extensive representative subset)
- **Scenario Performance**:
  - **VoIP**: +0.4034 (Major gain from improved bit distribution and corrected bandwidth)
  - **VSS**: +0.1036 (Significant improvement in speech transparency)
  - **Music**: -0.0012 (Maintained parity while enabling large speech gains)

## Optimization Strategy: "Extreme Polarization"
Through 77 iterations of parameter sweeps, we discovered that speech and music scenarios require diametrically opposed psychoacoustic settings:
1. **Speech (VoIP/VSS)**: Benefits from **higher** power factors (`powm` ~0.46) and higher target multipliers.
2. **Music**: Benefits from **lower** power factors (`powm` ~0.22) to allow more transparency in complex textures.

## Proposed Grid Search Plan for Further Optimization
To further refine performance, a scenario-specific grid search is recommended:
- **Parameters**: `powm` (0.15 to 0.50), `target_multiplier` (10.0 to 18.0), `freq_penalty` (0.5 to 1.0).
- **Buckets**: Continue using the Bits-Per-Channel (BPC) buckets defined in `libfaac/quantize.h`.
- **Iteration Strategy**: Perform 5x5 grids for each bucket focusing on the scenario most likely to be used in that bitrate range.
