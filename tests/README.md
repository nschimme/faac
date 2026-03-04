# FAAC Benchmark Suite

This suite provides the objective data necessary to evaluate encoder improvements and ensure high-quality releases. By standardizing our metrics, we establish a **Northstar** for the project: shipping better audio, faster, with a smaller footprint.

## Our Customers & Use Cases

FAAC provides high-efficiency AAC encoding for resource-constrained environments where every cycle and byte matters.

1.  **IoT & Surveillance (NVR)**: Manufacturers building low-power cameras and recording devices. They require reliable, clear audio recording that fits within minimal storage and power budgets.
2.  **Embedded Communication (VoIP)**: Developers of residential intercoms, industrial headsets, and low-bandwidth communication systems. They need resilient, low-latency speech encoding that maintains intelligibility even over unstable networks.
3.  **Transparency Focus**: While FAAC does not compete with FDK-AAC or Opus on raw audio quality at high bitrates, our users expect transparent, artifact-free performance within our targeted operational ranges.

## Who This Suite Helps

*   **Maintainers**: Provides the confidence to merge PRs by proving that a change improves the encoder—or at least doesn't cause a quality or performance regression.
*   **Developers**: Offers rapid, automated feedback during the implementation of new psychoacoustic models or performance optimizations.
*   **End Users**: Ensures that every new version of FAAC remains a reliable choice for their critical firmware and communication products.

---

## The "Golden Triangle" Philosophy

Every change is evaluated against three competing pillars. A gain in one must not come at a catastrophic cost to the others.

1.  **Audio Fidelity**: Our Northstar is delivering transparent audio quality for our target bitrates. We use objective metrics like ViSQOL (MOS) to ensure that psychoacoustic improvements truly benefit the listener.
2.  **Computational Efficiency**: FAAC must remain fast. We optimize for low-power ARM and MIPS cores where encoding speed is a critical requirement, not just a preference.
3.  **Minimal Footprint**: Binary size is a feature. We ensure the library remains small enough to fit within the most restrictive embedded firmware and IoT storage environments.

---

## Benchmarking Scenarios

| Scenario | Mode | Source | Config | Business Goal |
| :--- | :--- | :--- | :--- | :--- |
| **VoIP** | Speech (16k) | TCD-VOIP | `-q 15` | Clear communication at low bitrates (~15kbps). |
| **NVR** | Speech (16k) | TCD-VOIP | `-q 30` | High-fidelity surveillance recording (~25kbps). |
| **Music** | Audio (48k) | PMLT / SoundExpert | `-q 60-250` | Full-range stereo transparency for storage & streaming. |

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
Downloads samples and generates 10-minute synthetic throughput signals.
```bash
python3 tests/setup_datasets.py
```

### 3. Run a Benchmark
Perceptual analysis is enabled by default. Use `--skip-mos` for faster iteration during local development.
```bash
python3 tests/run_benchmark.py <faac_bin> <lib_path> <run_label> <output.json>
```

### 4. Compare Results
Generate a high-signal summary comparing a candidate against a baseline.
```bash
python3 tests/compare_results.py tests/results/
```

---

## CI/CD Architecture

To provide deterministic hardware metrics, our GitHub Actions environment utilizes:
*   **Hardware Isolation**: Worker processes are pinned to unique CPU cores (`os.sched_setaffinity`) to eliminate measurement jitter.
*   **Synthetic Benchmarking**: 10-minute Sine, Sweep, Noise, and Silence signals provide rock-solid throughput data.
*   **Intelligent Caching**: Successfully generated environments and baseline results are cached to ensure fast feedback loops.
