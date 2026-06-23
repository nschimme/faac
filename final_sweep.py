import subprocess
import os
import json
import csv

ENCODER = "./build/frontend/faac"
LIB_PATH = "./build/libfaac/libfaac.so"
BENCHMARK_PATH = "/opt/faac-benchmark/run_benchmark.py"
RESULTS_DIR = "./final_sweep"

def run_config(name, q, is_floor, coll_thr, scenario):
    env = os.environ.copy()
    env["FAAC_STEREO_IS_FLOOR"] = str(is_floor)
    env["FAAC_STEREO_MS_COLLAPSE_THR"] = str(coll_thr)

    output_json = os.path.join(RESULTS_DIR, f"{name}.json")

    # Use --include-tests instead of --subset (which is not supported by run_benchmark.py)
    cmd = [
        "python3", BENCHMARK_PATH,
        ENCODER, LIB_PATH, name, output_json,
        "--scenarios", scenario,
        "--include-tests", "mof.wav,slaves.wav"
    ]

    print(f"Running {name} (f={is_floor}, c={coll_thr}) on {scenario}...")
    subprocess.run(cmd, env=env, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    with open(output_json, "r") as f:
        data = json.load(f)

    matrix = data.get("matrix", {})
    mos_list = [v["mos"] for v in matrix.values() if "mos" in v]
    err_list = [v["ic_err"] for v in matrix.values() if "ic_err" in v]

    return {
        "mos": sum(mos_list) / len(mos_list) if mos_list else 0,
        "error": sum(err_list) / len(err_list) if err_list else 0
    }

def main():
    os.makedirs(RESULTS_DIR, exist_ok=True)
    results = []

    # 1. Reference: Signaling fix + Legacy Tuning (f=5500, c=1.0)
    ref_64 = run_config("ref_64", 1.0, 5500, 1.0, "music_low")
    ref_128 = run_config("ref_128", 4.0, 5500, 1.0, "music_std")

    print(f"REF 64k: MOS={ref_64['mos']:.4f}, Err={ref_64['error']:.4f}")
    print(f"REF 128k: MOS={ref_128['mos']:.4f}, Err={ref_128['error']:.4f}")

    # 2. Sweep 64k (q=1.0)
    for c in [1.0, 5.0, 10.0, 20.0]:
        res = run_config(f"64k_f5500_c{c}", 1.0, 5500, c, "music_low")
        results.append({"q": 1.0, "floor": 5500, "coll": c, **res})

    # 3. Sweep 128k (q=4.0)
    for f in [5500, 8000, 10000, 12000, 15000]:
        for c in [1.0, 10.0, 30.0, 50.0]:
            res = run_config(f"128k_f{f}_c{c}", 4.0, f, c, "music_std")
            results.append({"q": 4.0, "floor": f, "coll": c, **res})

    with open("final_sweep_results.csv", "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["q", "floor", "coll", "mos", "error"])
        writer.writeheader()
        writer.writerows(results)

if __name__ == "__main__":
    main()
