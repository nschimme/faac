#!/usr/bin/env python3
"""
Stand-alone library footprint inspection and comparison tool for FAAC.

Inspects the primary libraries in the build directory:
  - libfaac (libfaac.so / dylib / dll)
  - libfaad (libfaad3.so / dylib / dll)
  - libfaam (libfaam.so / dylib / dll)
  - libfaar (libfaar.so / dylib / dll)

Extracts section sizes (.text, .rodata, .data, .bss, and total code = .text + .rodata + .data)
using standard system utilities (size/objdump/dumpbin) without requiring external Python packages.
"""

from __future__ import annotations

import argparse
import glob
import os
import re
import subprocess
import sys

PRIMARY_LIBS = [
    ("libfaac", ["libfaac/libfaac.so*", "libfaac/libfaac.*dylib", "libfaac/*faac*.dll"]),
    ("libfaad", ["libfaad/libfaad*.so*", "libfaad/libfaad*.*dylib", "libfaad/*faad*.dll"]),
    ("libfaam", ["libfaam/libfaam.so*", "libfaam/libfaam.*dylib", "libfaam/*faam*.dll"]),
    ("libfaar", ["libfaar/libfaar.so*", "libfaar/libfaar.*dylib", "libfaar/*faar*.dll"]),
]

SECTION_ALIASES = {
    "__text": "text", ".text": "text", "text": "text",
    "__const": "rodata", ".rodata": "rodata", "rodata": "rodata",
    "__cstring": "rodata", ".rdata": "rodata",
    "__data": "data", ".data": "data", "data": "data",
    ".data.rel.ro": "data",
    "__bss": "bss", ".bss": "bss", "bss": "bss", "__common": "bss",
}


def find_library_file(build_dir: str, patterns: list[str]) -> str | None:
    """Find the best shared library binary match in the build directory."""
    for pattern in patterns:
        full_pattern = os.path.join(build_dir, pattern)
        matches = glob.glob(full_pattern)
        # Filter out symlinks if possible, or pick the real file
        files = [m for m in matches if os.path.isfile(m) and not m.endswith(".p") and not m.endswith(".symbols")]
        if files:
            # Sort so canonical library names or real shared libraries come first
            # e.g. libfaac.so or libfaac.so.3.0.0
            files.sort(key=lambda p: (os.path.islink(p), len(p)))
            return files[0]
    return None


def get_section_sizes(path: str) -> dict[str, int]:
    """Extract section sizes (.text, .rodata, .data, .bss, and total_code) from a binary."""
    sections = {"text": 0, "rodata": 0, "data": 0, "bss": 0}
    if not path or not os.path.exists(path):
        return sections

    # 1. Try GNU size (-A)
    try:
        r = subprocess.run(["size", "-A", path], capture_output=True, text=True, check=False)
        if r.returncode == 0:
            for line in r.stdout.splitlines():
                parts = line.split()
                if len(parts) >= 2 and parts[1].isdigit():
                    sec_name = parts[0].strip()
                    sec_size = int(parts[1])
                    if sec_name in SECTION_ALIASES:
                        key = SECTION_ALIASES[sec_name]
                        sections[key] += sec_size
            sections["total_code"] = sections["text"] + sections["rodata"] + sections["data"]
            sections["file_size"] = os.path.getsize(path)
            return sections
    except Exception:
        pass

    # 2. Try BSD size (-m) for macOS
    try:
        r = subprocess.run(["size", "-m", path], capture_output=True, text=True, check=False)
        if r.returncode == 0:
            for line in r.stdout.splitlines():
                # Formats: "Section __text: 71800" or "Section (__TEXT, __text): 8024"
                m = re.search(r"Section\s+(?:\([^)]+\)|[^\s:]+)?\s*([^\s:]+):\s*(\d+)", line)
                if m:
                    sec_name = m.group(1).strip()
                    sec_size = int(m.group(2))
                    if sec_name in SECTION_ALIASES:
                        key = SECTION_ALIASES[sec_name]
                        sections[key] += sec_size
            sections["total_code"] = sections["text"] + sections["rodata"] + sections["data"]
            sections["file_size"] = os.path.getsize(path)
            return sections
    except Exception:
        pass

    # 3. Fallback to Berkeley size
    try:
        r = subprocess.run(["size", path], capture_output=True, text=True, check=False)
        if r.returncode == 0:
            lines = [ln.strip() for ln in r.stdout.splitlines() if ln.strip()]
            if len(lines) >= 2:
                parts = lines[1].split()
                if len(parts) >= 3 and parts[0].isdigit() and parts[1].isdigit() and parts[2].isdigit():
                    sections["text"] = int(parts[0])
                    sections["data"] = int(parts[1])
                    sections["bss"] = int(parts[2])
                    sections["rodata"] = 0
                    sections["total_code"] = sections["text"] + sections["data"]
                    sections["file_size"] = os.path.getsize(path)
                    return sections
    except Exception:
        pass

    # Ultimate fallback: file size
    sections["total_code"] = os.path.getsize(path)
    sections["file_size"] = os.path.getsize(path)
    return sections


