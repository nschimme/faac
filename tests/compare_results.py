import json
import sys

def compare(base_file, opt_file):
    with open(base_file, "r") as f:
        base = json.load(f)
    with open(opt_file, "r") as f:
        opt = json.load(f)

    # 1. Perceptual Matrix (if present)
    base_m = base.get("matrix", {})
    opt_m = opt.get("matrix", {})

    if opt_m:
        print("### Perceptual Matrix (ViSQOL MOS)")

        failures = []
        all_cases = []

        for k in sorted(opt_m.keys()):
            o = opt_m[k]
            b = base_m.get(k, {})

            o_mos = o.get("mos")
            b_mos = b.get("mos")
            thresh = o.get("thresh", 1.0)

            o_size = o.get("size", 1)
            b_size = b.get("size", 1)
            size_chg = (o_size - b_size) / b_size * 100 if b_size else 0

            status = "✅"
            if o_mos is not None:
                if o_mos < (thresh - 0.5):
                    status = "🤮" # Awful
                elif o_mos < thresh:
                    status = "📉" # Bad/Poor

                # Check for regressions
                if b_mos is not None and (o_mos - b_mos) < -0.1:
                    status = "❌" # Regression
            else:
                status = "❓"

            mos_str = f"{o_mos:.2f}" if o_mos is not None else "N/A"
            b_mos_str = f"{b_mos:.2f}" if b_mos is not None else "N/A"
            delta_mos = f"{(o_mos - b_mos):+.2f}" if (o_mos is not None and b_mos is not None) else "N/A"

            case_info = f"| {k} | {status} | {mos_str} ({b_mos_str}) | {delta_mos} | {size_chg:+.2f}% |"
            all_cases.append(case_info)
            if status in ["🤮", "📉", "❌"]:
                failures.append(case_info)

        if failures:
            print("\n#### ⚠️ Detailed Failures/Warnings")
            print("| Test Case | Status | MOS (Base) | Delta | Size Δ |")
            print("| :--- | :---: | :---: | :---: | :---: |")
            for f in failures:
                print(f)

        print("\n<details><summary><b>Click to expand full matrix</b></summary>\n")
        print("| Test Case | Status | MOS (Base) | Delta | Size Δ |")
        print("| :--- | :---: | :---: | :---: | :---: |")
        for c in all_cases:
            print(c)
        print("\n</details>")

    # 2. System Efficiency Summary
    print("\n### 🚀 System Efficiency Summary")
    base_tp = base.get("throughput", {})
    opt_tp = opt.get("throughput", {})

    total_base_t = sum(base_tp.values())
    total_opt_t = sum(opt_tp.values())

    if total_opt_t > 0 and total_base_t > 0:
        speedup = total_base_t / total_opt_t
        reduction = (1 - 1/speedup) * 100
        print(f"- **Throughput Improvement:** {reduction:+.1f}% ({total_base_t:.3f}s -> {total_opt_t:.3f}s)")
    elif total_opt_t > 0:
         print(f"- **Throughput (New):** {total_opt_t:.3f}s")

    base_lib = base.get("lib_size", 0)
    opt_lib = opt.get("lib_size", 0)
    if base_lib > 0:
        lib_chg = ((opt_lib/base_lib)-1)*100
        print(f"- **Binary Size Change:** {lib_chg:+.2f}% ({base_lib} -> {opt_lib} bytes)")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(1)
    compare(sys.argv[1], sys.argv[2])
