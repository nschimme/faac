import subprocess
import os
import sys
import json
from pathlib import Path

# Add benchmark to path for config
sys.path.append("/opt/faac-benchmark")
from config import SCENARIOS

def modify_source(nf, pm):
    with open("libfaac/util.c", "r") as f:
        lines = f.readlines()
    new_lines = []
    skip = False
    for line in lines:
        if "faac_real calc_noisefloor" in line:
            new_lines.append(line); new_lines.append("{\n"); new_lines.append(f"    return {nf};\n"); new_lines.append("}\n")
            skip = True
        elif "faac_real calc_powm" in line:
            new_lines.append(line); new_lines.append("{\n"); new_lines.append(f"    return {pm};\n"); new_lines.append("}\n")
            skip = True
        elif line.startswith("}"):
            if skip: skip = False; continue
            new_lines.append(line)
        elif not skip: new_lines.append(line)
    with open("libfaac/util.c", "w") as f: f.writelines(new_lines)

def build():
    subprocess.run(["meson", "setup", "build/test", "--reconfigure"], capture_output=True, check=True)
    subprocess.run(["ninja", "-C", "build/test"], capture_output=True, check=True)

def run_test(scenario, nf, pm):
    modify_source(nf, pm)
    build()
    out_json = f"test_{scenario}_{nf}_{pm}.json"
    cmd = [sys.executable, "/opt/faac-benchmark/run_benchmark.py", "build/test/frontend/faac", "build/test/libfaac/libfaac.so", f"test_{nf}_{pm}", out_json, "--scenarios", scenario, "--coverage", "5"]
    subprocess.run(cmd, capture_output=True, check=True)
    with open(out_json) as f: data = json.load(f)
    scores = [e["mos"] for e in data["matrix"].values() if e.get("mos") is not None]
    return sum(scores)/len(scores) if scores else 0

# Test baseline vs proposed
print(f"VoIP (16k) Baseline: {run_test('voip', 0.4, 0.4)}")
print(f"VoIP (16k) Proposed: {run_test('voip', 1.2, 0.2)}")
print(f"VSS (40k) Baseline: {run_test('vss', 0.4, 0.4)}")
print(f"VSS (40k) Proposed: {run_test('vss', 0.4, 0.4)}") # Should be same
print(f"Music Low (32k) Baseline: {run_test('music_low', 0.4, 0.4)}")
print(f"Music Low (32k) Proposed: {run_test('music_low', 1.2, 0.4)}")
print(f"Music Std (64k) Baseline: {run_test('music_std', 0.4, 0.4)}")
print(f"Music Std (64k) Proposed: {run_test('music_std', 0.1, 0.1)}")
