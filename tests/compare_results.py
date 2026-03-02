import json
import sys

def compare(base_file, opt_file):
    with open(base_file, "r") as f:
        base = json.load(f)
    with open(opt_file, "r") as f:
        opt = json.load(f)

    print("=" * 115)
    print(f"{'Test Case':<25} | {'Spdup':<6} | {'MSE Chg':<8} | {'MOS Chg':<8} | {'Health Status / Risk'}")
    print("-" * 115)

    base_m = base["matrix"]
    opt_m = opt["matrix"]

    for k in sorted(base_m.keys()):
        if k in opt_m:
            b, o = base_m[k], opt_m[k]
            speedup = b["time"] / o["time"] if o["time"] > 0 else 0
            mse_chg = ((o["mse"] / b["mse"]) - 1) * 100 if b["mse"] > 0 else 0

            b_mos = b.get("mos")
            o_mos = o.get("mos")
            mos_chg = (o_mos - b_mos) if (o_mos is not None and b_mos is not None) else None

            risks = []
            if o["holes"] > 20: risks.append("Metallic Risk")
            if o["ms_ratio"] > 80: risks.append("Phasing Risk")
            if o_mos and o_mos < 3.0: risks.append("Poor Perceptual")

            status = "PASS" if not risks else f"WARN ({', '.join(risks)})"
            if speedup < 0.9: status = "SLOW " + status

            mos_str = f"{mos_chg:>+8.2f}" if mos_chg is not None else "   N/A  "

            print(f"{k:<25} | {speedup:>5.2f}x | {mse_chg:>+7.1f}% | {mos_str} | {status}")

    # Overall Summary
    total_base = sum(f["time"] for f in base_m.values())
    total_opt = sum(f["time"] for f in opt_m.values())
    print("-" * 115)

    improvement = (1 - total_opt/total_base)*100 if total_base > 0 else 0
    print(f"OVERALL CPU REDUCTION: {improvement:.1f}%")

    base_size = base.get("binary_size", 0)
    opt_size = opt.get("binary_size", 0)
    if base_size > 0:
        print(f"BINARY SIZE CHANGE: {((opt_size/base_size)-1)*100:+.2f}%")
    print("=" * 115)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 tests/compare_results.py <baseline.json> <optimized.json>")
        sys.exit(1)
    compare(sys.argv[1], sys.argv[2])
