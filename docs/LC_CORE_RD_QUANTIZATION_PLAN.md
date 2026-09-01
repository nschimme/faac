# LC Core Rate-Distortion & Quantization Optimization Plan

## Objective & Goal
Close the LC core audio quality MOS gap against Apple AAC and FDK-AAC at mid-to-high bitrates (56k, 96k, 128k, 160k, 192k, and 256k LC stereo). FAAC currently achieves 4.52–4.94 MOS in high-bitrate LC scenarios compared to 4.71–4.98 for Apple AAC and 4.67–4.95 for FDK-AAC, despite FAAC maintaining equivalent stereo fidelity (0.97–0.99) and transient timing.

## Problem Statement
FAAC's single-pass quantization in `assign_band_codebooks()` (`libfaac/quantize.c`) derives scalefactor gains directly from a coarse logarithmic energy target (`lrintf(log10f(target[sb]) * sfstep - sf_enrg_avg + log10_w_sf)`). This open-loop estimation leads to localized over-quantization, non-zero spectral holes between harmonic peaks, and excessive scalefactor differential deltas (HCB_DELTA / book 12 bit consumption).

## Proposed Architectural Levers (in `libfaac/quantize.c`)
1. **Single-Pass SMR Distortion Feedback**:
   - Calculate quantization distortion `D_sfb = sum((xr[i] - xq[i])^2)` during MDCT line quantization.
   - Compare `D_sfb` against psychoacoustic masking threshold target `T_sfb`.
   - If `D_sfb > 2.0 * T_sfb`, apply a lightweight +1 scalefactor step adjustment without triggering full outer-loop re-quantization.
2. **Non-Zero Spectral Hole Protection**:
   - Prevent isolated zeroed scale-factor bands between active harmonic SFBs by capping scalefactor delta jumps (`diff <= 60`).
3. **Band-Wise Psychoacoustic Noise Filling**:
   - Inject low-level pseudo-random noise into zeroed high-frequency scale-factor bands below the psychoacoustic masking threshold to eliminate metallic "birdie" artifacts.
4. **Scalefactor Differential Delta Pruning**:
   - Prune scalefactor deltas (`sf[band] - sf[band-1]`) that cross codebook delta limits to save bitstream payload bits.

## Execution & Benchmark Validation Instructions

### 1. Build setup
Ensure the candidate build is compiled with optimization:
```bash
meson setup build --buildtype=release
ninja -C build
```

### 2. Running `faac-benchmark`
Execute the benchmark from `/opt/faac-benchmark`:
```bash
cd /opt/faac-benchmark
# Ensure PATH points to the build directory containing the candidate faac binary
export PATH="/app/build/frontend:$PATH"

# Run baseline (cached in /tmp/ to avoid redundant runs)
python3 run_benchmark.py --encoder faac --profile lc --output /tmp/amd64_lc_base.json

# Run candidate
python3 run_benchmark.py --encoder faac --profile lc --output /tmp/amd64_lc_cand.json

# Compare baseline and candidate results
python3 compare_results.py /tmp/amd64_lc_base.json /tmp/amd64_lc_cand.json
```

## Definition of Success
- **Perceptual Audio Quality (MOS)**:
  - Net overall MOS gain across LC scenarios >= **+0.10 MOS** (target: +0.15 to +0.25 MOS in `48k_stereo_96k` through `48k_stereo_256k`).
  - No clip MOS regressions > 0.10 MOS.
  - Baseline and candidate MOS values must both be valid numeric scores (never `null`).
- **Encoding Throughput (Speed)**:
  - Average encoding speed >= **600xRT** (max 3% throughput drop from baseline 690xRT).
- **ROM Footprint**:
  - `libfaac.so` `.text + .rodata` footprint <= **82.0 KB** (strictly passing CI footprint gate).
- **Bitrate Accuracy**:
  - Bitrate error deviation <= **8.0%**.
