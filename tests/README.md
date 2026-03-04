# FAAC Benchmark Suite

FAAC is the high-efficiency encoder for the resource-constrained world. From hobbyist IoT projects to professional surveillance (NVR) and embedded VoIP, we prioritize performance where every cycle and byte matters.

Our **Northstar** is the optimal balance of quality, speed, and size. This suite provides the objective data necessary to ensure that every change moves us closer to that goal without breaking the project's delicate equilibrium.

---

## The "Golden Triangle" Philosophy

We evaluate every contribution against three competing pillars. To remain as approachable and distributable as possible for the global open-source community, we focus on **non-patent encumbered** areas and the standard LC-AAC profile.

1.  **Audio Fidelity**: We target transparent audio quality for our specific bitrates. We use objective metrics like ViSQOL (MOS) to ensure psychoacoustic improvements truly benefit the listener without introducing "metallic" ringing or "underwater" artifacts.
2.  **Computational Efficiency**: FAAC must remain fast. We optimize for low-power ARM and MIPS cores where encoding speed is a critical requirement. We aim to be the go-to encoder for devices with limited CPU budgets.
3.  **Minimal Footprint**: Binary size is a feature. We ensure the library remains small enough to fit within restrictive embedded firmware and IoT storage environments.

---

## Benchmarking Scenarios

| Scenario | Mode | Source | Config | Project Goal |
| :--- | :--- | :--- | :--- | :--- |
| **VoIP** | Speech (16k) | TCD-VOIP | `-q 15` | Clear communication at low bitrates (~15kbps). |
| **NVR** | Speech (16k) | TCD-VOIP | `-q 30` | High-fidelity voice recording (~25kbps). |
| **Music** | Audio (48k) | PMLT / SoundExpert | `-q 60-250` | Full-range transparency for storage & streaming. |
| **Throughput** | Efficiency | Synthetic Signals | N/A | Stability test using 10-minute Sine/Sweep/Noise/Silence. |

---

## Dataset Sources

We are grateful to the following projects for providing high-quality research material:

*   **TCD-VoIP (Sigmedia-VoIP)**: [Listener Test Database](https://www.sigmedia.tv/datasets/tcd_voip_ltd/) - A research database specifically designed for assessing quality in VoIP applications.
*   **Public Multiformat Listening Test (PMLT2014)**: [July 2014 Results](https://listening-test.coresv.net/) - A comprehensive multi-codec test suite.
*   **SoundExpert**: [Sound samples](https://soundexpert.org/sound-samples) - High-precision excerpts from the EBU SQAM CD for transparency testing.

---

## Quick Start

### 1. Install Dependencies
```bash
# System (Ubuntu/Debian)
sudo apt-get update && sudo apt-get install -y meson ninja-build bc ffmpeg

# Python
pip install -r tests/requirements.txt
```

### 2. Prepare Datasets
Downloads samples and generates 10-minute synthetic throughput signals (Sine, Sweep, Noise, Silence).
```bash
python3 tests/setup_datasets.py
```

### 3. Run a Benchmark
Perceptual analysis is enabled by default. Use `--skip-mos` for faster iteration during local development.
```bash
python3 tests/run_benchmark.py build/frontend/faac build/libfaac/libfaac.so my_run tests/results/my_run.json
```

### 4. Compare Results
Generate a high-signal summary comparing your candidate against a baseline.
```bash
python3 tests/compare_results.py tests/results/
```

---

## CI/CD Architecture

Our GitHub Actions environment is hardened for measurement precision:
*   **Hardware Isolation**: Worker processes are pinned to unique CPU cores (`os.sched_setaffinity`) to eliminate measurement jitter.
*   **Robust Throughput**: We use 10-minute synthetic signals and multi-run averaging to provide a rock-solid performance signal.
*   **Intelligent Caching**: We cache the entire build environment and baseline results to provide rapid feedback to contributors.
