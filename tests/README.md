# FAAC Benchmark Suite

This suite provides the objective data necessary to evaluate encoder improvements and ensure high-quality releases. By standardizing our metrics, we establish a **Northstar** for the project: shipping better audio, faster, with a smaller footprint.

## Philosophy: The "Golden Triangle"

FAAC occupies a unique niche: high-efficiency encoding for resource-constrained environments. While we do not compete with FDK-AAC or Opus on raw audio quality at high bitrates, we must maintain transparency within our targeted scenarios.

Every change is evaluated against three competing pillars. A gain in one must not come at a catastrophic cost to the others.

1.  **Perceptual Quality (MOS)**: Objective quality measured via ViSQOL v3. We target specific Mean Opinion Score thresholds for each scenario to ensure transparency.
2.  **Throughput (CPU)**: Encoding speed must remain high. FAAC is optimized for low-power IoT and embedded devices.
3.  **Binary Size**: The footprint of `libfaac.so` is critical for minimal firmware images.

---

## Benchmarking Scenarios

The suite evaluates FAAC across three primary real-world scenarios:

| Scenario | Mode | Source | Config | Goal |
| :--- | :--- | :--- | :--- | :--- |
| **VoIP** | Speech (16k) | TCD-VOIP | `-q 15` | Clear communication at low bitrates (~15kbps). |
| **NVR** | Speech (16k) | TCD-VOIP | `-q 30` | High-fidelity surveillance recording (~25kbps). |
| **Music** | Audio (48k) | PMLT / SoundExpert | `-q 60-250` | Full-range stereo transparency across bitrates. |

---

## Quick Start

### 1. Install Dependencies
```bash
# System
sudo apt-get update && sudo apt-get install -y meson ninja-build bc ffmpeg

# Python
pip install -r tests/requirements.txt
```

### 2. Prepare Datasets
Downloads and prepares samples using an adaptive duration strategy (5-10s) and generates 10-minute synthetic throughput signals.
```bash
python3 tests/setup_datasets.py
```

### 3. Run a Benchmark
Perceptual analysis is enabled by default. Use `--skip-mos` for faster iteration during local development.
```bash
python3 tests/run_benchmark.py <faac_bin> <lib_path> <run_label> <output.json>
```

### 4. Compare Results
Generate a high-signal report comparing a candidate against a baseline.
```bash
python3 tests/compare_results.py tests/results/
```

---

## Automated CI/CD

This suite is fully integrated into GitHub Actions (`benchmark.yml`). To ensure maximum precision and reliability, the CI environment implements:

*   **Hardware Isolation**: Worker processes use `ProcessPoolExecutor` and are pinned to unique CPU cores (`os.sched_setaffinity`) to eliminate measurement jitter.
*   **Intelligent Caching**: The entire Python `.venv`, external datasets, and baseline results are cached to provide fast turnaround times for PRs.
*   **Bitstream Consistency**: We track MD5 identity to verify that pure refactors or optimizations do not inadvertently alter the psychoacoustic output.

## Reading the Maintainer Summary

The benchmark report focuses on **signal over noise**:
- **Regressions (❌)**: Any MOS drop > 0.1 or failure to meet scenario thresholds.
- **Significant Wins (🌟)**: MOS improvements > 0.1.
- **Consistency**: Percentage of bit-identical samples vs. the baseline.
- **TP Breakdown**: Detailed throughput deltas across synthetic signals (Sine, Sweep, Noise, Silence).
