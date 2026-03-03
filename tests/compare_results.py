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
import os
import re

def analyze_pair(base_file, cand_file):
    print(f"Analyzing pair:\n  Base: {base_file}\n  Candidate: {cand_file}")
    try:
        with open(base_file, "r") as f:
            base = json.load(f)
    except:
        base = {}

    try:
        with open(cand_file, "r") as f:
            cand = json.load(f)
    except:
        print(f"  Error: Could not load candidate file {cand_file}")
        return None

    suite_results = {
        "has_regression": False,
        "missing_data": False,
        "mos_delta_sum": 0,
        "mos_count": 0,
        "tp_reduction": 0,
        "lib_size_chg": 0,
        "regressions": [],
        "new_wins": [],
        "significant_wins": [],
        "opportunities": [],
        "bit_exact_count": 0,
        "total_cases": 0,
        "all_cases": []
    }

    base_m = base.get("matrix", {})
    cand_m = cand.get("matrix", {})

    if cand_m:
        suite_results["total_cases"] = len(cand_m)
        print(f"  Found {len(cand_m)} test cases.")
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

            o_md5 = o.get("md5", "")
            b_md5 = b.get("md5", "")

            if o_md5 and b_md5 and o_md5 == b_md5:
                suite_results["bit_exact_count"] += 1

            size_chg = "N/A"
            if o_size is not None and b_size is not None:
                size_chg_val = (o_size - b_size) / b_size * 100
                size_chg = f"{size_chg_val:+.2f}%"
            elif o_size is None:
                suite_results["missing_data"] = True

            status = "✅"
            delta = 0
            if o_mos is not None:
                if b_mos is not None:
                    delta = o_mos - b_mos
                    suite_results["mos_delta_sum"] += delta
                    suite_results["mos_count"] += 1

                if o_mos < (thresh - 0.5):
                    status = "🤮" # Awful
                elif o_mos < thresh:
                    status = "📉" # Bad/Poor

                if b_mos is not None:
                    if (o_mos - b_mos) < -0.1:
                        status = "❌" # Regression
                        suite_results["has_regression"] = True
                    elif (o_mos - b_mos) > 0.1:
                        status = "🌟" # Significant Win

                # Check for New Win (Baseline failed, Candidate passed)
                if b_mos is not None and b_mos < thresh and o_mos >= thresh:
                    suite_results["new_wins"].append({
                        "display_name": display_name,
                        "mos": o_mos,
                        "b_mos": b_mos,
                        "delta": delta
                    })

            else:
                status = "❓"
                suite_results["missing_data"] = True

            mos_str = f"{o_mos:.2f}" if o_mos is not None else "N/A"
            b_mos_str = f"{b_mos:.2f}" if b_mos is not None else "N/A"
            delta_mos = f"{(o_mos - b_mos):+.2f}" if (o_mos is not None and b_mos is not None) else "N/A"

            case_data = {
                "display_name": display_name,
                "status": status,
                "mos": o_mos,
                "b_mos": b_mos,
                "delta": delta,
                "size_chg": size_chg,
                "line": f"| {display_name} | {status} | {mos_str} ({b_mos_str}) | {delta_mos} | {size_chg} |"
            }

            suite_results["all_cases"].append(case_data)
            if status == "❌":
                suite_results["regressions"].append(case_data)
            elif status == "🌟":
                suite_results["significant_wins"].append(case_data)
            elif status in ["🤮", "📉"]:
                suite_results["opportunities"].append(case_data)
    else:
        suite_results["missing_data"] = True

    # Sort regressions by delta (worst first)
    suite_results["regressions"].sort(key=lambda x: x["delta"])

    # Sort opportunities by absolute MOS (lowest quality first)
    suite_results["opportunities"].sort(key=lambda x: x["mos"] if x["mos"] is not None else 6.0)

    # Throughput
    base_tp = base.get("throughput", {})
    cand_tp = cand.get("throughput", {})
    total_base_t = sum(base_tp.values())
    total_cand_t = sum(cand_tp.values())
    if total_cand_t > 0 and total_base_t > 0:
        suite_results["tp_reduction"] = (1 - total_cand_t / total_base_t) * 100
    else:
        suite_results["missing_data"] = True

    # Binary Size
    base_lib = base.get("lib_size", 0)
    cand_lib = cand.get("lib_size", 0)
    if cand_lib > 0 and base_lib > 0:
        suite_results["lib_size_chg"] = ((cand_lib/base_lib)-1)*100
    else:
        suite_results["missing_data"] = True

    return suite_results

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 tests/compare_results.py <results_dir>")
        sys.exit(1)

    results_dir = sys.argv[1]
    print(f"Scanning {results_dir} for benchmark results...")
    files = os.listdir(results_dir)

    suites = {}
    for f in files:
        if f.endswith("_cand.json"):
            suite_name = f[:-10]
            base_f = suite_name + "_base.json"
            if base_f in files:
                suites[suite_name] = (os.path.join(results_dir, base_f), os.path.join(results_dir, f))

    if not suites:
        print("No result pairs found in directory.")
        sys.exit(1)

    all_suite_data = {}
    overall_regression = False
    overall_missing = False
    total_mos_delta = 0
    total_mos_count = 0
    total_tp_reduction = 0
    total_lib_chg = 0

    total_regressions = 0
    total_new_wins = 0
    total_significant_wins = 0
    total_bit_exact = 0
    total_cases_all = 0

    for name, (base, cand) in sorted(suites.items()):
        data = analyze_pair(base, cand)
        if data:
            all_suite_data[name] = data
            if data["has_regression"]: overall_regression = True
            if data["missing_data"]: overall_missing = True
            total_mos_delta += data["mos_delta_sum"]
            total_mos_count += data["mos_count"]
            total_tp_reduction += data["tp_reduction"]
            total_lib_chg += data["lib_size_chg"]

            total_regressions += len(data["regressions"])
            total_new_wins += len(data["new_wins"])
            total_significant_wins += len(data["significant_wins"])
            total_bit_exact += data["bit_exact_count"]
            total_cases_all += data["total_cases"]

    avg_mos_delta = total_mos_delta / total_mos_count if total_mos_count > 0 else 0
    avg_tp_reduction = total_tp_reduction / len(all_suite_data) if all_suite_data else 0
    avg_lib_chg = total_lib_chg / len(all_suite_data) if all_suite_data else 0
    bit_exact_percent = (total_bit_exact / total_cases_all * 100) if total_cases_all > 0 else 0

    # Output report to stdout (will be captured by GHA)
    report = []
    if overall_regression:
        report.append("## ❌ Quality Regression Detected")
    elif total_new_wins > 0 or total_significant_wins > 0 or avg_mos_delta > 0.01 or avg_tp_reduction > 5:
        report.append("## 🚀 Perceptual & Efficiency Improvement")
    else:
        report.append("## 📊 Benchmark Summary")

    report.append(f"**High-level Summary:**")
    report.append(f"- Regressions: {total_regressions}")
    report.append(f"- New Wins (Passed threshold): {total_new_wins}")
    report.append(f"- Significant MOS Wins (Δ > 0.1): {total_significant_wins}")
    report.append(f"- Bitstream Consistency: {bit_exact_percent:.1f}% ({total_bit_exact}/{total_cases_all} identical)")
    report.append(f"- Avg MOS Delta: {avg_mos_delta:+.3f}")
    report.append(f"- Avg Throughput Improvement: {avg_tp_reduction:+.1f}%")
    report.append(f"- Avg Binary Size Change: {avg_lib_chg:+.2f}%")

    report.append("\n### 🛠️ Test Suites (Arch/Precision)")

    # Sort suites: regressions first
    sorted_suite_names = sorted(all_suite_data.keys(), key=lambda n: (not all_suite_data[n]["has_regression"], n))

    for name in sorted_suite_names:
        data = all_suite_data[name]
        status_icon = "❌" if data["has_regression"] else "✅"
        if data["missing_data"]: status_icon = "⚠️"

        avg_mos = data["mos_delta_sum"] / data["mos_count"] if data["mos_count"] > 0 else 0
        suite_bit_exact_percent = (data["bit_exact_count"] / data["total_cases"] * 100) if data["total_cases"] > 0 else 0

        report.append(f"\n#### {status_icon} {name}")
        report.append(f"- MOS Δ: {avg_mos:+.3f}, TP Δ: {data['tp_reduction']:+.1f}%, Size Δ: {data['lib_size_chg']:+.2f}%")
        report.append(f"- Bitstream Consistency: {suite_bit_exact_percent:.1f}%")

        if data["regressions"]:
            report.append("<details open><summary><b>❌ Regressions ({})</b></summary>\n".format(len(data["regressions"])))
            report.append("| Test Case | Status | MOS (Base) | Delta | Size Δ |")
            report.append("| :--- | :---: | :---: | :---: | :---: |")
            for r in data["regressions"]: report.append(r["line"])
            report.append("\n</details>")

        if data["new_wins"]:
            report.append("<details><summary><b>🆕 New Wins ({})</b></summary>\n".format(len(data["new_wins"])))
            report.append("| Test Case | MOS (Base) | Delta |")
            report.append("| :--- | :---: | :---: |")
            for w in data["new_wins"]:
                report.append("| {} | {:.2f} ({:.2f}) | {:+.2f} |".format(w["display_name"], w["mos"], w["b_mos"], w["delta"]))
            report.append("\n</details>")

        if data["significant_wins"]:
            report.append("<details><summary><b>🌟 Significant Wins ({})</b></summary>\n".format(len(data["significant_wins"])))
            report.append("| Test Case | Status | MOS (Base) | Delta | Size Δ |")
            report.append("| :--- | :---: | :---: | :---: | :---: |")
            for w in data["significant_wins"]: report.append(w["line"])
            report.append("\n</details>")

        if data["opportunities"]:
            report.append("<details><summary><b>💡 Opportunities/Warnings ({})</b></summary>\n".format(len(data["opportunities"])))
            report.append("| Test Case | Status | MOS (Base) | Delta | Size Δ |")
            report.append("| :--- | :---: | :---: | :---: | :---: |")
            for o in data["opportunities"]: report.append(o["line"])
            report.append("\n</details>")

        if data["all_cases"]:
            report.append(f"<details><summary><b>View All {len(data['all_cases'])} cases</b></summary>\n")
            report.append("| Test Case | Status | MOS (Base) | Delta | Size Δ |")
            report.append("| :--- | :---: | :---: | :---: | :---: |")
            for c in data["all_cases"]: report.append(c["line"])
            report.append("\n</details>")

    print("\n".join(report))

    if overall_regression or overall_missing:
        sys.exit(1)

if __name__ == "__main__":
    main()
