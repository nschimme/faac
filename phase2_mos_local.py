"""
 * FAAC Benchmark Suite (Local ViSQOL Adapter)
 * Adapted to use local bazel-built visqol binary for environments where Docker is restricted.
"""

import os
import sys
import json
import tempfile
import subprocess
import concurrent.futures

# Attempt to load scenarios from config if available in path
try:
    from config import SCENARIOS
except ImportError:
    # Fallback to local copy of essential scenarios if config.py is missing
    SCENARIOS = {
        "voip": {"mode": "speech", "visqol_rate": 16000},
        "vss": {"mode": "speech", "visqol_rate": 16000},
        "music_low": {"mode": "audio", "visqol_rate": 48000},
        "music_std": {"mode": "audio", "visqol_rate": 48000},
        "music_high": {"mode": "audio", "visqol_rate": 48000},
    }

# Update these paths as needed for your environment
VISQOL_BIN = os.environ.get("VISQOL_BIN", "/app/visqol/bazel-bin/visqol")
MODEL_DIR = os.environ.get("VISQOL_MODEL_DIR", "/app/visqol/model")

def compute_single_mos(key, entry, aac_dir, external_data_dir, results_path):
    scenario_name = entry.get("scenario")
    filename = entry.get("filename")
    cfg = SCENARIOS.get(scenario_name)

    if not cfg:
        return key, None

    data_subdir = "speech" if cfg["mode"] == "speech" else "audio"
    ref_input_path = os.path.join(external_data_dir, data_subdir, filename)

    results_filename = os.path.basename(results_path)
    precision_suffix = ""
    if "_base.json" in results_filename:
        precision_suffix = results_filename.replace("_base.json", "_base.aac")
    elif "_cand.json" in results_filename:
        precision_suffix = results_filename.replace("_cand.json", "_cand.aac")

    target_filename = f"{key}_{precision_suffix}"
    aac_path = os.path.join(aac_dir, target_filename)

    if not os.path.exists(aac_path):
        aac_files = [f for f in os.listdir(aac_dir) if f.startswith(key) and f.endswith(".aac")]
        if not aac_files:
            return key, None
        aac_path = os.path.join(aac_dir, aac_files[0])

    with tempfile.TemporaryDirectory() as tmpdir:
        v_ref = os.path.join(tmpdir, "vref.wav")
        v_deg = os.path.join(tmpdir, "vdeg.wav")
        v_rate = cfg["visqol_rate"]
        v_channels = 1 if cfg["mode"] == "speech" else 2

        try:
            subprocess.run(['ffmpeg', '-i', ref_input_path, '-ar', str(v_rate), '-ac', str(v_channels), '-sample_fmt', 's16', v_ref],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            subprocess.run(['ffmpeg', '-i', aac_path, '-ar', str(v_rate), '-ac', str(v_channels), '-sample_fmt', 's16', v_deg],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

            cmd = [VISQOL_BIN, "--reference_file", v_ref, "--degraded_file", v_deg]

            if cfg["mode"] == "speech":
                cmd.append("--use_speech_mode")
                cmd.extend(["--similarity_to_quality_model", os.path.join(MODEL_DIR, "lattice_tcditugenmeetpackhref_ls2_nl60_lr12_bs2048_learn.005_ep2400_train1_7_raw.tflite")])
            else:
                cmd.extend(["--similarity_to_quality_model", os.path.join(MODEL_DIR, "libsvm_nu_svr_model.txt")])

            result = subprocess.run(cmd, capture_output=True, text=True, check=True)

            for line in result.stdout.splitlines():
                if "MOS-LQO:" in line:
                    mos = float(line.split()[-1])
                    return key, mos

        except Exception as e:
            print(f"  Error computing MOS for {key}: {e}")

    return key, None

def main():
    if len(sys.argv) < 4:
        print("Usage: python3 phase2_mos.py <results_json> <aac_dir> <external_data_dir>")
        sys.exit(1)

    results_path = sys.argv[1]
    aac_dir = sys.argv[2]
    external_data_dir = sys.argv[3]

    with open(results_path, 'r') as f:
        data = json.load(f)

    matrix = data.get("matrix", {})
    total = len(matrix)
    num_cpus = os.cpu_count() or 1
    print(f"Computing MOS for {total} samples using local ViSQOL ({num_cpus} cores)...")

    with concurrent.futures.ProcessPoolExecutor(max_workers=num_cpus) as executor:
        futures = {
            executor.submit(compute_single_mos, key, entry, aac_dir, external_data_dir, results_path): key
            for key, entry in matrix.items()
        }

        for i, future in enumerate(concurrent.futures.as_completed(futures)):
            key, mos = future.result()
            if mos is not None:
                matrix[key]["mos"] = mos
            mos_str = f"{mos:.2f}" if mos is not None else "N/A"
            print(f"  ({i+1}/{total}) {key}: {mos_str}")

    with open(results_path, 'w') as f:
        json.dump(data, f, indent=2)
    print("Phase 2 (MOS) complete.")

if __name__ == "__main__":
    main()
