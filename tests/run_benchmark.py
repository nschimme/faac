"""
 * FAAC Benchmark Suite
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
"""

import os
import subprocess
import time
import sys
import json
import re
import tempfile
import hashlib

try:
    import visqol_py
    HAS_VISQOL = True
except ImportError:
    HAS_VISQOL = False

# Paths relative to script directory
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
EXTERNAL_DATA_DIR = os.path.join(SCRIPT_DIR, "data", "external")
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "output")

SCENARIOS = {
    "voip": {"mode": "speech", "rate": 16000, "visqol_rate": 16000, "q": 15, "thresh": 2.5},
    "nvr": {"mode": "audio", "rate": 48000, "visqol_rate": 48000, "q": 30, "thresh": 3.0},
    "music_low": {"mode": "audio", "rate": 48000, "visqol_rate": 48000, "q": 60, "thresh": 3.5},
    "music_std": {"mode": "audio", "rate": 48000, "visqol_rate": 48000, "q": 120, "thresh": 4.0},
    "music_high": {"mode": "audio", "rate": 48000, "visqol_rate": 48000, "q": 250, "thresh": 4.3}
}

def get_binary_size(path):
    if os.path.exists(path):
        return os.path.getsize(path)
    return 0

def get_md5(path):
    if not os.path.exists(path):
        return ""
    hash_md5 = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(4096), b""):
            hash_md5.update(chunk)
    return hash_md5.hexdigest()

def run_visqol(ref_wav, deg_wav, mode):
    """Run ViSQOL via Python API and return MOS score."""
    if not HAS_VISQOL:
        print(" ViSQOL Python API not available.")
        return None
    try:
        # visqol-py handles model paths and configuration internally.
        visqol = visqol_py.ViSQOL(mode=mode)
        result = visqol.measure(ref_wav, deg_wav)
        return float(result.moslqo)
    except Exception as e:
        print(f" ViSQOL API error: {e}")
        pass
    return None

def run_benchmark(faac_path, lib_path, precision, coverage=100, run_perceptual=False):
    env = os.environ.copy()

    os.makedirs(OUTPUT_DIR, exist_ok=True)
    results = {
        "matrix": {},
        "throughput": {},
        "lib_size": get_binary_size(lib_path)
    }

    if run_perceptual:
        print(f"Starting perceptual benchmark for {precision}...")
        for name, cfg in SCENARIOS.items():
            data_subdir = "speech" if cfg["mode"] == "speech" else "audio"
            data_dir = os.path.join(EXTERNAL_DATA_DIR, data_subdir)
            if not os.path.exists(data_dir):
                print(f"  [Scenario: {name}] Data directory {data_dir} not found, skipping.")
                continue

            all_samples = sorted([f for f in os.listdir(data_dir) if f.endswith(".wav")])
            num_to_run = max(1, int(len(all_samples) * coverage / 100.0))
            step = len(all_samples) / num_to_run if num_to_run > 0 else 1
            samples = [all_samples[int(i * step)] for i in range(num_to_run)]

            print(f"  [Scenario: {name}] Running {len(samples)} samples (coverage {coverage}%)...")
            for i, sample in enumerate(samples):
                input_path = os.path.join(data_dir, sample)
                key = f"{name}_{sample}"
                print(f"    ({i+1}/{len(samples)}) Processing {sample}...", end="", flush=True)
                output_path = os.path.join(OUTPUT_DIR, f"{key}_{precision}.aac")

                try:
                    subprocess.run([faac_path, "-q", str(cfg["q"]), "-o", output_path, input_path],
                                   env=env, check=True, capture_output=True)

                    mos = None
                    aac_size = os.path.getsize(output_path)

                    with tempfile.TemporaryDirectory() as tmpdir:
                        deg_wav = os.path.join(tmpdir, "deg.wav")
                        subprocess.run(["faad", "-o", deg_wav, output_path], capture_output=True)
                        if os.path.exists(deg_wav):
                            v_rate = cfg["visqol_rate"]
                            v_ref = os.path.join(tmpdir, "vref.wav")
                            v_deg = os.path.join(tmpdir, "vdeg.wav")

                            subprocess.run(["ffmpeg", "-y", "-i", input_path, "-ar", str(v_rate), v_ref], capture_output=True)
                            subprocess.run(["ffmpeg", "-y", "-i", deg_wav, "-ar", str(v_rate), v_deg], capture_output=True)

                            mos = run_visqol(v_ref, v_deg, cfg["mode"])

                    mos_str = f"{mos:.2f}" if mos is not None else "N/A"
                    print(f" done. (MOS: {mos_str})")
                    results["matrix"][key] = {
                        "mos": mos,
                        "size": aac_size,
                        "md5": get_md5(output_path),
                        "thresh": cfg["thresh"],
                        "scenario": name,
                        "filename": sample
                    }
                except Exception as e:
                    print(f" failed: {e}")
                    pass

    print(f"Measuring throughput for {precision}...")
    speech_dir = os.path.join(EXTERNAL_DATA_DIR, "speech")
    if os.path.exists(speech_dir):
        samples = sorted([f for f in os.listdir(speech_dir) if f.endswith(".wav")])
        if samples:
            input_path = os.path.join(speech_dir, samples[0])
            output_path = os.path.join(OUTPUT_DIR, f"throughput_{precision}.aac")
            start_time = time.time()
            try:
                for _ in range(3):
                    subprocess.run([faac_path, "-o", output_path, input_path], env=env, check=True, capture_output=True)
                results["throughput"]["overall"] = (time.time() - start_time) / 3
            except:
                pass

    return results

if __name__ == "__main__":
    if len(sys.argv) < 5:
        print("Usage: python3 tests/run_benchmark.py <faac_bin_path> <lib_path> <precision_name> <output_json> [--perceptual] [--coverage 100]")
        sys.exit(1)

    do_perc = "--perceptual" in sys.argv
    coverage = 100
    if "--coverage" in sys.argv:
        idx = sys.argv.index("--coverage")
        coverage = int(sys.argv[idx+1])

    data = run_benchmark(sys.argv[1], sys.argv[2], sys.argv[3], coverage=coverage, run_perceptual=do_perc)

    # Ensure results directory exists
    output_json = os.path.abspath(sys.argv[4])
    os.makedirs(os.path.dirname(output_json), exist_ok=True)
    with open(output_json, "w") as f:
        json.dump(data, f, indent=2)
