import os
import subprocess
import time
import hashlib
import sys
import json
import re

RATES = [16000, 44100, 48000]
QUALITIES = [10, 100, 250]
TYPES = ["sine", "sweep", "noise", "harpsichord", "castanets", "applause", "silence"]
DATA_DIR = "tests/data"
OUTPUT_DIR = "tests/output"

def get_md5(filename):
    hash_md5 = hashlib.md5()
    with open(filename, "rb") as f:
        for chunk in iter(lambda: f.read(4096), b""):
            hash_md5.update(chunk)
    return hash_md5.hexdigest()

def get_binary_size(path):
    if os.path.exists(path):
        return os.path.getsize(path)
    return 0

def run_peaq(ref_wav, deg_wav):
    """Run gst-peaq if available."""
    try:
        # Example: gst-launch-1.0 filesrc location=ref.wav ! wavparse ! peaq reference=true ! fakesink \
        #          filesrc location=deg.wav ! wavparse ! peaq ! fakesink
        # This is complex to parse from gst-launch output, so we assume a simpler wrapper or tool if present
        # For now, we provide the placeholder for the maintainer to plug in their specific peaq binary
        pass
    except:
        pass
    return None

def run_benchmark(faac_path, precision, scalar=True):
    env = os.environ.copy()
    if scalar:
        env["FAAC_NO_SIMD"] = "1"
    env["FAAC_DUMP_MSE"] = "1"

    os.makedirs(OUTPUT_DIR, exist_ok=True)
    results = {"matrix": {}, "binary_size": get_binary_size(faac_path)}

    print(f"--- Matrix Benchmarking {precision} precision (Scalar: {scalar}) ---")

    for rate in RATES:
        for quality in QUALITIES:
            for t in TYPES:
                input_path = os.path.join(DATA_DIR, f"{t}_{rate}.wav")
                if not os.path.exists(input_path):
                    continue

                key = f"{t}_{rate}_q{quality}"
                output_path = os.path.join(OUTPUT_DIR, f"{key}_{precision}.aac")

                start_time = time.time()
                cmd = [faac_path, "-q", str(quality), "-o", output_path, input_path]
                try:
                    proc = subprocess.run(cmd, env=env, check=True, capture_output=True, text=True)
                    elapsed = time.time() - start_time
                    md5 = get_md5(output_path)

                    mse = re.findall(r"MSE: ([\d.]+)", proc.stdout)
                    holes = re.findall(r"HOLES: ([\d.]+)%", proc.stdout)
                    ms = re.findall(r"MS_RATIO: ([\d.]+)%", proc.stdout)

                    results["matrix"][key] = {
                        "time": elapsed,
                        "md5": md5,
                        "mse": float(mse[-1]) if mse else 0,
                        "holes": float(holes[-1]) if holes else 0,
                        "ms_ratio": float(ms[-1]) if ms else 0,
                        "odg": None # Maintainer can populate via gst-peaq
                    }
                    print(f"{key:<25}: {elapsed:>5.2f}s, MSE: {results['matrix'][key]['mse']:>9.1f}")
                except subprocess.CalledProcessError as e:
                    print(f"Error running {key}: {e.stderr}")

    return results

if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Usage: python3 tests/run_benchmarks.py <faac_bin_path> <precision_name> <output_json>")
        sys.exit(1)

    faac_bin = sys.argv[1]
    precision = sys.argv[2]
    out_json = sys.argv[3]

    data = run_benchmark(faac_bin, precision)
    with open(out_json, "w") as f:
        json.dump(data, f, indent=2)
