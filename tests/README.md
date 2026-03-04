# FAAC Benchmark Suite

This directory contains scripts and tools for evaluating the perceptual quality and efficiency of the FAAC encoder using ViSQOL (Virtual Speech Quality Objective Listener).

## Philosophy: The "Golden Triangle"

When evaluating encoder changes, we aim for an optimal balance between:
1. **Perceptual Quality (MOS)**: Measured via ViSQOL. A score above the scenario threshold is required.
2. **Throughput (CPU)**: Performance should not regress. FAAC is optimized for low-power IoT/NVR/VoIP devices.
3. **Binary Size**: Minimal footprint is critical for embedded firmware.

A gain in one area must not come at a "catastrophic" cost to another (e.g., a 10% speedup that drops MOS by 0.5 is a failure).

## Metrics Definition

### Qualitative Metrics
*   **MOS (Mean Opinion Score)**: An objective estimate of perceptual audio quality. We use ViSQOL v3, which maps internal similarity metrics to a 1.0 - 5.0 scale (higher is better).
    *   **Goal**: Ensure audio remains clear and free of artifacts like "underwater" effects or metallic ringing, especially at low bitrates.
*   **Bitstream Consistency**: Measures how often the candidate encoder produces bit-identical output to the baseline.
    *   **Goal**: Pure refactors or performance optimizations should ideally maintain 100% consistency. Changes to psychoacoustic models will naturally lower this.

### Quantitative Metrics
*   **Throughput (CPU)**: Measured as the time taken to encode a set of reference samples.
    *   **Goal**: Maintain or improve encoding speed. FAAC's niche is high-efficiency encoding for resource-constrained hardware.
*   **Binary Size**: The footprint of `libfaac.so`.
    *   **Goal**: Keep the library small enough to fit in minimal NVR or IoT firmware images.
*   **Output Size (Bitrate Accuracy)**: The size of the resulting `.aac` file.
    *   **Goal**: Ensure the encoder respects quality/bitrate targets and doesn't "bloat" the output with inefficient coding.

## Dataset Sources
*   **Public Multiformat Listening Test @ 96 kbps (July 2014)**: [PMLT2014](https://listening-test.coresv.net/) - A multi-codec listening test (AAC, Vorbis, Opus, MP3) designed to identify quality differences at typical streaming bitrates.
*   **SoundExpert Sound samples**: [SoundExpert](https://soundexpert.org/sound-samples) - A selection of excerpts from the EBU SQAM (Sound Quality Assessment Material) CD, including castanets, glockenspiel, and harpsichord, used for high-precision transparency testing.
*   **TCD-VoIP (Sigmedia-VoIP) Listener Test Database**: [TCD-VOIP](https://www.sigmedia.tv/datasets/tcd_voip_ltd/) - A research database of degraded speech specifically designed for assessing quality in VoIP applications, featuring degradations like packet loss (chop), clipping, and echo.

## Prerequisites & Installation

### Debian/Ubuntu

Install core build and analysis dependencies:
```bash
sudo apt-get update
sudo apt-get install -y meson ninja-build bc ffmpeg
```

Install Python dependencies (recommended within a virtual environment):
```bash
python3 -m venv venv
source venv/bin/activate
pip install -r tests/requirements.txt
```

Python dependencies include:
- `numpy`: Numerical processing.
- `protobuf==3.20.3`: Required for ViSQOL API compatibility.
- `ffmpeg-python`: Python bindings for FFmpeg, used for audio decoding and preparation.
- `visqol-py`: Python wrapper for the ViSQOL objective audio quality metric.

For detailed information, see:
- [visqol-py Repository](https://github.com/diggerdu/visqol-py)
- [ffmpeg-python Repository](https://github.com/kkroening/ffmpeg-python)

**Note**: ViSQOL is currently most reliable on **Ubuntu 22.04**.

## Setup Datasets

Before running benchmarks, you need to download and prepare the external datasets (PMLT2014, TCD-VOIP, SoundExpert).

```bash
python3 tests/setup_datasets.py
```

This will download the datasets to `tests/data/external/` and prepare them (resampling to 16kHz for speech and 48kHz for audio, trimming to 7 seconds).

## Running Benchmarks Manually

1. **Build FAAC**:
   ```bash
   meson setup build -Dfloating-point=single --buildtype=release
   ninja -C build
   ```

2. **Run the Benchmark Script**:
   ```bash
   python3 tests/run_benchmark.py \
     build_cand/frontend/faac \
     build_cand/libfaac/libfaac.so \
     my_test_run \
     tests/results/my_test_run_cand.json \
     --perceptual \
     --coverage 100
   ```

   - `precision_name`: A label for the run (e.g., `single_cand`).
   - `--perceptual`: Enables ViSQOL MOS calculation (slow).
   - `--coverage X`: Percentage of the dataset to run (set to `100` for full verification).

## CI/CD Benchmarking

This suite is integrated into the GitHub Actions workflow (`.github/workflows/benchmark.yml`). On every pull request and push to the `master` branch, the following steps are performed:
1.  **Environment Setup**: Installs system dependencies and Python requirements from `tests/requirements.txt`.
2.  **Dataset Caching**: External datasets are cached to accelerate subsequent runs.
3.  **Candidate Build & Benchmark**: The code in the current branch is built and benchmarked across all scenarios.
4.  **Baseline Build & Benchmark**: The code from the target branch (e.g., `master`) is built and benchmarked using the same scripts.
5.  **Consolidated Report**: Results are compared using `tests/compare_results.py`, and a detailed Markdown report is posted as a PR comment.
6.  **Regression Check**: The workflow fails if significant regressions in MOS, throughput, or binary size are detected.

## Comparing Results

To generate a report comparing a candidate run against a baseline:

1. **Run a baseline**:
   Generate `tests/results/my_test_run_base.json` using a known good version of FAAC.

2. **Generate the Report**:
   ```bash
   python3 tests/compare_results.py tests/results/
   ```

   The script will look for pairs of `*_base.json` and `*_cand.json` in the specified directory and output a Markdown summary with regressions highlighted at the top.

## Benchmarking Scenarios (Test Suites)

To provide a high-value signal for merge decisions, the suite evaluates FAAC across three primary scenarios using the **ViSQOL v3** objective metric:

| Scenario | Mode | Source | Config | Description |
| :--- | :--- | :--- | :--- | :--- |
| **VoIP** | Speech (16kHz) | TCD-VOIP | `-q 15` | Low-bitrate (~15kbps) mono speech. Tests resilience to artifacts in communication. |
| **NVR** | Audio (48kHz) | SoundExpert | `-q 30` | Mid-bitrate (~40kbps) stereo security audio. Focuses on environmental clarity. |
| **Music (Low/Std/High)** | Audio (48kHz) | PMLT2014 | `-q 60/120/250` | Full-range stereo music. Evaluates transparency across various bitrates. |

### Parallel Execution
Benchmarks are parallelized across all CPU cores using `ThreadPoolExecutor`. This is critical for ViSQOL analysis, which is the most intensive part of the suite.

## Philosophy: "Less is More"
Maintainers focus on the **Maintainer Summary** generated by `compare_results.py`. This table highlights:
- **Regressions**: (❌) Any drop in MOS > 0.1 or failure to meet the scenario threshold.
- **New Wins**: (🆕) Baseline failed threshold, but Candidate passed.
- **Consistency**: (%) MD5 match of the bitstream against the baseline.
- **Efficiency**: (%) Relative changes in Throughput (CPU) and Binary Size.