def analyze_build_dir(build_dir: str) -> dict[str, dict]:
    """Measure all primary libraries in the given build directory."""
    results = {}
    for lib_name, patterns in PRIMARY_LIBS:
        path = find_library_file(build_dir, patterns)
        if path:
            sizes = get_section_sizes(path)
            results[lib_name] = {
                "path": path,
                "found": True,
                **sizes
            }
        else:
            results[lib_name] = {
                "path": None,
                "found": False,
                "text": 0, "rodata": 0, "data": 0, "bss": 0, "total_code": 0, "file_size": 0
            }
    return results


def format_single_table(results: dict[str, dict]) -> str:
    """Format single build analysis into text table."""
    lines = [
        f"{'Library':<12} {'Path':<28} {'Text':>8} {'Rodata':>8} {'Data':>8} {'BSS':>8} {'Total Code':>12}",
        "-" * 88,
    ]
    for lib, data in results.items():
        if not data["found"]:
            lines.append(f"{lib:<12} {'(not built / found)':<28} {'-':>8} {'-':>8} {'-':>8} {'-':>8} {'-':>12}")
        else:
            p = os.path.basename(data["path"])
            lines.append(
                f"{lib:<12} {p:<28} {data['text']:>8} {data['rodata']:>8} {data['data']:>8} {data['bss']:>8} {data['total_code']:>12}"
            )
    return "\n".join(lines)


def format_single_markdown(results: dict[str, dict]) -> str:
    """Format single build analysis into Markdown table."""
    lines = [
        "| Library | Binary | .text | .rodata | .data | .bss | Total Code |",
        "| :--- | :--- | ---: | ---: | ---: | ---: | ---: |",
    ]
    for lib, data in results.items():
        if not data["found"]:
            lines.append(f"| `{lib}` | *(not found)* | - | - | - | - | - |")
        else:
            p = os.path.basename(data["path"])
            lines.append(
                f"| `{lib}` | `{p}` | {data['text']:,} B | {data['rodata']:,} B | {data['data']:,} B | {data['bss']:,} B | **{data['total_code']:,} B** |"
            )
    return "\n".join(lines)


