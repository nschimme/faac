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

def run_peaq(ref_wav, deg_wav):
    """Run gst-peaq and parse ODG score."""
    try:
        # Expected pipeline: gst-launch-1.0 -v peaq name=p reference-file=ref.wav degraded-file=deg.wav ! fakesink
        # We assume the maintainer has installed gst-peaq and we can parse its output
        cmd = ["gst-launch-1.0", "-v", "peaq", f"reference-file={ref_wav}", f"degraded-file={deg_wav}", "!", "fakesink"]
        proc = subprocess.run(cmd, capture_output=True, text=True)
        # Parse ODG from output (example pattern)
        match = re.search(r"ODG:\s+([-.\d]+)", proc.stdout + proc.stderr)
        if match:
            return float(match.group(1))
    except:
        pass
    return None

def run_benchmark(faac_path, precision, scalar=True):
    env = os.environ.copy()
    if scalar:
        env["FAAC_NO_SIMD"] = "1"
    env["FAAC_DUMP_MSE"] = "1"

    os.makedirs(OUTPUT_DIR, exist_ok=True)
    results = {"matrix": {}, "binary_size": os.path.getsize(faac_path) if os.path.exists(faac_path) else 0}

    print(f"--- Matrix Tests {precision} precision (Scalar: {scalar}) ---")

    for rate in RATES:
        for quality in QUALITIES:
            for t in TYPES:
                input_path = os.path.join(DATA_DIR, f"{t}_{rate}.wav")
                if not os.path.exists(input_path):
                    continue

                key = f"{t}_{rate}_q{quality}"
                output_path = os.path.join(OUTPUT_DIR, f"{key}_{precision}.aac")
                deg_wav = output_path.replace(".aac", "_deg.wav")

                start_time = time.time()
                cmd = [faac_path, "-q", str(quality), "-o", output_path, input_path]
                try:
                    proc = subprocess.run(cmd, env=env, check=True, capture_output=True, text=True)
                    elapsed = time.time() - start_time

                    # Qualitative: PEAQ ODG
                    odg = None
                    try:
                        # Decode back to WAV for comparison
                        subprocess.run(["faad", "-o", deg_wav, output_path], capture_output=True)
                        if os.path.exists(deg_wav):
                            odg = run_peaq(input_path, deg_wav)
                    except:
                        pass
                    finally:
                        if os.path.exists(deg_wav): os.remove(deg_wav)

                    # Parse debug metrics if PRINTSTAT was enabled
                    mse_match = re.search(r"MSE: ([\d.]+)", proc.stdout)
                    mse = float(mse_match.group(1)) if mse_match else 0

                    results["matrix"][key] = {
                        "time": elapsed,
                        "md5": get_md5(output_path),
                        "mse": mse,
                        "odg": odg
                    }
                    odg_str = f"ODG: {odg:>+5.2f}" if odg is not None else ""
                    print(f"{key:<25}: {elapsed:>5.2f}s {odg_str}")
                except subprocess.CalledProcessError as e:
                    print(f"Error running {key}: {e.stderr}")

    return results

if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Usage: python3 tests/run_benchmarks.py <faac_bin_path> <precision_name> <output_json>")
        sys.exit(1)
    data = run_benchmark(sys.argv[1], sys.argv[2])
    with open(sys.argv[3], "w") as f:
        json.dump(data, f, indent=2)
