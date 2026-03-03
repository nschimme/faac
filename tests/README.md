# FAAC Perceptual Benchmark Suite

This directory contains scripts and tools for evaluating the perceptual quality and efficiency of the FAAC encoder using ViSQOL (Virtual Speech Quality Objective Listener).

## Prerequisites

- **ViSQOL**: Must be installed and available in your PATH.
- **FFmpeg**: For audio resampling and transcoding.
- **FAAD2**: For decoding AAC files back to WAV for comparison.
- **Python 3**: With `numpy` installed.
- **Meson & Ninja**: For building FAAC.

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
