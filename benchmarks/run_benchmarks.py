import subprocess
import time
import os
import hashlib
import sys

# Default to the 10-minute waves if they exist, otherwise try to find them.
TEST_WAVES = ["sine.wav", "sweep.wav", "noise.wav", "silence.wav"]
WAVE_DIR = "test_waves"

def get_md5(filename):
    hash_md5 = hashlib.md5()
    if not os.path.exists(filename):
        return "MISSING"
    with open(filename, "rb") as f:
        for chunk in iter(lambda: f.read(4096), b""):
            hash_md5.update(chunk)
    return hash_md5.hexdigest()

def run_bench(faac_path, wav_path, env=None):
    if env is None: env = os.environ.copy()
    if not os.path.exists(faac_path):
        print(f"Error: {faac_path} not found.")
        return None, None
    if not os.path.exists(wav_path):
        print(f"Error: {wav_path} not found.")
        return None, None

    cmd = [faac_path, "-o", "out.aac", wav_path, "-v0"]

    start_time = time.time()
    process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
    process.communicate()
    end_time = time.time()

    if process.returncode != 0:
        return None, None

    encoding_time = end_time - start_time
    md5 = get_md5("out.aac")
    if os.path.exists("out.aac"):
        os.remove("out.aac")
    return encoding_time, md5

def main():
    # Attempt to locate the faac executables in standard build dirs
    configs = [
        ("Single SSE2", "build_single/frontend/faac", {}),
        ("Double SSE2", "build_double/frontend/faac", {}),
        ("Double Scalar", "build_double/frontend/faac", {"FAAC_NO_SIMD": "1"}),
    ]

    print("FAAC SSE2 vs Scalar Benchmark")
    print("=============================")

    results = {}
    for config_name, faac_path, env_vars in configs:
        # Go up one level to root if running from benchmarks/
        root_faac_path = os.path.join("..", faac_path)
        if not os.path.exists(root_faac_path):
            root_faac_path = faac_path # Try current dir

        results[config_name] = {}
        print(f"\nConfig: {config_name}")
        for wave_file in TEST_WAVES:
            wav_path = os.path.join("..", WAVE_DIR, wave_file)
            if not os.path.exists(wav_path):
                wav_path = os.path.join(WAVE_DIR, wave_file)

            t, m = run_bench(root_faac_path, wav_path, env={**os.environ, **env_vars})
            if t is not None:
                results[config_name][wave_file] = (t, m)
                print(f"  {wave_file.ljust(12)}: {t:6.2f}s, MD5: {m}")
            else:
                print(f"  {wave_file.ljust(12)}: FAILED")

    print("\nSummary (Encoding Times):")
    header = "Config".ljust(15) + "".join(w.rjust(12) for w in TEST_WAVES)
    print(header)
    for name, _, _ in configs:
        row = name.ljust(15)
        for wave_file in TEST_WAVES:
            if wave_file in results[name]:
                row += f"{results[name][wave_file][0]:12.2f}"
            else:
                row += "         N/A"
        print(row)

    print("\nBit-Exactness Check (Double SSE2 vs Double Scalar):")
    for wave_file in TEST_WAVES:
        if wave_file in results["Double Scalar"] and wave_file in results["Double SSE2"]:
            scalar_md5 = results["Double Scalar"][wave_file][1]
            sse2_md5 = results["Double SSE2"][wave_file][1]
            match = "MATCH" if scalar_md5 == sse2_md5 else "MISMATCH"
            print(f"  {wave_file.ljust(12)}: {match}")
        else:
            print(f"  {wave_file.ljust(12)}: N/A")

if __name__ == "__main__":
    main()
