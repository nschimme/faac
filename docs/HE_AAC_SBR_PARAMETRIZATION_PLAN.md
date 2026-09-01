# HE-AAC v1 SBR Parameterization Optimization Plan

## Objective & Goal
Close the large ~0.4–0.5 MOS gap in HE-AAC v1 scenarios across 32k, 40k, 48k, 56k, and 64k stereo. FAAC currently scores 3.51–4.01 MOS in HE-v1 stereo scenarios compared to 3.71–4.49 for Apple AAC and 3.77–4.47 for FDK-AAC.

## Problem Statement
FAAC's current SBR implementation in `libfaac/sbr.c` uses hardcoded parameterization defaults:
1. **Hardcoded Inverse Filtering**: `fd->ch[ch].invfMode = 3` (`INVF_MAX`) is set unconditionally for all channels and bands in `sbr_quantize_envelopes()`.
2. **Static Noise Floors**: `noiseData` is assigned a static default level (`SBR_NOISE_LEVEL_DEFAULT = 0`).
3. **Shared Envelope Grid Borders**: Time envelope borders (`tEnv[]`) and grid configurations are shared across L and R channels in `sbr_adopt_envelope_grid()`, causing temporal smearing on asymmetric L/R transients.

## Proposed Architectural Levers (in `libfaac/sbr.c` & `libfaac/sbr_internal.h`)
1. **Dynamic SFM-Based Inverse Filtering Mode Selection**:
   - Compute Spectral Flatness Measure (SFM) over QMF sub-band energy matrices (`sbr_envR`).
   - Assign `INVF_STRONG` / `INVF_MAX` on tonal/harmonic QMF bands and `INVF_OFF` / `INVF_LOW` on unvoiced/noise-like QMF bands.
2. **Tonal-to-Noise Ratio SBR Noise Floor Estimation**:
   - Derive dynamic `noiseData[ne][nb]` values based on sub-band spectral variance rather than zero defaults.
3. **Independent Per-Channel Envelope Grid Borders**:
   - Move `tEnv[]`, `numEnvelopes`, and `sbr_grid` into `SBRChannel` in `libfaac/sbr_internal.h`.
   - Update `SbrGrid()` to process L and R channels independently when transient correlation is low.
4. **Energy-Preserving HF SBR Envelope Gain Scaling**:
   - Adjust `sbr_envR` energy factors to compensate for core LC crossover roll-off.

## Execution & Benchmark Validation Instructions

### 1. Build setup
```bash
meson setup build --buildtype=release
ninja -C build
```

### 2. Running `faac-benchmark`
Execute HE-v1 scenario benchmarks from `/opt/faac-benchmark`:
```bash
cd /opt/faac-benchmark
export PATH="/app/build/frontend:$PATH"

# Run baseline
python3 run_benchmark.py --encoder faac --profile he-v1 --output /tmp/amd64_he_base.json

# Run candidate
python3 run_benchmark.py --encoder faac --profile he-v1 --output /tmp/amd64_he_cand.json

# Compare baseline and candidate results
python3 compare_results.py /tmp/amd64_he_base.json /tmp/amd64_he_cand.json
```

## Definition of Success
- **Perceptual Audio Quality (MOS)**:
  - Net overall MOS gain across HE-v1 scenarios >= **+0.25 MOS** (target: +0.35 to +0.50 MOS in `48k_stereo_40k` through `48k_stereo_64k`).
  - Elimination of metallic ringing and unvoiced noise smearing artifacts.
  - Baseline and candidate MOS values must both be valid numeric scores (never `null`).
- **Encoding Throughput (Speed)**:
  - Average encoding speed >= **550xRT** (max 5% throughput drop from baseline 619xRT).
- **ROM Footprint**:
  - `libfaac.so` `.text + .rodata` footprint <= **82.0 KB** (strictly passing CI footprint gate).
- **Bitrate Accuracy**:
  - Bitrate error deviation <= **6.0%**.
