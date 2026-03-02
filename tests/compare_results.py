import json
import sys

def compare(base_file, opt_file):
    with open(base_file, "r") as f:
        base = json.load(f)
    with open(opt_file, "r") as f:
        opt = json.load(f)

    print("### Perceptual & Compression Efficiency")
    print("| Test Case | MOS Delta (Perceptual) | AAC Size Chg | Status |")
    print("| :--- | :---: | :---: | :---: |")

    base_m = base["matrix"]
    opt_m = opt["matrix"]

    for k in sorted(base_m.keys()):
        if k in opt_m:
            b, o = base_m[k], opt_m[k]
            b_mos = b.get("mos")
            o_mos = o.get("mos")
            mos_delta = (o_mos - b_mos) if (o_mos is not None and b_mos is not None) else None

            b_size = b.get("aac_size", 0)
            o_size = o.get("aac_size", 0)
            size_chg = ((o_size / b_size) - 1) * 100 if b_size > 0 else 0

            status = "✅ PASS"
            if mos_delta is not None and mos_delta < -0.25: status = "⚠️ WARN"
            if mos_delta is not None and mos_delta < -0.6: status = "❌ FAIL"

            mos_str = f"{mos_delta:>+5.2f}" if mos_delta is not None else "N/A"
            print(f"| {k:<20} | {mos_str} | {size_chg:>+6.1f}% | {status} |")

    print("\n### System Performance & Footprint")
    base_tp = base.get("throughput", {})
    opt_tp = opt.get("throughput", {})

    total_base_t = sum(base_tp.values())
    total_opt_t = sum(opt_tp.values())

    if total_opt_t > 0:
        speedup = total_base_t / total_opt_t
        reduction = (1 - 1/speedup) * 100
        print(f"- **CPU Usage Reduction:** {reduction:.1f}% ({speedup:.2f}x speedup)")

    base_lib = base.get("lib_size", 0)
    opt_lib = opt.get("lib_size", 0)
    if base_lib > 0:
        lib_chg = ((opt_lib/base_lib)-1)*100
        print(f"- **Library Binary Size:** {lib_chg:+.2f}% ({base_lib} -> {opt_lib} bytes)")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(1)
    compare(sys.argv[1], sys.argv[2])
