import json
import sys

def compare(base_file, opt_file):
    with open(base_file, "r") as f:
        base = json.load(f)
    with open(opt_file, "r") as f:
        opt = json.load(f)

    print(f"{'Test Case':<25} | {'Base (s)':<8} | {'Opt (s)':<8} | {'Spdup':<6} | {'MSE Chg'}")
    print("-" * 75)

    base_m = base["matrix"]
    opt_m = opt["matrix"]

    for k in sorted(base_m.keys()):
        if k in opt_m:
            b, o = base_m[k], opt_m[k]
            speedup = b["time"] / o["time"] if o["time"] > 0 else 0
            mse_chg = ((o["mse"] / b["mse"]) - 1) * 100 if b["mse"] > 0 else 0

            print(f"{k:<25} | {b['time']:>8.2f} | {o['time']:>8.2f} | {speedup:>5.2f}x | {mse_chg:>+7.1f}%")

    # Grouped Summary
    total_base = sum(f["time"] for f in base_m.values())
    total_opt = sum(f["time"] for f in opt_m.values())
    print("-" * 75)
    print(f"{'TOTAL':<25} | {total_base:>8.2f} | {total_opt:>8.2f} | {(total_base/total_opt):>5.2f}x |")

    improvement = (1 - total_opt/total_base)*100 if total_base > 0 else 0
    print(f"\nOverall CPU Reduction: {improvement:.1f}%")

    base_size = base.get("binary_size", 0)
    opt_size = opt.get("binary_size", 0)
    if base_size > 0:
        print(f"Binary Size Change: {((opt_size/base_size)-1)*100:+.2f}%")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 benchmarks/compare_results.py <baseline.json> <optimized.json>")
        sys.exit(1)
    compare(sys.argv[1], sys.argv[2])
