import os
import subprocess
import time
import hashlib
import sys
import json
import re

RATES = [16000, 48000]
QUALITIES = [10, 100, 250]
TYPES = ["sine", "sweep", "noise", "harpsichord", "castanets", "applause", "silence"]
DATA_DIR = "tests/data"
OUTPUT_DIR = "tests/output"

def get_binary_size(path):
    if os.path.exists(path):
        return os.path.getsize(path)
    return 0

def run_visqol(ref_wav, deg_wav, rate):
    """Run ViSQOL and parse MOS score."""
    try:
        mode = "wideband" if rate <= 16000 else "fullband"
        cmd = ["visqol", mode, "--reference", ref_wav, "--degraded", deg_wav]
        proc = subprocess.run(cmd, capture_output=True, text=True)
        match = re.search(r"MOS-LQO:\s+([-.\d]+)", proc.stdout)
        if match:
            return float(match.group(1))
    except:
        pass
    return None

def run_benchmark(faac_path, lib_path, precision, run_perceptual=False):
    env = os.environ.copy()
    env["FAAC_NO_SIMD"] = "1"

    os.makedirs(OUTPUT_DIR, exist_ok=True)
    results = {
        "matrix": {},
        "throughput": {},
        "lib_size": get_binary_size(lib_path)
    }

    # Throughput (all platforms)
    for rate in RATES:
        for t in ["sine", "noise"]:
            input_path = os.path.join(DATA_DIR, f"{t}_{rate}_long.wav")
            if not os.path.exists(input_path): continue

            output_path = os.path.join(OUTPUT_DIR, f"{t}_{rate}_long_{precision}.aac")
            start_time = time.time()
            try:
                subprocess.run([faac_path, "-o", output_path, input_path], env=env, check=True, capture_output=True)
                results["throughput"][f"{t}_{rate}"] = time.time() - start_time
            except:
                pass

    # Perceptual (Optional, Linux only)
    if run_perceptual:
        for rate in RATES:
            for quality in QUALITIES:
                for t in TYPES:
                    input_path = os.path.join(DATA_DIR, f"{t}_{rate}.wav")
                    if not os.path.exists(input_path): continue

                    key = f"{t}_{rate}_q{quality}"
                    output_path = os.path.join(OUTPUT_DIR, f"{key}_{precision}.aac")
                    deg_wav = output_path.replace(".aac", "_deg.wav")

                    try:
                        subprocess.run([faac_path, "-q", str(quality), "-o", output_path, input_path],
                                       env=env, check=True, capture_output=True)

                        mos = None
                        try:
                            # Use 'faad' for decoding
                            subprocess.run(["faad", "-o", deg_wav, output_path], capture_output=True)
                            if os.path.exists(deg_wav):
                                mos = run_visqol(input_path, deg_wav, rate)
                        except:
                            pass
                        finally:
                            if os.path.exists(deg_wav): os.remove(deg_wav)

                        results["matrix"][key] = {"mos": mos}
                    except:
                        pass

    return results

if __name__ == "__main__":
    if len(sys.argv) < 5:
        print("Usage: python3 tests/run_benchmarks.py <faac_bin_path> <lib_path> <precision_name> <output_json> [--perceptual]")
        sys.exit(1)

    do_perc = "--perceptual" in sys.argv
    data = run_benchmark(sys.argv[1], sys.argv[2], sys.argv[3], run_perceptual=do_perc)
    with open(sys.argv[4], "w") as f:
        json.dump(data, f, indent=2)