def compare_builds(base_results: dict[str, dict], cand_results: dict[str, dict], footprint_allow: int = 0) -> tuple[str, str, bool]:
    """Compare baseline and candidate sizes, return (terminal_str, markdown_str, has_regression)."""
    term_lines = [
        f"{'Library':<10} {'Metric':<12} {'Baseline':>10} {'Candidate':>10} {'Change':>12} {'% Change':>10}",
        "-" * 68,
    ]
    md_lines = [
        "| Library | Metric | Baseline | Candidate | Change | % Change |",
        "| :--- | :--- | ---: | ---: | ---: | ---: |",
    ]

    has_regression = False

    for lib in ["libfaac", "libfaad", "libfaam", "libfaar"]:
        b = base_results.get(lib, {})
        c = cand_results.get(lib, {})

        b_code = b.get("total_code", 0)
        c_code = c.get("total_code", 0)

        delta = c_code - b_code
        pct = (delta / b_code * 100) if b_code > 0 else 0.0

        if delta > footprint_allow:
            has_regression = True

        status_sign = "+" if delta > 0 else ""
        pct_sign = "+" if pct > 0 else ""

        term_lines.append(
            f"{lib:<10} {'Total Code':<12} {b_code:>10} {c_code:>10} {status_sign + str(delta) + ' B':>12} {pct_sign + f'{pct:.2f}%':>10}"
        )
        md_lines.append(
            f"| `{lib}` | **Total Code** (`.text+.rodata+.data`) | {b_code:,} B | {c_code:,} B | **{status_sign}{delta:,} B** | {pct_sign}{pct:.2f}% |"
        )

        for sec in ["text", "rodata", "data", "bss"]:
            b_s = b.get(sec, 0)
            c_s = c.get(sec, 0)
            d_s = c_s - b_s
            pct_s = (d_s / b_s * 100) if b_s > 0 else 0.0
            s_sign = "+" if d_s > 0 else ""
            p_sign = "+" if pct_s > 0 else ""
            term_lines.append(
                f"{'':<10} {'.' + sec:<12} {b_s:>10} {c_s:>10} {s_sign + str(d_s) + ' B':>12} {p_sign + f'{pct_s:.2f}%':>10}"
            )
            md_lines.append(
                f"| `{lib}` | `.{sec}` | {b_s:,} B | {c_s:,} B | {s_sign}{d_s:,} B | {p_sign}{pct_s:.2f}% |"
            )

    return "\n".join(term_lines), "\n".join(md_lines), has_regression


def main():
    parser = argparse.ArgumentParser(description="Inspect and compare FAAC library footprints.")
    parser.add_argument("build_dir", nargs="?", default="build", help="Current or candidate build directory")
    parser.add_argument("--baseline", help="Baseline build directory for comparison")
    parser.add_argument("--markdown-output", help="Write Markdown table to file")
    parser.add_argument("--github-summary", action="store_true", help="Append markdown table to $GITHUB_STEP_SUMMARY")
    parser.add_argument("--footprint-allow", type=int, default=0, help="Accepted total code size increase in bytes")
    parser.add_argument("--fail-on-regression", action="store_true", help="Exit with code 1 if total code growth exceeds --footprint-allow")

    args = parser.parse_args()

    cand_results = analyze_build_dir(args.build_dir)

    if args.baseline and os.path.isdir(args.baseline):
        base_results = analyze_build_dir(args.baseline)
        term_table, md_table, has_regression = compare_builds(base_results, cand_results, args.footprint_allow)
        print("=== FAAC Library Footprint Comparison ===")
        print(term_table)

        summary_title = "## 📊 Library Footprint Impact Analysis\n\n"
        if has_regression:
            note = f"> ⚠️ **Warning:** Library size increase exceeds allowance of {args.footprint_allow} B.\n\n"
        else:
            note = "> ✅ **Pass:** Library sizes within expected boundaries.\n\n"
        full_md = summary_title + note + md_table + "\n\n*Measured via `scripts/check_footprint.py` (`.text + .rodata + .data` represents total code/Flash).*\n"
    else:
        term_table = format_single_table(cand_results)
        md_table = format_single_markdown(cand_results)
        print("=== FAAC Library Footprint ===")
        print(term_table)
        full_md = "## 📊 Library Footprint\n\n" + md_table + "\n\n*Measured via `scripts/check_footprint.py` (`.text + .rodata + .data` represents total code/Flash).*\n"
        has_regression = False

    if args.markdown_output:
        with open(args.markdown_output, "w", encoding="utf-8") as f:
            f.write(full_md)

    if args.github_summary and "GITHUB_STEP_SUMMARY" in os.environ:
        summary_path = os.environ["GITHUB_STEP_SUMMARY"]
        with open(summary_path, "a", encoding="utf-8") as f:
            f.write(full_md)

    if args.fail_on_regression and has_regression:
        sys.exit(1)


if __name__ == "__main__":
    main()
