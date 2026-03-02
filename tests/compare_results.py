import json
import sys

def compare(base_file, opt_file):
    with open(base_file, "r") as f:
        base = json.load(f)
    with open(opt_file, "r") as f:
        opt = json.load(f)

    print("=" * 105)
    print(f"{'Test Case':<25} | {'MOS Delta':<10} | {'MSE Chg':<8} | {'Status'}")
    print("-" * 105)

    base_m = base["matrix"]
    opt_m = opt["matrix"]

    for k in sorted(base_m.keys()):
        if k in opt_m:
            b, o = base_m[k], opt_m[k]

            b_mos = b.get("mos")
            o_mos = o.get("mos")
            mos_delta = (o_mos - b_mos) if (o_mos is not None and b_mos is not None) else None

            b_mse = b.get("mse", 0)
            o_mse = o.get("mse", 0)
            mse_chg = ((o_mse / b_mse) - 1) * 100 if b_mse > 0 else 0

            status = "PASS"
            if mos_delta is not None and mos_delta < -0.2: status = "WARN (PERC)"
            if mos_delta is not None and mos_delta < -0.5: status = "FAIL (QUAL)"

            mos_str = f"{mos_delta:>+10.2f}" if mos_delta is not None else "   N/A    "
            mse_str = f"{mse_chg:>+7.1f}%" if (b_mse > 0 or o_mse > 0) else "  N/A   "

            print(f"{k:<25} | {mos_str} | {mse_str} | {status}")

    print("-" * 105)
    print(f"{'Throughput Comparison':<25} | {'Base (s)':<8} | {'Opt (s)':<8} | {'Spdup'}")
    print("-" * 105)

    base_tp = base.get("throughput", {})
    opt_tp = opt.get("throughput", {})

    total_base_t = 0
    total_opt_t = 0

    for k in sorted(base_tp.keys()):
        if k in opt_tp:
            bt, ot = base_tp[k], opt_tp[k]
            total_base_t += bt
            total_opt_t += ot
            print(f"{k:<25} | {bt:>8.2f} | {ot:>8.2f} | {(bt/ot):>5.2f}x")

    print("-" * 105)
    improvement = (1 - total_opt_t/total_base_t)*100 if total_base_t > 0 else 0
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
