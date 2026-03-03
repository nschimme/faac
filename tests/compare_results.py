"""
 * FAAC Benchmark Suite
 * Copyright (C) 2026 Nils Schimmelmann
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

def analyze_pair(base_file, cand_file):
    try:
        with open(base_file, "r") as f:
            base = json.load(f)
    except Exception:
        base = {}

    try:
        with open(cand_file, "r") as f:
            cand = json.load(f)
    except Exception:
        return None

    suite_results = {
        "has_regression": False,
        "missing_data": False,
        "mos_delta_sum": 0,
        "mos_count": 0,
        "missing_mos_count": 0,
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
                suite_results["missing_mos_count"] += 1
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
    SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
    results_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(SCRIPT_DIR, "results")

    if not os.path.exists(results_dir):
        sys.exit(1)

    files = os.listdir(results_dir)

    suites = {}
    for f in files:
        if f.endswith("_cand.json"):
            suite_name = f[:-10]
            base_f = suite_name + "_base.json"
            if base_f in files:
                suites[suite_name] = (os.path.join(results_dir, base_f), os.path.join(results_dir, f))

    if not suites:
        sys.exit(1)

    all_suite_data = {}
    overall_regression = False
    overall_missing = False
    total_mos_delta = 0
    total_mos_count = 0
    total_missing_mos = 0
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
            total_missing_mos += data["missing_mos_count"]
            total_tp_reduction += data["tp_reduction"]
            total_lib_chg += data["lib_size_chg"]

            total_regressions += len(data["regressions"])
            total_new_wins += len(data["new_wins"])
            total_significant_wins += len(data["significant_wins"])
            total_bit_exact += data["bit_exact_count"]
            total_cases_all += data["total_cases"]

    avg_mos_delta_str = f"{(total_mos_delta / total_mos_count):+.3f}" if total_mos_count > 0 else "N/A"
    avg_tp_reduction = total_tp_reduction / len(all_suite_data) if all_suite_data else 0
    avg_lib_chg = total_lib_chg / len(all_suite_data) if all_suite_data else 0
    bit_exact_percent = (total_bit_exact / total_cases_all * 100) if total_cases_all > 0 else 0

    report = []
    if overall_regression:
        report.append("## ❌ Action Required: Regressions Detected")
    elif overall_missing:
        report.append("## ❌ Error: Incomplete Benchmark Data")
    elif bit_exact_percent == 100.0:
        report.append("## ✅ Safe to Merge: Refactor Verified (Bit-Identical)")
    elif total_new_wins > 0 or total_significant_wins > 0 or (total_mos_count > 0 and (total_mos_delta/total_mos_count) > 0.01) or avg_tp_reduction > 5:
        report.append("## 🚀 Recommended: Perceptual & Efficiency Gains")
    else:
        report.append("## 📊 Benchmark Summary")

    report.append("\n### 📈 Performance Dashboard")
    report.append("| Metric | Status | Value |")
    report.append("| :--- | :---: | :--- |")

    # MOS Delta
    mos_icon = "🚀" if total_significant_wins > 0 or (total_mos_count > 0 and total_mos_delta/total_mos_count > 0.05) else "✅"
    if overall_regression: mos_icon = "❌"
    report.append(f"| Avg Perceptual (MOS) Delta | {mos_icon} | {avg_mos_delta_str} |")

    # Throughput
    tp_icon = "⚡" if avg_tp_reduction > 10 else "✅"
    if avg_tp_reduction < -5: tp_icon = "📉"
    report.append(f"| Avg Throughput Improvement | {tp_icon} | {avg_tp_reduction:+.1f}% |")

    # Binary Size
    size_icon = "📦" if abs(avg_lib_chg) < 1 else "✅"
    if avg_lib_chg > 5: size_icon = "⚠️"
    report.append(f"| Avg Binary Size Change | {size_icon} | {avg_lib_chg:+.2f}% |")

    # Consistency
    cons_icon = "💎" if bit_exact_percent == 100.0 else "✅"
    if bit_exact_percent < 50.0 and bit_exact_percent > 0: cons_icon = "⚠️"
    report.append(f"| Bitstream Consistency | {cons_icon} | {bit_exact_percent:.1f}% ({total_bit_exact}/{total_cases_all}) |")

    report.append("\n### 🎯 Detailed Signal")
    report.append(f"- **Regressions**: {total_regressions} " + ("(Fix these before merging!)" if total_regressions > 0 else "None"))
    report.append(f"- **New Wins**: {total_new_wins} (Baseline failed, Candidate passed)")
    report.append(f"- **Significant Wins**: {total_significant_wins} (MOS gain > 0.1)")
    if total_missing_mos > 0:
        report.append(f"- ⚠️ **Missing Data**: {total_missing_mos} scores failed to generate.")

    if total_regressions > 0:
        report.append("\n<details open><summary><b>❌ View Regression Details</b></summary>\n")
        for name, data in sorted(all_suite_data.items()):
            if data["regressions"]:
                report.append(f"#### {name}")
                report.append("| Test Case | Status | MOS (Base) | Delta | Size Δ |")
                report.append("| :--- | :---: | :---: | :---: | :---: |")
                for r in data["regressions"]: report.append(r["line"])
        report.append("\n</details>")

    report.append("\n<details><summary><b>🔍 View Full Suite Metrics & Wins</b></summary>\n")
    for name, data in sorted(all_suite_data.items()):
        report.append(f"\n#### {'❌' if data['has_regression'] or data['missing_data'] else '✅'} {name}")
        report.append(f"- MOS Δ: {f'{(data[u'mos_delta_sum'] / data[u'mos_count']):+.3f}' if data[u'mos_count'] > 0 else 'N/A'}")
        report.append(f"- TP Δ: {data['tp_reduction']:+.1f}%, Size Δ: {data['lib_size_chg']:+.2f}%")

        if data["new_wins"]:
            report.append("\n**🆕 New Wins**")
            report.append("| Test Case | MOS (Base) | Delta |")
            report.append("| :--- | :---: | :---: |")
            for w in data["new_wins"]:
                report.append("| {} | {:.2f} ({:.2f}) | {:+.2f} |".format(w["display_name"], w["mos"], w["b_mos"], w["delta"]))

        if data["all_cases"]:
            report.append(f"\n<details><summary>View all {len(data['all_cases'])} cases for {name}</summary>\n")
            report.append("| Test Case | Status | MOS (Base) | Delta | Size Δ |")
            report.append("| :--- | :---: | :---: | :---: | :---: |")
            for c in data["all_cases"]: report.append(c["line"])
            report.append("\n</details>")
    report.append("\n</details>")

    print("\n".join(report))

    if overall_regression or overall_missing:
        sys.exit(1)

if __name__ == "__main__":
    main()
