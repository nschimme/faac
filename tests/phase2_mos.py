"""
 * FAAC Benchmark Suite - MOS Computation Script (Python API strategy)
 * Copyright (C) 2026 Nils Schimmelmann
"""

import os
import sys
import json
import tempfile
import ffmpeg
import visqol_py
from visqol_py import ViSQOLMode
import concurrent.futures
import multiprocessing
try:
    from config import SCENARIOS
except ImportError:
    from .config import SCENARIOS

# Process-local storage for ViSQOL instances
_process_visqol_instances = {}

def get_process_visqol(mode_str):
    if mode_str not in _process_visqol_instances:
        mode = ViSQOLMode.SPEECH if mode_str == "speech" else ViSQOLMode.AUDIO
        _process_visqol_instances[mode_str] = visqol_py.ViSQOL(mode=mode)
    return _process_visqol_instances[mode_str]

def compute_single_mos(key, entry, aac_dir, external_data_dir):
    scenario_name = entry.get("scenario")
    filename = entry.get("filename")
    cfg = SCENARIOS.get(scenario_name)

    if not cfg:
        return key, None

    data_subdir = "speech" if cfg["mode"] == "speech" else "audio"
    ref_input_path = os.path.join(external_data_dir, data_subdir, filename)

    aac_files = [f for f in os.listdir(aac_dir) if (f.startswith(key + "_single.aac") or f.startswith(key + "_double.aac"))]
    if not aac_files:
        return key, None

    aac_path = os.path.join(aac_dir, aac_files[0])

    with tempfile.TemporaryDirectory() as tmpdir:
        v_ref = os.path.join(tmpdir, "vref.wav")
        v_deg = os.path.join(tmpdir, "vdeg.wav")
        v_rate = cfg["visqol_rate"]
        v_channels = 1 if cfg["mode"] == "speech" else 2

        try:
            ffmpeg.input(ref_input_path).output(
                v_ref, ar=v_rate, ac=v_channels, sample_fmt='s16').run(
                quiet=True, overwrite_output=True)
            ffmpeg.input(aac_path).output(
                v_deg, ar=v_rate, ac=v_channels, sample_fmt='s16').run(
                quiet=True, overwrite_output=True)

            if os.path.exists(v_ref) and os.path.exists(v_deg):
                visqol = get_process_visqol(cfg["mode"])
                result = visqol.measure(v_ref, v_deg)
                return key, float(result.moslqo)
        except Exception as e:
            print(f"  Error computing MOS for {key}: {e}")

    return key, None

def main():
    if len(sys.argv) < 4:
        print("Usage: python3 compute_mos.py <results_json> <aac_dir> <external_data_dir>")
        sys.exit(1)

    results_path = sys.argv[1]
    aac_dir = sys.argv[2]
    external_data_dir = sys.argv[3]

    with open(results_path, 'r') as f:
        data = json.load(f)

    matrix = data.get("matrix", {})
    total = len(matrix)
    num_cpus = os.cpu_count() or 1
    print(f"Computing MOS for {total} samples using {num_cpus} cores...")

    with concurrent.futures.ProcessPoolExecutor(max_workers=num_cpus) as executor:
        futures = {
            executor.submit(compute_single_mos, key, entry, aac_dir, external_data_dir): key
            for key, entry in matrix.items()
        }

        for i, future in enumerate(concurrent.futures.as_completed(futures)):
            key, mos = future.result()
            if mos is not None:
                matrix[key]["mos"] = mos
            print(f"  ({i+1}/{total}) {key}: {mos if mos is not None else 'N/A'}")

    with open(results_path, 'w') as f:
        json.dump(data, f, indent=2)
    print("MOS computation complete.")

if __name__ == "__main__":
    main()
