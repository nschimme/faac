import subprocess
import os
import json
import re

QUANT_FILE = "libfaac/quantize.c"

def update_quant_file(nf, af, mf):
    with open(QUANT_FILE, "r") as f:
        content = f.read()
    content = re.sub(r'#define NOISEFLOOR \d+\.\d+', f'#define NOISEFLOOR {nf}', content)
    content = re.sub(r'#define AVGE_FLOOR_FACTOR \d+\.\d+', f'#define AVGE_FLOOR_FACTOR {af}', content)
    content = re.sub(r'#define MAXE_FLOOR_FACTOR \d+\.\d+', f'#define MAXE_FLOOR_FACTOR {mf}', content)
    with open(QUANT_FILE, "w") as f:
        f.write(content)

def run_benchmark(name):
    subprocess.run(["meson", "compile", "-C", "build"], check=True)
    output_file = f"/tmp/sweep_{name}.json"
    cmd = [
        "python3", "/opt/faac-benchmark/run_benchmark.py",
        "./build/frontend/faac", "./build/libfaac/libfaac.so",
        name, output_file,
        "--scenarios", "voip,vss,music_std",
        "--coverage", "15",
        "--backend", "visqol"
    ]
    env = os.environ.copy()
    env["VISQOL_BIN"] = "/opt/visqol/bazel-bin/visqol"
    env["VISQOL_MODEL_DIR"] = "/opt/visqol/model"
    subprocess.run(cmd, env=env, check=True)
    with open(output_file, "r") as f:
        data = json.load(f)
    matrix = data.get("matrix", {})
    scenarios = {}
    for key, val in matrix.items():
        sc = val["scenario"]
        if sc not in scenarios:
            scenarios[sc] = {"mos": [], "br_err": []}
        if val.get("mos"):
            scenarios[sc]["mos"].append(val["mos"])
        if val.get("bitrate") and val.get("bitrate_target"):
            scenarios[sc]["br_err"].append(val["bitrate"] / val["bitrate_target"] - 1.0)

    summary = {}
    for sc, info in scenarios.items():
        avg_mos = sum(info["mos"]) / len(info["mos"]) if info["mos"] else 0
        avg_br_err = sum(info["br_err"]) / len(info["br_err"]) if info["br_err"] else 0
        summary[sc] = {"mos": avg_mos, "br_err": avg_br_err}
    return summary

configs = [
    (0.15, 0.001, 0.005),
]

for nf, af, mf in configs:
    name = f"nf{nf}_af{af}_mf{mf}"
    print(f"Testing {name}...")
    update_quant_file(nf, af, mf)
    try:
        summary = run_benchmark(name)
        print(f"Results: {summary}")
    except Exception as e:
        print(f"Failed {name}: {e}")
    subprocess.run(["git", "checkout", "libfaac/quantize.c"], check=True)
