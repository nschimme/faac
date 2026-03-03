"""
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2025 Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
"""

import json
import sys

def compare(base_file, cand_file):
    try:
        with open(base_file, "r") as f:
            base = json.load(f)
    except:
        base = {}

    try:
        with open(cand_file, "r") as f:
            cand = json.load(f)
    except:
        print("Error: Could not load candidate results.")
        sys.exit(1)

    has_regression = False
    missing_data = False

    # 1. Perceptual Matrix (if present)
    base_m = base.get("matrix", {})
    cand_m = cand.get("matrix", {})

    if cand_m:
        print("### Perceptual Matrix (ViSQOL MOS)")

        failures = []
        all_cases = []
        total_mos_delta = 0
        mos_count = 0

        for k in sorted(cand_m.keys()):
            o = cand_m[k]
            b = base_m.get(k, {})

            filename = o.get("filename", k)
            scenario = o.get("scenario", "")
            display_name = f"{scenario}: {filename}"

            o_mos = o.get("mos")
            b_mos = b.get("mos")
            thresh = o.get("thresh", 1.0)

            o_size = o.get("size")
            b_size = b.get("size")

            if o_size is not None and b_size is not None:
                size_chg_val = (o_size - b_size) / b_size * 100
                size_chg = f"{size_chg_val:+.2f}%"
            else:
                size_chg = "N/A"
                if o_size is None: missing_data = True

            status = "✅"
            if o_mos is not None:
                if b_mos is not None:
                    delta = o_mos - b_mos
                    total_mos_delta += delta
                    mos_count += 1

                if o_mos < (thresh - 0.5):
                    status = "🤮" # Awful
                elif o_mos < thresh:
                    status = "📉" # Bad/Poor

                if b_mos is not None and (o_mos - b_mos) < -0.1:
                    status = "❌" # Regression
                    has_regression = True
            else:
                status = "❓"
                missing_data = True

            mos_str = f"{o_mos:.2f}" if o_mos is not None else "N/A"
            b_mos_str = f"{b_mos:.2f}" if b_mos is not None else "N/A"
            delta_mos = f"{(o_mos - b_mos):+.2f}" if (o_mos is not None and b_mos is not None) else "N/A"

            case_info = f"| {display_name} | {status} | {mos_str} ({b_mos_str}) | {delta_mos} | {size_chg} |"
            all_cases.append(case_info)
            if status in ["🤮", "📉", "❌"]:
                failures.append(case_info)

        if failures:
            print("\n#### ⚠️ Detailed Failures/Warnings")
            print("| Test Case | Status | MOS (Base) | Delta | Size Δ |")
            print("| :--- | :---: | :---: | :---: | :---: |")
            for f in failures:
                print(f)

        avg_mos_delta = total_mos_delta / mos_count if mos_count > 0 else 0
        print(f"\n- **Average MOS Improvement:** {avg_mos_delta:+.3f}")
        if avg_mos_delta > 0.05:
            print("  - 🚀 Significant perceptual improvement detected!")
        elif avg_mos_delta < -0.05:
            print("  - ⚠️ Perceptual quality has decreased overall.")

        print("\n<details><summary><b>Click to expand full matrix</b></summary>\n")
        print("| Test Case | Status | MOS (Base) | Delta | Size Δ |")
        print("| :--- | :---: | :---: | :---: | :---: |")
        for c in all_cases:
            print(c)
        print("\n</details>")
    else:
        missing_data = True

    # 2. System Efficiency Summary
    print("\n### 🚀 System Efficiency Summary")
    base_tp = base.get("throughput", {})
    cand_tp = cand.get("throughput", {})

    total_base_t = sum(base_tp.values())
    total_cand_t = sum(cand_tp.values())

    if total_cand_t > 0 and total_base_t > 0:
        speedup = total_base_t / total_cand_t
        reduction = (1 - 1/speedup) * 100
        print(f"- **Throughput Improvement:** {reduction:+.1f}% ({total_base_t:.3f}s -> {total_cand_t:.3f}s)")
    elif total_cand_t > 0:
         print(f"- **Throughput (New):** {total_cand_t:.3f}s")
    else:
        missing_data = True

    base_lib = base.get("lib_size", 0)
    cand_lib = cand.get("lib_size", 0)
    if cand_lib > 0:
        if base_lib > 0:
            lib_chg = ((cand_lib/base_lib)-1)*100
            print(f"- **Binary Size Change:** {lib_chg:+.2f}% ({base_lib} -> {cand_lib} bytes)")
        else:
            print(f"- **Binary Size:** {cand_lib} bytes")
    else:
        missing_data = True

    if has_regression:
        print("\n❌ **Job failed: Quality regression detected.**")
        sys.exit(1)
    if missing_data:
        print("\n❌ **Job failed: Missing benchmark data (MOS, CPU, or Size).**")
        sys.exit(1)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 tests/compare_results.py <base_json> <opt_json>")
        sys.exit(1)
    compare(sys.argv[1], sys.argv[2])
