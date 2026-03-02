import json
import sys

def compare(base_file, opt_file):
    with open(base_file, "r") as f:
        base = json.load(f)
    with open(opt_file, "r") as f:
        opt = json.load(f)

    print("=" * 105)
    print(f"{'Test Case':<25} | {'Spdup':<6} | {'ODG Delta':<10} | {'MSE Chg':<8} | {'Status'}")
    print("-" * 105)

    base_m = base["matrix"]
    opt_m = opt["matrix"]

    for k in sorted(base_m.keys()):
        if k in opt_m:
            b, o = base_m[k], opt_m[k]
            speedup = b["time"] / o["time"] if o["time"] > 0 else 0

            b_odg = b.get("odg")
            o_odg = o.get("odg")
            odg_delta = (o_odg - b_odg) if (o_odg is not None and b_odg is not None) else None

            mse_chg = ((o["mse"] / b["mse"]) - 1) * 100 if b["mse"] > 0 else 0

            # Status:
            # - PASS: Speedup > 0.9, ODG delta > -0.2 (perceptually equivalent)
            # - FAIL: ODG delta < -0.5 (significant degradation)
            status = "PASS"
            if odg_delta is not None and odg_delta < -0.2: status = "WARN (PERC)"
            if odg_delta is not None and odg_delta < -0.5: status = "FAIL (QUAL)"
            if speedup < 0.9: status = "SLOW " + status

            odg_str = f"{odg_delta:>+10.2f}" if odg_delta is not None else "   N/A    "

            print(f"{k:<25} | {speedup:>5.2f}x | {odg_str} | {mse_chg:>+7.1f}% | {status}")

    # Overall Summary
    total_base = sum(f["time"] for f in base_m.values())
    total_opt = sum(f["time"] for f in opt_m.values())
    print("-" * 105)

    improvement = (1 - total_opt/total_base)*100 if total_base > 0 else 0
    print(f"OVERALL CPU REDUCTION: {improvement:.1f}%")

    base_size = base.get("binary_size", 0)
    opt_size = opt.get("binary_size", 0)
    if base_size > 0:
        print(f"BINARY SIZE CHANGE: {((opt_size/base_size)-1)*100:+.2f}%")
    print("=" * 105)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 tests/compare_results.py <baseline.json> <optimized.json>")
        sys.exit(1)
    compare(sys.argv[1], sys.argv[2])
