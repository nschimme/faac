# Machine Learning Optimized Audio Shaping in FAAC

## Overview
FAAC supports an offline machine-learning optimization workflow to tune psychoacoustic audio shaping parameters, temporal noise shaping (TNS) admission thresholds, and mid/side (M/S) stereo coupling.

The workflow consists of:
1. **Telemetry Data Logging**: Captures per-frame audio features during encoding with zero runtime overhead when inactive.
2. **Dataset Extraction**: Pairs telemetry logs with `faac-benchmark` evaluation metrics (perceptual MOS, BD-rate, transient NPER, and stereo image coherence).
3. **Continuous Model Training**: Uses `scipy.optimize` (L-BFGS-B) to train parametric models and export pure C11 headers (`libfaac/ml_shaping_model.h`).
4. **Embedded In-Tree Inference**: Evaluates continuous learned models in `libfaac` with zero external runtime dependencies and sub-5-cycle execution overhead.

## Enabling Data Logging
Data logging can be enabled in two ways:
- **CLI Flag**: `faac input.wav -o output.m4a --log-shaping telemetry.csv`
- **Environment Variable**: `export FAAC_LOG_SHAPING=telemetry.csv`
- **C API**: `faac_set_log_shaping("telemetry.csv")`

## Training & Model Generation
1. Ingest telemetry CSVs and benchmark evaluation JSONs:
   ```bash
   python3 scripts/ml_shaping/build_dataset.py --telemetry-dir ./logs --benchmark-json ./results/base.json --output dataset.json
   ```
2. Fit continuous parameters and emit C model headers:
   ```bash
   python3 scripts/ml_shaping/train_shaping_model.py --dataset dataset.json --output-header libfaac/ml_shaping_model.h
   ```
