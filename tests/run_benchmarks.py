import os
import subprocess
import time
import sys
import json
import re

EXTERNAL_DATA_DIR = "tests/data/external"
OUTPUT_DIR = "tests/output"

SCENARIOS = {
    "voip": {"mode": "speech", "rate": 16000, "q": 15, "thresh": 2.5},
    "nvr": {"mode": "audio", "rate": 48000, "q": 30, "thresh": 3.0},
    "music_low": {"mode": "audio", "rate": 48000, "q": 60, "thresh": 3.5},
    "music_std": {"mode": "audio", "rate": 48000, "q": 120, "thresh": 4.0},
    "music_high": {"mode": "audio", "rate": 48000, "q": 250, "thresh": 4.3}
}

def get_binary_size(path):
    if os.path.exists(path):
        return os.path.getsize(path)
    return 0

def run_visqol(ref_wav, deg_wav, mode):
    """Run ViSQOL and parse MOS score."""
    try:
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

    # 1. Perceptual Matrix (External Data)
    if run_perceptual:
        for name, cfg in SCENARIOS.items():
            data_subdir = "speech" if cfg["mode"] == "speech" else "audio"
            data_dir = os.path.join(EXTERNAL_DATA_DIR, data_subdir)
            if not os.path.exists(data_dir): continue

            samples = [f for f in os.listdir(data_dir) if f.endswith(".wav")]

            for sample in samples[:3]: # Use top 3 samples per scenario
                input_path = os.path.join(data_dir, sample)
                key = f"{name}_{sample}"
                output_path = os.path.join(OUTPUT_DIR, f"{key}_{precision}.aac")
                deg_wav = output_path.replace(".aac", "_deg.wav")

                try:
                    subprocess.run([faac_path, "-q", str(cfg["q"]), "-o", output_path, input_path],
                                   env=env, check=True, capture_output=True)

                    mos = None
                    aac_size = os.path.getsize(output_path)

                    try:
                        # Use 'faad' for decoding
                        subprocess.run(["faad", "-o", deg_wav, output_path], capture_output=True)
                        if os.path.exists(deg_wav):
                            mos = run_visqol(input_path, deg_wav, cfg["mode"])
                    except:
                        pass
                    finally:
                        if os.path.exists(deg_wav): os.remove(deg_wav)

                    results["matrix"][key] = {
                        "mos": mos,
                        "size": aac_size,
                        "thresh": cfg["thresh"],
                        "scenario": name
                    }
                except:
                    pass

    # 2. Throughput using real samples
    speech_dir = os.path.join(EXTERNAL_DATA_DIR, "speech")
    if os.path.exists(speech_dir):
        samples = [f for f in os.listdir(speech_dir) if f.endswith(".wav")]
        if samples:
            input_path = os.path.join(speech_dir, samples[0])
            output_path = os.path.join(OUTPUT_DIR, f"throughput_{precision}.aac")
            start_time = time.time()
            try:
                # Loop a few times to get more stable measurement if it's very fast
                for _ in range(5):
                    subprocess.run([faac_path, "-o", output_path, input_path], env=env, check=True, capture_output=True)
                results["throughput"]["overall"] = (time.time() - start_time) / 5
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
