import os
import subprocess
import json
import re

def run_experiment(pe_divisor, convergence, name):
    print(f"--- Running Experiment: {name} (Divisor={pe_divisor}, Convergence={convergence}) ---")

    # Update source files
    with open("libfaac/frame.c", "r") as f:
        content = f.read()
    content = re.sub(r"pe_target = \(faac_real\)desbits / [\d\.]+;", f"pe_target = (faac_real)desbits / {pe_divisor:.4f};", content)
    content = re.sub(r"fix = \(fix - 1\.0\) \* [\d\.]+ \+ 1\.0; /\* PE loop convergence \*/", f"fix = (fix - 1.0) * {convergence:.4f} + 1.0; /* PE loop convergence */", content)
    with open("libfaac/frame.c", "w") as f:
        f.write(content)

    subprocess.run(["ninja", "-C", "build"], check=True, capture_output=True)

    output_json = f"results_{name}.json"
    cmd = [
        "python3", "/opt/faac-benchmark/run_benchmark.py",
        "./build/frontend/faac", "./build/libfaac/libfaac.so",
        name, output_json,
        "--coverage", "30"
    ]

    try:
        subprocess.run(cmd, check=True)
    except subprocess.CalledProcessError:
        return None

    try:
        with open("baseline.json", "r") as f:
            baseline = json.load(f)
        with open(output_json, "r") as f:
            candidate = json.load(f)

        base_m = baseline.get("matrix", {})
        cand_m = candidate.get("matrix", {})
        mos_deltas = []
        regressions = 0
        wins = 0
        for k, o in cand_m.items():
            b = base_m.get(k)
            if b and o.get("mos") is not None and b.get("mos") is not None:
                delta = o["mos"] - b["mos"]
                mos_deltas.append(delta)
                if delta < -0.05: regressions += 1
                if delta > 0.05: wins += 1
        avg_mos_delta = sum(mos_deltas) / len(mos_deltas) if mos_deltas else 0
        return {"avg_mos_delta": avg_mos_delta, "regressions": regressions, "wins": wins}
    except Exception as e:
        print(f"Analysis failed: {e}")
        return None

def main():
    divisors = [1.10, 1.15, 1.20, 1.25, 1.30]
    convergences = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6]
    all_summary = []

    # Ensure baseline exists with MOS
    if not os.path.exists("baseline.json"):
        print("Creating baseline with MOS...")
        subprocess.run([
            "python3", "/opt/faac-benchmark/run_benchmark.py",
            "./build/frontend/faac", "./build/libfaac/libfaac.so",
            "baseline", "baseline.json", "--coverage", "30"
        ], check=True)

    for d in divisors:
        for c in convergences:
            name = f"d{d:.2f}_c{c:.2f}".replace(".", "")
            res = run_experiment(d, c, name)
            if res:
                res.update({"pe_divisor": d, "convergence": c})
                print(f"RESULT: {res}")
                all_summary.append(res)
                with open("tuning_summary.json", "w") as f:
                    json.dump(all_summary, f, indent=2)

if __name__ == "__main__":
    main()
