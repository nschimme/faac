import os
import subprocess
import time
import hashlib
import sys

TEST_FILES = ["sine.wav", "sweep.wav", "noise.wav", "silence.wav"]
DATA_DIR = "benchmarks/data"
OUTPUT_DIR = "benchmarks/output"

def get_md5(filename):
    hash_md5 = hashlib.md5()
    with open(filename, "rb") as f:
        for chunk in iter(lambda: f.read(4096), b""):
            hash_md5.update(chunk)
    return hash_md5.hexdigest()

def run_benchmark(faac_path, precision, scalar=True):
    env = os.environ.copy()
    if scalar:
        env["FAAC_NO_SIMD"] = "1"

    os.makedirs(OUTPUT_DIR, exist_ok=True)
    results = []

    print(f"--- Benchmarking {precision} precision (Scalar: {scalar}) ---")
    for test_file in TEST_FILES:
        input_path = os.path.join(DATA_DIR, test_file)
        if not os.path.exists(input_path):
            print(f"Skipping {test_file}, not found.")
            continue

        output_path = os.path.join(OUTPUT_DIR, f"{test_file}_{precision}.aac")

        start_time = time.time()
        cmd = [faac_path, "-o", output_path, input_path]
        try:
            # We use capture_output=True to keep the console clean
            subprocess.run(cmd, env=env, check=True, capture_output=True)
            elapsed = time.time() - start_time
            md5 = get_md5(output_path)
            print(f"{test_file}: {elapsed:.2f}s, MD5: {md5}")
            results.append({"file": test_file, "time": elapsed, "md5": md5})
        except subprocess.CalledProcessError as e:
            print(f"Error running {test_file}: {e.stderr.decode()}")

    return results

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 benchmarks/run_benchmarks.py <faac_bin_path> <precision_name>")
        sys.exit(1)

    faac_bin = sys.argv[1]
    precision = sys.argv[2]

    if not os.path.exists(faac_bin):
        print(f"FAAC binary not found at {faac_bin}.")
        sys.exit(1)

    run_benchmark(faac_bin, precision)
