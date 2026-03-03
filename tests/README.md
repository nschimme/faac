# FAAC Perceptual Benchmark Suite

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
*   **PMLT2014**: [MPEG AAC/HE-AAC/AAC-ELD Listening Test](https://listening-test.coresv.net/) - High-quality audio and speech.
*   **SoundExpert**: [SoundExpert Research Project](https://soundexpert.org/sound-samples) - Diverse music and instrument samples.
*   **TCD-VOIP**: [TCD-VoIP Dataset](https://www.sigmedia.tv/datasets/tcd_voip_ltd/) - Comprehensive speech samples with various degradations.

## Prerequisites & Installation

### Debian/Ubuntu

Install core build and analysis dependencies:
```bash
sudo apt-get update
sudo apt-get install -y meson ninja-build bc faad ffmpeg python3-numpy
```

For detailed dependency information and manual installation of ViSQOL, please refer to:
- [ViSQOL Installation Guide](https://github.com/google/visqol#installation)
- [FFmpeg Documentation](https://ffmpeg.org/documentation.html)

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
     build/frontend/faac \
     build/libfaac/libfaac.so \
     my_test_run \
     results/my_test_run_cand.json \
     --perceptual \
     --coverage 20
   ```

   - `precision_name`: A label for the run (e.g., `single_cand`).
   - `--perceptual`: Enables ViSQOL MOS calculation (slow).
   - `--coverage X`: Only runs X% of the dataset (useful for quick local tests).

## Comparing Results

To generate a report comparing a candidate run against a baseline:

1. **Run a baseline**:
   Generate `results/my_test_run_base.json` using a known good version of FAAC.

2. **Generate the Report**:
   ```bash
   python3 tests/compare_results.py results/
   ```

   The script will look for pairs of `*_base.json` and `*_cand.json` in the specified directory and output a Markdown summary with regressions highlighted at the top.

## Benchmarking Logic

- **VoIP Scenario**: 16kHz mono speech, bitrates ~15-20kbps.
- **NVR Scenario**: 48kHz stereo audio, bitrates ~30-40kbps.
- **Music Scenarios**: 48kHz stereo, quality levels `60`, `120`, and `250`.

The suite uses ViSQOL v3. Audio mode uses the 48kHz music model, while speech mode uses the default wideband speech model.
