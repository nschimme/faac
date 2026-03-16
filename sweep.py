import subprocess
import json
import os
import sys

thresholds = [1.4, 2.0, 2.5, 3.0]
results = {}

for th in thresholds:
    print(f"--- Testing threshold {th} ---")
    # Update coder.h
    with open("libfaac/coder.h", "r") as f:
        lines = f.readlines()
    with open("libfaac/coder.h", "w") as f:
        for line in lines:
            if "#define DEF_TNS_GAIN_THRESH" in line:
                f.write(f"#define DEF_TNS_GAIN_THRESH {th}\n")
            else:
                f.write(line)

    # Build
    subprocess.run(["meson", "compile", "-C", "builddir"], check=True, capture_output=True)

    # Run benchmark for voip
    out_file = f"results/sweep_{th}.json"
    subprocess.run([
        "python3", "/opt/faac-benchmark/run_benchmark.py",
        "./builddir/frontend/faac", "./builddir/libfaac/libfaac.so",
        f"sweep_{th}", out_file,
        "--scenarios", "voip",
        "--include-tests", "*ECHO*,*CHOP*"
    ], check=True)

    with open(out_file, "r") as f:
        data = json.load(f)

    # Extract stats
    mos_sum = 0
    count = 0
    for k, v in data["matrix"].items():
        if v["mos"] is not None:
            mos_sum += v["mos"]
            count += 1

    avg_mos = mos_sum / count if count > 0 else 0
    tp = data["throughput"]["overall"]

    results[th] = {"avg_mos": avg_mos, "throughput": tp}
    print(f"Results for {th}: Avg MOS={avg_mos:.4f}, TP={tp:.2f}")

print("\n--- Sweep Summary ---")
print("Threshold | Avg MOS | Throughput")
for th in thresholds:
    res = results[th]
    print(f"{th:9} | {res['avg_mos']:.4f} | {res['throughput']:.2f}")
