# Joint CPE Block Switching & Window Shape Selection Plan

## Objective & Goal
Eliminate pre-echo artifacts and maximize joint M/S stereo coding gain across 24k–64k LC and HE stereo bitrates. Currently, per-channel window shape/sequence decisions in `libfaac/blockswitch.c` (`BlockSwitch()`) occur prior to CPE grouping in `libfaac/quantize.c` (`BlocGroupCPE()`), creating window shape mismatches between Left and Right channels during transient attacks.

## Problem Statement
When one channel in a CPE pair detects a transient and switches to short windows (`EIGHT_SHORT_SEQUENCE`) while the other remains in long windows (`ONLY_LONG_WINDOW`), M/S joint stereo coding (`AACstereo` in `libfaac/stereo.c`) must be disabled for those frame windows, forcing dual-mono fallback. This destroys M/S coding gain and increases pre-echo distortion on pan-stereo transients.

## Proposed Architectural Levers (in `libfaac/blockswitch.c`)
1. **Joint Perceptual Entropy (PE) Transient Coupling**:
   - Evaluate combined perceptual entropy (`PE_L + PE_R`) against transient attack thresholds.
   - Force common window sequence (`EIGHT_SHORT_SEQUENCE` / `LONG_START_SEQUENCE` / `LONG_STOP_SEQUENCE`) across both channels in a CPE pair whenever either channel detects a transient onset.
2. **Synchronized KBD vs. Sine Window Shape Selection**:
   - Synchronize Kaiser-Bessel-Derived (KBD) vs. Sine window shape selection across Left and Right channels in CPE elements to optimize MDCT energy compaction and M/S cross-channel cancellation.

## Execution & Benchmark Validation Instructions

### 1. Build setup
```bash
meson setup build --buildtype=release
ninja -C build
```

### 2. Running `faac-benchmark`
Execute LC profile benchmarks from `/opt/faac-benchmark`:
```bash
cd /opt/faac-benchmark
export PATH="/app/build/frontend:$PATH"

# Run baseline
python3 run_benchmark.py --encoder faac --profile lc --output /tmp/amd64_cpe_base.json

# Run candidate
python3 run_benchmark.py --encoder faac --profile lc --output /tmp/amd64_cpe_cand.json

# Compare baseline and candidate results
python3 compare_results.py /tmp/amd64_cpe_base.json /tmp/amd64_cpe_cand.json
```

## Definition of Success
- **Perceptual Audio Quality (MOS)**:
  - Net overall MOS gain across 24k–64k stereo scenarios >= **+0.10 MOS** (target: +0.15 to +0.30 MOS on transient clips).
  - Baseline and candidate MOS values must both be valid numeric scores (never `null`).
- **Stereo Image Fidelity**:
  - Stereo fidelity score >= **0.965** across 24k–64k stereo.
- **Encoding Throughput (Speed)**:
  - Average encoding speed >= **650xRT** (max 2% throughput drop).
- **ROM Footprint**:
  - `libfaac.so` `.text + .rodata` footprint <= **82.0 KB** (strictly passing CI footprint gate).
